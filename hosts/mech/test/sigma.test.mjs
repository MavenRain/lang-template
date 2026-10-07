import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import test from 'node:test';
import { run, reject } from './helper.mjs';

const instances = source => {
  const result = run(source);
  assert.equal(result['sample-lang'], 1, JSON.stringify(result));
  return result.instances;
};
const sigmaError = 'expected a Sigma type';
const reservedError = 'reserved definition name';
const duplicateError = 'duplicate definition name';
const dataError = 'expected a data type';
const supportedError = 'expected a supported type';
const unknownError = 'unknown name or constructor';
const termError = 'term does not have the declared type';
const inferError = 'cannot infer the type of this term';
const reflError = 'refl needs two equal sides';
const productError = 'expected a term of a product type';
// Each case names the last occurrence of `at` in the source as the error byte.

test('a non-dependent Sigma prints the same JSON as a pair', () => {
  assert.deepEqual(instances('def s : Sigma (n : Nat) Text := pack 3 "a" def q : Prod Nat Text := pair 3 "a"'), [
    { name: 's', type: 'Sigma (x : Nat) (Text)', value: { first: 3, second: 'a' } },
    { name: 'q', type: 'Prod (Nat) (Text)', value: { first: 3, second: 'a' } },
  ]);
});

test('the payload is checked against the type with the witness value', () => {
  assert.deepEqual(instances('def price : Nat := 1200 def s : Sigma (n : Nat) (Eq Nat n price) := pack 1200 refl '
    + 'def w : Nat := witness s def p : Eq Nat (witness s) 1200 := payload s'), [
    { name: 'price', type: 'Nat', value: 1200 },
    { name: 's', type: 'Sigma (x : Nat) (Eq (Nat) (x) (1200))', value: { first: 1200, second: null } },
    { name: 'w', type: 'Nat', value: 1200 },
  ]);
});

test('witness and payload give the packed parts', () => {
  assert.deepEqual(instances('def s : Sigma (n : Nat) Text := pack 1 "a" def t : Text := payload s def u : Nat := witness s'), [
    { name: 's', type: 'Sigma (x : Nat) (Text)', value: { first: 1, second: 'a' } },
    { name: 't', type: 'Text', value: 'a' },
    { name: 'u', type: 'Nat', value: 1 },
  ]);
});

test('a payload proof fits the instantiated equality', () => {
  assert.deepEqual(instances('def s : Sigma (n : Nat) (Eq Nat n 1200) := pack 1200 refl def p : Eq Nat 1200 1200 := payload s'),
    [{ name: 's', type: 'Sigma (x : Nat) (Eq (Nat) (x) (1200))', value: { first: 1200, second: null } }]);
});

test('packing the witness and the payload gives the same instance', () => {
  const [s, t] = instances('def s : Sigma (n : Nat) (Eq Nat n 2) := pack 2 refl '
    + 'def t : Sigma (n : Nat) (Eq Nat n 2) := pack (witness s) (payload s)');
  assert.deepEqual({ type: t.type, value: t.value }, { type: s.type, value: s.value });
  const [u, v] = instances('def u : Sigma (n : Nat) Text := pack 5 "e" def v : Sigma (n : Nat) Text := pack (witness u) (payload u)');
  assert.deepEqual({ type: v.type, value: v.value }, { type: u.type, value: u.value });
});

test('a nested Sigma refers to both binders and its payload is a Sigma', () => {
  assert.deepEqual(instances('def s : Sigma (a : Nat) (Sigma (b : Nat) (Eq Nat a b)) := pack 4 (pack 4 refl) '
    + 'def t : Sigma (b : Nat) (Eq Nat 4 b) := payload s'), [
    { name: 's', type: "Sigma (x : Nat) (Sigma (x' : Nat) (Eq (Nat) (x) (x')))", value: { first: 4, second: { first: 4, second: null } } },
    { name: 't', type: 'Sigma (x : Nat) (Eq (Nat) (4) (x))', value: { first: 4, second: null } },
  ]);
  assert.deepEqual(instances('def s : Sigma (n : Nat) (Sigma (t : Text) (Eq Text t "a")) := pack 1 (pack "a" refl)'), [
    { name: 's', type: "Sigma (x : Nat) (Sigma (x' : Text) (Eq (Text) (x') (\"a\")))", value: { first: 1, second: { first: 'a', second: null } } },
  ]);
});

test('binder names do not change the type', () => {
  const [s, t] = instances('def s : Sigma (n : Nat) (Eq Nat n 1200) := pack 1200 refl def t : Sigma (m : Nat) (Eq Nat m 1200) := s');
  assert.equal(t.type, s.type);
  assert.deepEqual(t.value, s.value);
});

test('Sigma equality carriers accept definitionally equal aliases', () => {
  assert.deepEqual(instances('def Count : Type 0 := Nat '
    + 'def s : Sigma (n : Option Count) (Eq (Option Nat) n n) := pack (some 1) refl'), [
    { name: 's', type: 'Sigma (x : Option (Nat)) (Eq (Option (Nat)) (x) (x))', value: { first: 1, second: null } },
  ]);
});

test('a nested equality checks the outer binder against its own type', () => {
  assert.deepEqual(instances('def s : Sigma (n : Nat) (Sigma (t : Text) (Eq Nat n n)) := pack 1 (pack "a" refl)'), [
    { name: 's', type: "Sigma (x : Nat) (Sigma (x' : Text) (Eq (Nat) (x) (x)))", value: { first: 1, second: { first: 'a', second: null } } },
  ]);
});

test('a function body can project a Sigma definition', () => {
  assert.deepEqual(instances('def s : Sigma (n : Nat) Text := pack 1 "a" '
    + 'def f : (k : Nat) -> Nat := fun (k : Nat) => witness s def m : Nat := f 3'), [
    { name: 's', type: 'Sigma (x : Nat) (Text)', value: { first: 1, second: 'a' } },
    { name: 'm', type: 'Nat', value: 1 },
  ]);
});

test('the sigma example compiles', async () => {
  const source = await readFile(new URL('../examples/sigma.lang', import.meta.url), 'utf8');
  assert.deepEqual(instances(source), [
    { name: 'price', type: 'Nat', value: 1200 },
    { name: 'offer', type: 'Sigma (x : Nat) (Eq (Nat) (x) (1200))', value: { first: 1200, second: null } },
    { name: 'amount', type: 'Nat', value: 1200 },
    { name: 'label', type: 'Sigma (x : Nat) (Text)', value: { first: 3, second: 'three' } },
    { name: 'name', type: 'Text', value: 'three' },
    { name: 'twice', type: "Sigma (x : Nat) (Sigma (x' : Nat) (Eq (Nat) (x) (x')))", value: { first: 4, second: { first: 4, second: null } } },
    { name: 'inner', type: 'Sigma (x : Nat) (Eq (Nat) (4) (x))', value: { first: 4, second: null } },
  ]);
});

const rejections = [
  ['a binder with the wrong Eq carrier', 'def s : Sigma (n : Nat) (Eq Text n n) := pack 1 refl', termError, 'n n'],
  ['a binder on the right with the wrong Eq carrier', 'def s : Sigma (n : Nat) (Eq Text "a" n) := pack 1 refl',
    termError, 'n)'],
  ['an outer binder with the wrong Eq carrier', 'def s : Sigma (a : Nat) (Sigma (b : Text) (Eq Text a a)) := pack 1 (pack "a" refl)',
    termError, 'a a'],
  ['an inner binder with the wrong Eq carrier', 'def s : Sigma (a : Nat) (Sigma (b : Text) (Eq Nat b b)) := pack 1 (pack "a" refl)',
    termError, 'b b'],
  ['a binder with a different type argument', 'def s : Sigma (n : Option Nat) (Eq (Option Text) n n) := pack (some 1) refl',
    termError, 'n n'],
  ['refl with a wrong witness', 'def s : Sigma (n : Nat) (Eq Nat n 1200) := pack 1201 refl', reflError, 'refl'],
  ['a nested refl with a wrong witness', 'def s : Sigma (a : Nat) (Sigma (b : Nat) (Eq Nat a b)) := pack 4 (pack 5 refl)', reflError, 'refl'],
  ['a payload of the wrong type', 'def s : Sigma (n : Nat) Text := pack 1 2', termError, '2'],
  ['a projection at the wrong type', 'def s : Sigma (n : Nat) Text := pack 1 "a" def t : Nat := payload s', termError, 'payload'],
  ['a reserved binder', 'def s : Sigma (refl : Nat) Text := pack 1 "a"', reservedError, 'refl'],
  ['a binder equal to a defined name', 'def price : Nat := 1 def s : Sigma (price : Nat) Text := pack 1 "a"', duplicateError, 'price'],
  ['a duplicate nested binder', 'def s : Sigma (a : Nat) (Sigma (a : Nat) (Eq Nat a a)) := pack 1 (pack 1 refl)',
    duplicateError, 'a : Nat) (Eq'],
  ['a universe as the witness type', 'def s : Sigma (a : Type 0) Text := pack Nat "a"', dataError, 'Type'],
  ['a universe as the payload type', 'def s : Sigma (n : Nat) (Type 0) := pack 1 Nat', dataError, '(Type'],
  ['the binder inside a larger Eq side', 'def s : Sigma (n : Nat) (Eq (Option Nat) (some n) (some 1)) := pack 1 refl',
    unknownError, 'n) (some 1)'],
  ['Sigma as a parameter type', 'def f : (s : Sigma (n : Nat) Text) -> Nat := fun (s : Nat) => 1', supportedError, 'Sigma'],
  ['Sigma as a function result', 'def f : (n : Nat) -> Sigma (m : Nat) Text := fun (n : Nat) => pack n "a"', supportedError, 'Sigma'],
  ['Sigma as a type definition body', 'def T : Type 0 := Sigma (n : Nat) Text', supportedError, 'Sigma'],
  ['Sigma as an Eq carrier', 'def p : Eq (Sigma (n : Nat) Text) 1 1 := refl', supportedError, 'Sigma'],
  ['Sigma inside a type former', 'def x : Option (Sigma (n : Nat) Text) := none', supportedError, 'Sigma'],
  ['pack against Nat', 'def n : Nat := pack 1 2', sigmaError, 'pack'],
  ['pack in a synthesized position', 'def n : Nat := witness (pack 1 2)', inferError, 'pack'],
  ['witness of a pair', 'def q : Prod Nat Text := pair 1 "a" def n : Nat := witness q', sigmaError, 'witness'],
  ['payload of a pair', 'def q : Prod Nat Text := pair 1 "a" def t : Text := payload q', sigmaError, 'payload'],
  ['first of a Sigma', 'def s : Sigma (n : Nat) Text := pack 1 "a" def n : Nat := first s', productError, 'first'],
  ['second of a Sigma', 'def s : Sigma (n : Nat) Text := pack 1 "a" def t : Text := second s', productError, 'second'],
];
for (const [label, source, message, at] of rejections) {
  test(`rejects ${label} at its byte`, () => reject(source, message, at));
}
