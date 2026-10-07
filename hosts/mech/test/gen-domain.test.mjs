import assert from 'node:assert/strict';
import { spawnSync } from 'node:child_process';
import { cp, mkdir, mkdtemp, rm, writeFile } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import test from 'node:test';
import { fileURLToPath } from 'node:url';
import { createCompiler } from '../bin/bridge.mjs';
import { parseSchema, renderPlans, textChain } from '../bin/gen-domain.mjs';

const kit = fileURLToPath(new URL('..', import.meta.url));
const fixture = `def domainName : Text := ${textChain('fixture-lang')}

-- A tagged family with a nullary constructor, and a record of options and lists.
mu Glyph : Type 0 with
| glyphDot : Glyph
  | glyphLine : (length : Nat) -> Glyph
  | glyphMarks : (marks : List Flag) -> (note : Option Text) -> Glyph

mu Board : Type 0 with
| makeBoard : (first : Option Glyph) -> (rest : List (Option Glyph)) -> Board
`;

test('families are classified as enum, record or tagged record', () => {
  const kinds = schema => parseSchema(schema).families.map(family => [family.name, family.kind]);
  assert.deepEqual(kinds(fixture), [['Glyph', 'tagged'], ['Board', 'record']]);
  assert.deepEqual(kinds('mu Tone : Type 0 with\n| toneLow : Tone\n| toneHigh : Tone\n'), [['Tone', 'enum']]);
});

test('formatting does not omit families or constructors', () => {
  const compact = 'mu Tone : Type 0 with\n| toneLow : Tone\n| toneHigh : Tone\n';
  const formatted = '\tmu Tone : Type 0 with\n  | toneLow : Tone\n\n  -- The second constructor.\n\t| toneHigh : Tone\n';
  assert.deepEqual(parseSchema(formatted), parseSchema(compact));
  assert.equal(renderPlans(parseSchema(formatted)), renderPlans(parseSchema(compact)));
});

test('field types and tags render to plan terms', () => {
  const plans = renderPlans(parseSchema(fixture));
  [
    'recordPlan nil nil (some domainTextglyphDot)',
    'recordPlan (cons (tyList tyFlag) (cons (tyOption tyText) nil)) (cons domainTextmarks (cons domainTextnote nil)) (some domainTextglyphMarks)',
    'recordPlan (cons (tyOption (tyNamed domainTextGlyph)) (cons (tyList (tyOption (tyNamed domainTextGlyph))) nil)) (cons domainTextfirst (cons domainTextrest nil)) none',
  ].forEach(term => assert.ok(plans.includes(term), term));
});

test('text chains hold UTF-8 bytes', () => {
  assert.equal(textChain('é'), 'textByte 195 (textByte 169 (textEnd))');
});

test('the generator refuses schemas that it cannot plan', () => {
  [
    ['', /at least one family/],
    ['mu Tone : Type 0 with\n', /no constructors/],
    ['mu Box (0 A : Type 0) : Type 0 with\n| box : Box A\n', /must have the form mu Name/],
    ['| toneLow : Tone\n', /outside a family/],
    ['mu Tone : Type 0 with\n| toneLow : Nat\n', /must return Tone/],
    ['mu Item : Type 0 with\n| makeItem : Text -> Item\n', /field 1 of makeItem/],
    ['mu Item : Type 0 with\n| makeItem : (name : Count) -> Item\n', /unknown type Count/],
    ['mu Item : Type 0 with\n| makeItem : (name : List) -> Item\n', /incomplete type/],
    ['mu Item : Type 0 with\n| makeItem : (name : (Text) -> Item\n', /unbalanced parentheses/],
    ['mu Item : Type 0 with\n| makeItem : (name : Text) -> (name : Nat) -> Item\n', /repeats field name/],
    ['mu Item : Type 0 with\n| item : Item\nmu Tone : Type 0 with\n| item : Tone\n', /item is declared twice/],
    ['mu Item : Type 0 with\n| itemA : (tag : Text) -> Item\n| itemB : Item\n', /cannot use the field name tag/],
    ['mu Constructor : Type 0 with\n| makeConstructor : Constructor\n', /domainConstructorPlan is not unique/],
  ].forEach(([schema, message]) => assert.throws(() => parseSchema(schema), message, JSON.stringify(schema)));
});

test('a generated domain compiles tagged records, options and lists', async () => {
  const directory = await mkdtemp(join(tmpdir(), 'gen-domain-'));
  try {
    await Promise.all(['compiler', 'bin', 'prelude.mech'].map(path =>
      cp(join(kit, path), join(directory, path), { recursive: true })));
    await mkdir(join(directory, 'domain'));
    await writeFile(join(directory, 'domain/schema.mech'), fixture);
    [['bin/gen-domain.mjs'], ['bin/gen-domain.mjs', '--check'], ['bin/build.mjs']].forEach(args => {
      const result = spawnSync(process.execPath, args, { cwd: directory, encoding: 'utf8', timeout: 120_000 });
      assert.equal(result.status, 0, result.stderr);
    });
    const compile = await createCompiler(join(directory, 'build/langc.wasm'));
    const run = source => JSON.parse(compile(new TextEncoder().encode(source)));
    [
      ['def a : Glyph := glyphDot', { tag: 'glyphDot' }],
      ['def a : Glyph := glyphLine 3', { tag: 'glyphLine', length: 3 }],
      ['def a : Glyph := glyphMarks (cons flagYes nil) (some "n")', { tag: 'glyphMarks', marks: [true], note: 'n' }],
      ['def a : Glyph := glyphMarks nil none', { tag: 'glyphMarks', marks: [], note: null }],
      ['def a : Board := makeBoard (some glyphDot) (cons none (cons (some (glyphLine 2)) nil))',
        { first: { tag: 'glyphDot' }, rest: [null, { tag: 'glyphLine', length: 2 }] }],
    ].forEach(([source, value]) => assert.deepEqual(run(source), {
      'fixture-lang': 1,
      instances: [{ name: 'a', type: source.split(' ')[3], value }],
    }, source));
    [
      ['def a : Glyph := glyphLine "x"', 'term does not have the declared type'],
      ['def a : Board := makeBoard (some 1) nil', 'term does not have the declared type'],
      ['def glyphDot : Nat := 1', 'reserved definition name'],
      ['def Board : Nat := 1', 'reserved definition name'],
    ].forEach(([source, message]) => assert.equal(run(source).error?.message, message, source));
  } finally {
    await rm(directory, { recursive: true, force: true });
  }
});
