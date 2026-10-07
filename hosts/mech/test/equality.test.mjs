import assert from 'node:assert/strict';
import test from 'node:test';
import { compile, encoder, run, reject } from './helper.mjs';

const instances = source => {
  const result = run(source);
  assert.equal(result['sample-lang'], 1, JSON.stringify(result));
  return result.instances;
};
const messageOf = source => run(source).error.message;
const termError = messageOf('def x : Nat := "a"');
const parenError = messageOf('def x : Option Option Text := none');
const productError = messageOf('def n : Nat := 1 def x : Nat := first n');
const inferError = messageOf('def x : Nat := first 1');
const supportedError = 'expected a supported type';
const dataError = 'expected a data type';
const reflError = 'refl needs two equal sides';
const proofError = 'expected an equality proof';
const transError = 'trans needs the same middle value';
// Each case names the last occurrence of `at` in the source as the error byte.

test('proofs are not instances', () => {
  assert.deepEqual(instances('def p : Eq Nat 2 2 := refl def n : Nat := 3'),
    [{ name: 'n', type: 'Nat', value: 3 }]);
});

test('refl compares the values of the sides, also through references', () => {
  assert.deepEqual(instances('def a : Nat := 1 def p : Eq Nat a 1 := refl'),
    [{ name: 'a', type: 'Nat', value: 1 }]);
  assert.deepEqual(instances('def c : Prod Text Nat := pair "a" 1 def p : Eq Nat (second c) 1 := refl'),
    [{ name: 'c', type: 'Prod (Text) (Nat)', value: { first: 'a', second: 1 } }]);
});

test('sides of compound data types', () => {
  assert.deepEqual(instances('def p : Eq (Option Nat) (some 1) (some 1) := refl '
    + 'def q : Eq (Prod Text Nat) (pair "a" 1) (pair "a" 1) := refl '
    + 'def r : Eq (List Text) (cons "x" nil) (cons "x" nil) := refl'), []);
});

test('symm and trans compose proofs', () => {
  assert.deepEqual(instances('def a : Nat := 1 def p : Eq Nat a 1 := refl def q : Eq Nat 1 a := symm p '
    + 'def r : Eq Nat a a := trans p q def s : Eq Nat 1 1 := trans (symm p) (symm q) def t : Eq Nat 1 1 := (r)'),
  [{ name: 'a', type: 'Nat', value: 1 }]);
});

test('a reference to a proof keeps its equality type', () => {
  assert.deepEqual(instances('def p : Eq Nat 1 1 := refl def q : Eq Nat 1 1 := p'), []);
});

const rejections = [
  ['refl with different sides', 'def p : Eq Nat 1 2 := refl', reflError, 'refl'],
  ['refl at a data type', 'def n : Nat := refl', termError, 'refl'],
  ['refl in a synthesized position', 'def p : Eq Nat 1 1 := symm refl', inferError, 'refl'],
  ['symm of a data term', 'def n : Nat := 1 def p : Eq Nat 1 1 := symm n', proofError, 'symm'],
  ['trans with different middle values',
    'def p : Eq Nat 1 1 := refl def q : Eq Nat 2 2 := refl def r : Eq Nat 1 2 := trans p q', transError, 'trans'],
  ['trans with different carrier types',
    'def p : Eq Text "x" "x" := refl def q : Eq Nat 1 1 := refl def r : Eq Nat 1 1 := trans p q', transError, 'trans'],
  ['symm at a wrong declared type', 'def p : Eq Text "a" "a" := refl def q : Eq Text "a" "b" := symm p', termError, 'symm'],
  ['a proof as a data term', 'def p : Eq Nat 1 1 := refl def n : Nat := p', termError, 'p'],
  ['first of a proof', 'def p : Eq Nat 1 1 := refl def n : Nat := first p', productError, 'first'],
  ['a bare symm argument', 'def p : Eq Nat 1 1 := refl def r : Eq Nat 1 1 := trans symm p p', parenError, 'symm'],
  ['Eq inside a type former', 'def x : Option (Eq Nat 1 1) := none', supportedError, 'Eq'],
  ['Eq as a type definition', 'def T : Type 0 := Eq Nat 1 1', supportedError, 'Eq'],
  ['Eq over a universe', 'def p : Eq (Type 0) Nat Nat := refl', dataError, '('],
  ['a missing side', 'def p : Eq Nat 1 := refl', termError, ':='],
  ['a side of the wrong type', 'def p : Eq Nat "a" "a" := refl', termError, '"a" "a"'],
];
for (const [label, source, message, at] of rejections) {
  test(`rejects ${label} at its byte`, () => reject(source, message, at));
}
