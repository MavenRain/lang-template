import assert from 'node:assert/strict';
import { spawnSync } from 'node:child_process';
import { mkdtemp, readFile, rm, writeFile } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { fileURLToPath } from 'node:url';
import test from 'node:test';
import { sourceLimit } from '../bin/bridge.mjs';
import { compile, encoder, run } from './helper.mjs';

const values = source => {
  const result = run(source);
  assert.equal(result['sample-lang'], 1, JSON.stringify(result));
  return result.instances.map(instance => instance.value);
};
const reject = (source, message) => {
  const result = run(source);
  assert.deepEqual(Object.keys(result), ['error']);
  assert.equal(typeof result.error.byte, 'number');
  assert.equal(typeof result.error.message, 'string');
  if (message) assert.match(result.error.message, message);
  return result.error;
};

test('empty program and comments', () => {
  assert.deepEqual(run(''), { 'sample-lang': 1, instances: [] });
  assert.deepEqual(values('-- comment\n \t\r -- final comment'), []);
});

test('Nat decimal boundaries round trip', () => {
  for (const number of [0, 1, 9, 10, 99, 100, 255, 1000, 1_000_000_000, 1_073_741_823]) {
    assert.deepEqual(values(`def n : Nat := ${number}`), [number]);
  }
  assert.deepEqual(values('def n : Nat := 00042'), [42]);
});

test('flags and UTF-8 text', () => {
  assert.deepEqual(values('def a : Flag := flagNo def b : Flag := flagYes def t : Text := "héllo 😀"'),
    [false, true, 'héllo 😀']);
});

test('string escapes and JSON control escaping', () => {
  const text = '\"\\/\n\r\t\b\f';
  assert.deepEqual(values(`def text : Text := ${JSON.stringify(text)}`), [text]);
  assert.deepEqual(values('def text : Text := "\\/"'), ['/']);
  for (const byte of [0, 1, 8, 9, 10, 13, 15, 16, 31, 34, 92, 127]) {
    assert.deepEqual(values(`def text : Text := textByte ${byte} textEnd`), [String.fromCharCode(byte)]);
  }
});

test('UTF-8 built from core text constructors', () => {
  assert.deepEqual(values('def t : Text := textByte 195 (textByte 169 textEnd)'), ['é']);
  assert.deepEqual(values('def t : Text := textByte 240 (textByte 159 (textByte 152 (textByte 128 textEnd)))'), ['😀']);
});

test('Option nested presence stays distinct', () => {
  assert.deepEqual(values('def a : Option Text := none def b : Option Text := some "x"'), [null, 'x']);
  assert.deepEqual(values('def a : Option (Option Text) := none def b : Option (Option Text) := some none def c : Option (Option Text) := some (some "x")'),
    [null, { some: null }, { some: 'x' }]);
});

test('homogeneous lists and nested type arguments', () => {
  assert.deepEqual(values('def a : List Nat := nil def b : List Nat := cons 1 (cons 2 nil) def c : List (Option Text) := cons none (cons (some "x") nil)'),
    [[], [1, 2], [null, 'x']]);
});

test('all Value constructors', () => {
  assert.deepEqual(values('def a : Value := valueNull def b : Value := valueFlag flagYes def c : Value := valueNat 7 def d : Value := valueText "x" def e : Value := valueItems valuesEnd def f : Value := valueAttrs attrsEnd'),
    [null, true, 7, 'x', [], {}]);
});

test('Values and Attrs compose without losing order', () => {
  const source = 'def a : Values := valuesItem (valueNat 1) (valuesItem valueNull valuesEnd) def b : Attrs := attrsField "z" (valueItems a) (attrsField "a" (valueFlag flagNo) attrsEnd)';
  assert.deepEqual(values(source), [[1, null], { z: [1, null], a: false }]);
  const output = compile(encoder.encode(source));
  assert.ok(output.indexOf('"z":') < output.indexOf('"a":'));
});

test('products and both sum injections', () => {
  assert.deepEqual(values('def a : Prod Nat (List Text) := pair 7 (cons "x" nil) def b : Sum Text Nat := inl "x" def c : Sum Text Nat := inr 9'),
    [{ first: 7, second: ['x'] }, { inl: 'x' }, { inr: 9 }]);
  assert.equal(run('def a : Prod Nat Text := pair 7 "x"').instances[0].type, 'Prod (Nat) (Text)');
});

test('domain enum constructors', () => {
  assert.deepEqual(values('def a : Measure := measurePiece def b : Measure := measureBox'),
    ['measurePiece', 'measureBox']);
});

test('earlier definitions retain types and declaration order', () => {
  const result = run('def a : Nat := 7 def b : Nat := a def c : List Nat := cons b nil');
  assert.deepEqual(result.instances.map(item => item.name), ['a', 'b', 'c']);
  assert.deepEqual(result.instances.map(item => item.value), [7, 7, [7]]);
});

test('example program compiles', async () => {
  const result = run(await readFile(new URL('../examples/values.lang', import.meta.url), 'utf8'));
  assert.equal(result.instances.length, 9);
  assert.deepEqual(result.instances.at(-1).value, { first: 'Acme', second: ['Mira', 'Jo'] });
});

for (const [label, source, message] of [
  ['overflow', 'def n : Nat := 1073741824', /Nat exceeds/],
  ['large overflow', 'def n : Nat := 999999999999999999999999999999', /Nat exceeds/],
  ['negative Nat', 'def n : Nat := -1', /unexpected source byte/],
  ['wrong literal type', 'def n : Nat := "x"', /declared type/],
  ['wrong reference type', 'def a : Nat := 1 def b : Text := a', /declared type/],
  ['wrong list item', 'def a : List Nat := cons "x" nil', /declared type/],
  ['duplicate definition', 'def a : Nat := 1 def a : Nat := 2', /duplicate definition/],
  ['reserved definition', 'def cons : Nat := 1', /reserved definition/],
  ['self reference', 'def a : Nat := a', /unknown name/],
  ['forward reference', 'def a : Nat := b def b : Nat := 1', /unknown name/],
  ['unknown type', 'def a : MadeUp := 1', /supported type/],
  ['unknown constructor', 'def a : Flag := yes', /unknown name/],
  ['duplicate attribute', 'def a : Attrs := attrsField "x" valueNull (attrsField "x" (valueNat 1) attrsEnd)', /duplicate attribute/],
  ['equal decoded attribute', 'def a : Attrs := attrsField "x" valueNull (attrsField (textByte 120 textEnd) valueNull attrsEnd)', /duplicate attribute/],
  ['invalid byte', 'def a : Text := textByte 256 textEnd', /byte below 256/],
  ['invalid text value', 'def a : Text := textByte 128 textEnd', /invalid UTF-8/],
  ['overlong UTF-8', 'def a : Text := textByte 192 (textByte 128 textEnd)', /invalid UTF-8/],
  ['surrogate UTF-8', 'def a : Text := textByte 237 (textByte 160 (textByte 128 textEnd))', /invalid UTF-8/],
  ['out of range UTF-8', 'def a : Text := textByte 244 (textByte 144 (textByte 128 (textByte 128 textEnd)))', /invalid UTF-8/],
  ['unterminated string', 'def a : Text := "x', /invalid string/],
  ['unsupported escape', 'def a : Text := "\\u0061"', /string escape/],
  ['raw control byte', 'def a : Text := "\n"', /invalid string/],
  ['raw DEL byte', 'def a : Text := "\x7f"', /invalid string/],
  ['missing close', 'def a : Nat := (1', /closing parenthesis/],
  ['extra term', 'def a : Nat := 1 2', /expected def/],
  ['missing assign', 'def a : Nat 1', /expected def/],
  ['invalid second definition', 'def a : Nat := 1 def b : Text := 2', /declared type/],
]) {
  test(`rejects ${label}`, () => reject(source, message));
}

test('rejects new core types, axioms, and general recursion', () => {
  for (const source of ['mu A : Type 0 with', 'axiom a : Nat', 'def rec a : Nat := a', 'poly a : Nat := 0', 'specialize a']) reject(source);
  for (const type of ['Eq Nat 1 1', 'Sigma Nat Nat', 'Type 0', '(x : Nat) -> Nat', 'Item']) reject(`def a : ${type} := 1`);
});

test('EOF diagnostics carry the source byte position', () => {
  const source = 'def a : Nat :=';
  assert.equal(reject(source).byte, encoder.encode(source).length);
});

test('invalid source UTF-8 is diagnosed in the reactor at the first bad byte', () => {
  for (const [bytes, byte] of [[[0xc0, 0x80], 0], [[0xed, 0xa0, 0x80], 1], [[0xf4, 0x90, 0x80, 0x80], 1], [[0xc3], 0]]) {
    const result = JSON.parse(compile(Uint8Array.from(bytes)));
    assert.match(result.error.message, /invalid UTF-8/);
    assert.equal(result.error.byte, byte);
  }
  const prefix = encoder.encode('def a : Text := "a');
  assert.equal(JSON.parse(compile(Uint8Array.from([...prefix, 0xff, 0x22]))).error.byte, prefix.length);
});

test('checker fuel bounds deep nesting at the token where fuel runs out', async () => {
  const source = 'def a : Nat := ' + '('.repeat(600) + '0' + ')'.repeat(600);
  const error = reject(source, /fuel exhausted/);
  assert.ok(error.byte > 'def a : Nat := '.length);
  assert.equal(source[error.byte], '(');
  // Pin the measured limits stated in README.md. Each count includes the
  // innermost form: the stated count compiles and one more exhausts fuel.
  const readme = (await readFile(new URL('../docs/host/README.md', import.meta.url), 'utf8')
    .catch(error => {
      if (error.code !== 'ENOENT') throw error;
      return readFile(new URL('../README.md', import.meta.url), 'utf8');
    })).replace(/\s+/g, ' ');
  const limits = readme.match(/definition are (\d+) nested parentheses, (\d+) nested `Option` type formers, (\d+) nested `textByte` or `cons` terms, and (\d+) nested `attrsField` terms\./);
  assert.ok(limits, 'README.md states the fuel limits');
  const [parens, options, terms, attrs] = limits.slice(1).map(Number);
  const nest = (outer, inner, count) => Array.from({ length: count - 1 }, (_, index) => outer(index)).join('')
    + inner(count - 1) + ')'.repeat(count - 1);
  const forms = [
    [parens, count => 'def a : Nat := ' + '('.repeat(count) + '0' + ')'.repeat(count)],
    [options, count => 'def a : ' + nest(() => 'Option (', () => 'Option Text', count) + ' := none'],
    [terms, count => 'def a : Text := ' + nest(() => 'textByte 97 (', () => 'textByte 97 textEnd', count)],
    [terms, count => 'def a : List Nat := ' + nest(() => 'cons 0 (', () => 'cons 0 nil', count)],
    [attrs, count => 'def a : Attrs := ' + nest(index => `attrsField "k${index}" (valueNat 0) (`,
      index => `attrsField "k${index}" (valueNat 0) attrsEnd`, count)],
  ];
  for (const [limit, form] of forms) {
    assert.equal(values(form(limit)).length, 1, form(limit).slice(0, 40));
    reject(form(limit + 1), /fuel exhausted/);
  }
});

const sharedValues = () => {
  let source = `def a0 : Values := valuesItem (valueText "${'x'.repeat(100)}") valuesEnd\n`;
  for (let index = 1; index <= 18; index++) source += `def a${index} : Values := valuesItem (valueItems a${index - 1}) a${index - 1}\n`;
  return source;
};

test('shared values have a total serialization budget', () => {
  const source = sharedValues();
  const error = reject(source, /output budget exceeded/);
  assert.equal(source.slice(error.byte, error.byte + 4), 'def ');
});

test('the output budget counts emitted bytes', () => {
  const outcomes = new Set();
  for (let doublings = 0; doublings <= 8; doublings++) {
    let source = 'def t : Text := ' + 'textByte 1 ('.repeat(100) + 'textEnd' + ')'.repeat(100) + '\n';
    source += 'def v0 : Values := valuesItem (valueText t) valuesEnd\n';
    for (let index = 1; index <= doublings; index++) source += `def v${index} : Values := valuesItem (valueItems v${index - 1}) v${index - 1}\n`;
    const output = compile(encoder.encode(source));
    const result = JSON.parse(output);
    if (result.error) {
      assert.match(result.error.message, /output budget exceeded/);
      outcomes.add('refused');
    } else {
      assert.ok(encoder.encode(output).length <= 32 * encoder.encode(source).length + 128);
      outcomes.add('printed');
    }
  }
  assert.deepEqual([...outcomes].sort(), ['printed', 'refused']);
});

test('print-time errors report the byte of the definition', () => {
  const source = 'def a : Nat := 1 def b : Attrs := attrsField (textByte 255 textEnd) valueNull attrsEnd';
  assert.equal(reject(source, /invalid UTF-8/).byte, source.indexOf('def b'));
});

test('long names and keys compare without deep recursion', () => {
  const key = 'a'.repeat(64_000);
  assert.deepEqual(values(`def x : Attrs := attrsField "b" valueNull (attrsField "${key}" valueNull attrsEnd)`),
    [{ b: null, [key]: null }]);
  const name = 'a'.repeat(65_000);
  assert.deepEqual(values(`def ${name} : Nat := 1 def b : Nat := 2`), [1, 2]);
});

test('keys that share a long tail are compared in linear time', () => {
  const keys = Array.from({ length: 300 }, (_, i) => `(textByte ${65 + Math.floor(i / 26)} (textByte ${65 + i % 26} k))`);
  let source = `def k : Text := "${'a'.repeat(40_000)}"\n`;
  for (let d = 0; d < 3; d++) {
    source += `def f${d} : Attrs := ` + keys.slice(d * 100, (d + 1) * 100).map(key => `attrsField ${key} valueNull (`).join('') +
      (d === 0 ? 'attrsEnd' : `f${d - 1}`) + ')'.repeat(100) + '\n';
  }
  source += `def g : Attrs := attrsField ${keys[0]} valueNull f2`;
  const start = performance.now();
  assert.equal(reject(source, /duplicate attribute/).byte, source.indexOf('attrsField', source.indexOf('def g')));
  assert.ok(performance.now() - start < 10_000);
});

test('Option Value keeps presence', () => {
  assert.deepEqual(values('def a : Option Value := none def b : Option Value := some valueNull def c : Option Value := some (valueNat 1)'),
    [null, { some: null }, { some: 1 }]);
  assert.deepEqual(values('def a : Option (Option Value) := some none def b : Option (Option Value) := some (some valueNull)'),
    [{ some: null }, { some: { some: null } }]);
});

test('a carriage return ends a comment', () => {
  assert.deepEqual(values('-- header\rdef n : Nat := 1\rdef m : Nat := 2\r'), [1, 2]);
});

test('nullary and parenthesized arguments need no more parentheses', () => {
  assert.deepEqual(values('def a : Prod (Option Nat) (Option Nat) := pair none (some 1) def b : List Flag := cons flagYes nil def c : Option Nat := (some 1)'),
    [{ first: null, second: 1 }, [true], 1]);
});

for (const [label, source, at, message] of [
  ['a letter after a number', 'def n : Nat := 1def m : Nat := 2', 'def m', /unexpected source byte/],
  ['a bare type former argument', 'def a : Option Option Text := some none', 'Option Text', /argument needs parentheses/],
  ['a bare constructor argument', 'def a : List Text := cons "a" cons "b" nil', 'cons "b"', /argument needs parentheses/],
  ['a bare constructor in a pair', 'def a : Prod (Option Nat) (Option Nat) := pair some 1 none', 'some', /argument needs parentheses/],
]) {
  test(`rejects ${label} at its byte`, () => assert.equal(reject(source, message).byte, source.indexOf(at)));
}

test('ten thousand text bytes survive the complete pipeline', () => {
  const text = 'a'.repeat(10_000);
  assert.deepEqual(values(`def text : Text := "${text}"`), [text]);
});

test('transport source limit is enforced before compilation', () => {
  assert.throws(() => compile(new Uint8Array(sourceLimit + 1)), /source exceeds/);
});

test('a valid source at the 65536-byte limit compiles', () => {
  const prefix = 'def text : Text := "';
  const text = 'a'.repeat(sourceLimit - encoder.encode(prefix).length - 1);
  const source = prefix + text + '"';
  assert.equal(encoder.encode(source).length, sourceLimit);
  assert.deepEqual(values(source), [text]);
});

test('references distinguish nested product and sum types', () => {
  assert.deepEqual(values('def a : Prod Nat (Sum Text Nat) := pair 1 (inr 2) def b : Prod Nat (Sum Text Nat) := a'),
    [{ first: 1, second: { inr: 2 } }, { first: 1, second: { inr: 2 } }]);
  reject('def a : Prod Nat Text := pair 1 "x" def b : Prod Text Nat := a', /declared type/);
  reject('def a : Sum Nat Text := inl 1 def b : Sum Text Nat := a', /declared type/);
});

test('CLI emits JSON on success and only a diagnostic on failure', async () => {
  const directory = await mkdtemp(join(tmpdir(), 'ledger-lang-test-'));
  const path = join(directory, 'program.lang');
  const launcher = fileURLToPath(new URL('../bin/langc', import.meta.url));
  const script = fileURLToPath(new URL('../bin/langc.mjs', import.meta.url));
  const invoke = () => spawnSync('sh', [launcher, path], { encoding: 'utf8', timeout: 30_000 });
  try {
    await writeFile(path, 'def a : Nat := 7');
    const valid = invoke();
    assert.equal(valid.status, 0, valid.stderr);
    assert.equal(JSON.parse(valid.stdout).instances[0].value, 7);
    assert.equal(valid.stderr, '');
    await writeFile(path, 'def a : Nat := "x"');
    const invalid = invoke();
    assert.equal(invalid.status, 1);
    assert.equal(invalid.stdout, '');
    assert.match(invalid.stderr, /byte 15: term does not have the declared type/);
    await writeFile(path, new Uint8Array(sourceLimit + 1));
    const oversized = invoke();
    assert.equal(oversized.status, 1);
    assert.equal(oversized.stdout, '');
    assert.match(oversized.stderr, /program\.lang: source exceeds 65536 bytes/);
    await writeFile(path, sharedValues());
    const budget = invoke();
    assert.equal(budget.status, 1);
    assert.equal(budget.stdout, '');
    assert.match(budget.stderr, /program\.lang: byte \d+: output budget exceeded/);
    const prefix = 'def text : Text := "';
    const text = 'a'.repeat(sourceLimit - prefix.length - 1);
    await writeFile(path, prefix + text + '"');
    for (const largest of [invoke(), spawnSync(process.execPath, [script, path], { encoding: 'utf8', timeout: 30_000 })]) {
      assert.equal(largest.status, 0, largest.stderr);
      assert.equal(JSON.parse(largest.stdout).instances[0].value, text);
    }
  } finally {
    await rm(directory, { recursive: true, force: true });
  }
});
