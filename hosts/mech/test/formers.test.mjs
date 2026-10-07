import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import test from 'node:test';
import { compile, encoder, run, reject } from './helper.mjs';

const instances = source => {
  const result = run(source);
  assert.equal(result['sample-lang'], 1, JSON.stringify(result));
  return result.instances;
};
const messageOf = source => run(source).error.message;
const typeError = messageOf('def x : Bogus := 1');
const termError = messageOf('def x : Nat := "a"');
const parenError = messageOf('def x : Option Option Text := none');
const reservedError = messageOf('def Nat : Nat := 1');
const duplicateError = messageOf('def a : Nat := 1 def a : Nat := 2');
// Each case names the last occurrence of `at` in the source as the error byte.

test('type definitions are not instances and normalize in output types', () => {
  assert.deepEqual(instances('def Contact : Type 0 := Prod Text Nat def c : Contact := pair "Mira" 7'),
    [{ name: 'c', type: 'Prod (Text) (Nat)', value: { first: 'Mira', second: 7 } }]);
});

test('Type 1 classifies Type 0 and type definitions compose', () => {
  assert.deepEqual(instances('def U : Type 1 := Type 0 def T : U := Option Text def K : Type 0 := Measure ' +
    'def x : T := some "a" def ks : List K := cons measurePiece nil'),
    [{ name: 'x', type: 'Option (Text)', value: 'a' }, { name: 'ks', type: 'List (Measure)', value: ['measurePiece'] }]);
});

test('first and second project products, also as constructor arguments', () => {
  const source = 'def p : Prod Text (Prod Nat Flag) := pair "a" (pair 1 flagYes) ' +
    'def a : Text := first p def b : Flag := second (second p) def n : Nat := (first (second p)) ' +
    'def q : Prod Nat (List Text) := pair (first (second p)) (cons (first p) nil) ' +
    'def P : Type 0 := Prod Nat Nat def r : P := pair 4 5 def s : Nat := second r';
  assert.deepEqual(instances(source).map(item => item.value),
    [{ first: 'a', second: { first: 1, second: true } }, 'a', true, 1, { first: 1, second: ['a'] },
      { first: 4, second: 5 }, 5]);
});

test('formers example compiles', async () => {
  const source = await readFile(new URL('../examples/formers.lang', import.meta.url), 'utf8');
  const contact = 'Prod (Text) (Option (Text))';
  assert.deepEqual(instances(source), [
    { name: 'mira', type: contact, value: { first: 'Mira', second: 'mira@example.com' } },
    { name: 'jo', type: contact, value: { first: 'Jo', second: null } },
    { name: 'team', type: `List (${contact})`,
      value: [{ first: 'Mira', second: 'mira@example.com' }, { first: 'Jo', second: null }] },
    { name: 'miraName', type: 'Text', value: 'Mira' },
    { name: 'joEmail', type: 'Option (Text)', value: null },
    { name: 'names', type: 'Prod (Text) (Text)', value: { first: 'Mira', second: 'Jo' } },
  ]);
});

const rejections = [
  ['a universe in its own universe', 'def T : Type 0 := Type 0', /universe/, 'Type 0'],
  ['a data type in Type 1', 'def T : Type 1 := Nat', /universe/, 'Nat'],
  ['a universe above Type 1', 'def T : Type 2 := Nat', typeError, '2'],
  ['Type without a level', 'def T : Type := Nat', typeError, ':='],
  ['a universe as a type argument', 'def x : Option (Type 0) := none', /data type/, '(Type'],
  ['a universe as a second type argument', 'def p : Prod Nat (Type 0) := pair 1 2', /data type/, '(Type'],
  ['a bare Type argument', 'def x : List Type 0 := nil', parenError, 'Type'],
  ['a type definition as a term', 'def T : Type 0 := Nat def x : Nat := T', termError, 'T'],
  ['an instance as a type', 'def n : Nat := 1 def x : n := 2', typeError, 'n :='],
  ['a projection of a literal', 'def x : Nat := first 1', /infer/, '1'],
  ['a projection of a constructor', 'def x : Nat := first (pair 1 2)', /infer/, 'pair'],
  ['a projection of a type definition', 'def T : Type 0 := Nat def x : Nat := first T', /infer/, 'T'],
  ['a projection of a non-product', 'def t : Text := "a" def x : Nat := first t', /product/, 'first'],
  ['a projection at the wrong type', 'def p : Prod Nat Nat := pair 1 2 def x : Text := first p', termError, 'first'],
  ['a nested projection without parentheses',
    'def p : Prod (Prod Nat Nat) Nat := pair (pair 1 2) 3 def x : Nat := first first p', parenError, 'first'],
  ['a projection as a bare argument',
    'def p : Prod Nat Nat := pair 1 2 def q : Prod Nat Nat := pair first p 3', parenError, 'first'],
  ['a repeated type definition name', 'def T : Type 0 := Nat def T : Nat := 1', duplicateError, 'T'],
];
for (const [label, source, message, at] of rejections) {
  test(`rejects ${label} at its byte`, () => reject(source, message, at));
}

test('every SPEC form name is reserved', () => {
  for (const name of ['Type', 'fun', 'Sigma', 'pack', 'witness', 'payload', 'Eq', 'refl', 'transport', 'symm',
    'trans', 'cong', 'first', 'second', 'either', 'pure', 'map', 'bind', 'fold', 'unfold', 'filter']) {
    reject(`def ${name} : Nat := 1`, reservedError, name);
  }
});
