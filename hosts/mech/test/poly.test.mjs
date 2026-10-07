import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import test from 'node:test';
import { compile, encoder, run, reject } from './helper.mjs';

const values = source => {
  const result = run(source);
  assert.equal(result['sample-lang'], 1, JSON.stringify(result));
  return result.instances.map(item => item.value);
};
// Each case names the last occurrence of `at` in the source as the error byte.
const typeError = 'term does not have the declared type';
const supportedError = 'expected a supported type';
const funError = 'expected fun with the declared parameters';
const firstError = 'type parameters must come first';
const universeError = 'type is not in the declared universe';

const id = 'def id : (A : Type 0) -> (x : A) -> A := fun (A : Type 0) (x : A) => x ';
const dup = 'def dup : (A : Type 0) -> (x : A) -> Prod A A := fun (A : Type 0) (x : A) => pair x x ';
const swap = 'def swap : (A : Type 0) -> (B : Type 0) -> (p : Prod A B) -> Prod B A := ' +
  'fun (A : Type 0) (B : Type 0) (p : Prod A B) => pair (second p) (first p) ';
const four = 'def four : (A : Type 0) -> Nat := fun (A : Type 0) => 4 ';
const endo = 'def Id : Type 1 := (A : Type 0) -> (x : A) -> A ';

test('a polymorphic function applies at each type argument', () => {
  const source = `${id}${dup}def a : Nat := id Nat 4 def b : Prod Nat Nat := dup Nat 7 ` +
    'def c : Prod Nat Nat := id (Prod Nat Nat) (pair 1 2)';
  assert.deepEqual(values(source), [4, { first: 7, second: 7 }, { first: 1, second: 2 }]);
});

// back gives its type parameters to swap in the other order. A substitution
// that replaces one name after the other gives `Prod A A` here.
test('two type arguments substitute in one step', () => {
  const back = 'def back : (B : Type 0) -> (A : Type 0) -> (p : Prod B A) -> Prod A B := ' +
    'fun (B : Type 0) (A : Type 0) (p : Prod B A) => swap B A p ';
  const source = `${swap}${back}def s : Prod Nat (Prod Nat Nat) := swap (Prod Nat Nat) Nat (pair (pair 1 2) 3) ` +
    'def t : Prod Nat (Prod Nat Nat) := back (Prod Nat Nat) Nat (pair (pair 5 6) 7)';
  assert.deepEqual(values(source), [
    { first: 3, second: { first: 1, second: 2 } },
    { first: 7, second: { first: 5, second: 6 } },
  ]);
});

test('a binder gives a new name to a type parameter', () => {
  const flat = 'def f : (A : Type 0) -> (x : A) -> Prod A A := fun (B : Type 0) (y : B) => pair y y ';
  const curried = 'def g : (A : Type 0) -> (x : A) -> A := fun (B : Type 0) => fun (y : B) => y ';
  const source = `${flat}${curried}def a : Prod Nat Nat := f Nat 5 def b : Nat := g Nat 6`;
  assert.deepEqual(values(source), [{ first: 5, second: 5 }, 6]);
});

test('a polymorphic body applies polymorphic functions', () => {
  const twice = 'def twice : (A : Type 0) -> (x : A) -> Prod A A := fun (A : Type 0) (x : A) => dup A (id A x) ';
  const source = `${id}${dup}${twice}def a : Prod Nat Nat := twice Nat 3 ` +
    'def b : Prod (Prod Nat Nat) (Prod Nat Nat) := twice (Prod Nat Nat) a';
  assert.deepEqual(values(source), [
    { first: 3, second: 3 },
    { first: { first: 3, second: 3 }, second: { first: 3, second: 3 } },
  ]);
});

test('a function type with a type parameter is in Type 1', () => {
  const source = `${endo}def same : Id := fun (B : Type 0) (y : B) => y def a : Nat := same Nat 6`;
  assert.deepEqual(values(source), [6]);
  reject('def Id : Type 0 := (A : Type 0) -> (x : A) -> A', universeError, '(A : Type 0)');
  reject('def F : Type 1 := (n : Nat) -> Nat', universeError, '(n : Nat)');
});

test('a function can have type parameters only', () => {
  assert.deepEqual(values(`${four}def a : Nat := four Text def b : Nat := four (Option Nat)`), [4, 4]);
});

const rejected = [
  ['a result at another type', `${id}def a : Text := id Nat 4`, typeError, 'id Nat 4'],
  ['an argument of another type', `${id}def a : Text := id Text 4`, typeError, '4'],
  ['a missing type argument', `${id}def a : Nat := id 4`, supportedError, '4'],
  ['a type former without parentheses as a type argument',
    `${id}def a : Prod Nat Nat := id Prod Nat Nat (pair 1 2)`, 'argument needs parentheses', 'Prod Nat Nat ('],
  ['a universe as a type argument', `${id}def a : Nat := id (Type 0) 4`, 'expected a data type', '(Type 0) 4'],
  ['a type parameter after a value parameter',
    'def f : (n : Nat) -> (A : Type 0) -> Nat := fun (n : Nat) (A : Type 0) => n', firstError, 'A : Type 0) ->'],
  ['a named type parameter after a value parameter',
    `${endo}def f : (n : Nat) -> Id := fun (n : Nat) (A : Type 0) (x : A) => x`, firstError, 'Id :='],
  ['a reserved name for a type parameter',
    'def f : (Nat : Type 0) -> Nat := fun (A : Type 0) => 1', 'reserved definition name', 'Nat : Type 0'],
  ['a value binder for a type parameter',
    'def f : (A : Type 0) -> (x : A) -> A := fun (n : Nat) (x : Nat) => x', funError, '(n : Nat)'],
  ['the declared name after a binder gives a new name',
    'def f : (A : Type 0) -> (x : A) -> A := fun (B : Type 0) (x : A) => x', supportedError, 'A) =>'],
  ['a constructor at the type of a type parameter',
    'def f : (A : Type 0) -> A := fun (A : Type 0) => 4', typeError, '4'],
  ['a term of another type parameter',
    'def f : (A : Type 0) -> (B : Type 0) -> (x : A) -> B := fun (A : Type 0) (B : Type 0) (x : A) => x',
    typeError, 'x'],
  ['a polymorphic function as the argument of map',
    `${four}def xs : Option Nat := map four (some 1)`, 'expected a function with one parameter', 'four (some'],
];
for (const [label, source, message, at] of rejected) {
  test(`rejects ${label} at its byte`, () => reject(source, message, at));
}

test('the example compiles', () => {
  const source = readFileSync(new URL('../examples/poly.lang', import.meta.url), 'utf8');
  assert.deepEqual(values(source), [
    4,
    { first: 4, second: 4 },
    { first: 9, second: { first: 4, second: 4 } },
  ]);
});
