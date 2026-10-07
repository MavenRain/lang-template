import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import test from 'node:test';
import { compile, encoder, run, reject } from './helper.mjs';

const instances = source => {
  const result = run(source);
  assert.equal(result['sample-lang'], 1, JSON.stringify(result));
  return result.instances;
};
const funError = 'expected fun with the declared parameters';
// Each case names the last occurrence of `at` in the source as the error byte.

const id = 'def id : (n : Nat) -> Nat := fun (n : Nat) => n ';
const double = 'def double : (n : Nat) -> Prod Nat Nat := fun (n : Nat) => pair n n ';

test('functions are not instances and applications evaluate their bodies', () => {
  assert.deepEqual(instances(`${double}def d : Prod Nat Nat := double 3`),
    [{ name: 'd', type: 'Prod (Nat) (Nat)', value: { first: 3, second: 3 } }]);
});

test('binders can be curried and the argument types are checked', () => {
  const mk = 'def mk : (a : Text) -> (b : Nat) -> Prod Text Nat := fun (a : Text) => fun (b : Nat) => pair a b ';
  assert.deepEqual(instances(`${mk}def x : Prod Text Nat := mk "k" 2`).map(item => item.value),
    [{ first: 'k', second: 2 }]);
  const flat = 'def mk : (a : Text) -> (b : Nat) -> Prod Text Nat := fun (a : Text) (b : Nat) => pair a b ';
  assert.deepEqual(instances(`${flat}def x : Prod Text Nat := mk "k" 2`).map(item => item.value),
    [{ first: 'k', second: 2 }]);
  reject(`${flat}def x : Prod Text Nat := mk 2 2`, 'term does not have the declared type', '2 2');
});

test('applications compose with projections, constructors and earlier instances', () => {
  const source = `${id}${double}def a : Nat := 4 def x : Nat := second (double a) ` +
    'def y : Option Nat := some (id (first (double 9))) def z : Nat := id (id 5)';
  assert.deepEqual(instances(source).map(item => item.value), [4, 4, 9, 5]);
});

test('equality sides can be closed applications', () => {
  assert.deepEqual(instances(`${id}def p : Eq Nat (id 2) 2 := refl def q : Nat := id 5`),
    [{ name: 'q', type: 'Nat', value: 5 }]);
  reject(`${id}def p : Eq Nat (id 2) 3 := refl`, 'refl needs two equal sides', 'refl');
});

test('constructor errors in a body surface at the application', () => {
  const byte = 'def b : (n : Nat) -> Text := fun (n : Nat) => textByte n textEnd ';
  assert.deepEqual(instances(`${byte}def t : Text := b 65`).map(item => item.value), ['A']);
  reject(`${byte}def t : Text := b 300`, 'textByte requires a byte below 256', 'textByte');
});

const rejected = [
  ['an application without parentheses as an argument', `${id}def x : Option Nat := some id 4`, 'argument needs parentheses', 'id 4'],
  ['an application with the wrong result type', `${id}def x : Text := id 3`, 'term does not have the declared type', 'id 3'],
  ['a binder with another type', 'def f : (n : Nat) -> Nat := fun (n : Text) => n', funError, '(n : Text)'],
  ['a function without fun', 'def f : (n : Nat) -> Nat := n', funError, 'n'],
  ['too few binders', 'def f : (a : Nat) -> (b : Nat) -> Nat := fun (a : Nat) => a', funError, '=>'],
  ['a reserved binder name', 'def f : (n : Nat) -> Nat := fun (none : Nat) => 1', 'reserved definition name', 'none'],
  ['a repeated binder name', 'def f : (a : Nat) -> (b : Nat) -> Nat := fun (a : Nat) (a : Nat) => a', 'duplicate definition name', 'a : Nat) =>'],
  ['a universe as the result type', 'def f : (n : Nat) -> Type 0 := fun (n : Nat) => Nat', 'expected a data type', 'Type 0'],
  ['Type 1 as a parameter type', 'def f : (t : Type 1) -> Nat := fun (t : Type 1) => 1', 'expected a data type', 'Type 1) ->'],
  ['a function as a type', `${id}def x : id := 1`, 'expected a supported type', 'id :='],
  ['a missing arrow', 'def f : (n : Nat) Nat := fun (n : Nat) => n', 'expected def NAME : TYPE := TERM', 'Nat :='],
  ['a lone minus sign', 'def x : Nat := 1 - 2', 'unexpected source byte', '-'],
];
for (const [label, source, message, at] of rejected) {
  test(`rejects ${label} at its byte`, () => reject(source, message, at));
}

test('functions example compiles', async () => {
  const source = await readFile(new URL('../examples/functions.lang', import.meta.url), 'utf8');
  assert.deepEqual(instances(source).map(item => [item.name, item.value]), [
    ['visits', { first: 'acme', second: 3 }],
    ['names', ['acme', 'acme']],
    ['total', 7],
  ]);
});

test('each fun must introduce a nonempty parameter group', () => {
  const first = 'def f : (n : Nat) -> Nat := fun => fun (n : Nat) => n';
  reject(first, funError, '=> fun');
  const middle = 'def f : (n : Nat) -> (m : Nat) -> Nat := ' +
    'fun (n : Nat) => fun => fun (m : Nat) => m';
  reject(middle, funError, '=> fun');
  const mixed = 'def f : (a : Nat) -> (b : Nat) -> (c : Nat) -> Prod Nat Nat := ' +
    'fun (a : Nat) => fun (b : Nat) (c : Nat) => pair a c ';
  assert.deepEqual(instances(`${mixed}def x : Prod Nat Nat := f 1 2 3`).map(item => item.value),
    [{ first: 1, second: 3 }]);
});

test('signature parameters shadow outer aliases in subsequent types', () => {
  const alias = 'def A : Type 0 := Nat ';
  reject(`${alias}def f : (A : Text) -> A := fun (x : Text) => 1`,
    'expected a supported type', 'A :=');
  reject(`${alias}def f : (A : Text) -> (b : A) -> Nat := fun (x : Text) (b : Nat) => b`,
    'expected a supported type', 'A) ->');
});

test('lambda parameters shadow outer aliases in subsequent annotations', () => {
  const signature = 'def A : Type 0 := Nat def f : (a : Text) -> (b : Nat) -> Nat := ';
  reject(`${signature}fun (A : Text) (b : A) => b`, 'expected a supported type', 'A) =>');
  reject(`${signature}fun (A : Text) => fun (b : A) => b`,
    'expected a supported type', 'A) =>');
});

test('parameters can shadow outer values without leaking into later definitions', () => {
  const source = 'def A : Type 0 := Nat def n : Nat := 1 ' +
    'def f : (n : A) -> A := fun (n : A) => n ' +
    'def x : Nat := f 3 def y : Nat := n';
  assert.deepEqual(instances(source).map(item => item.value), [1, 3, 1]);
});
