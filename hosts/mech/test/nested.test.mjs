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
const budgetError = 'work budget exceeded';
const overBudget = source => {
  const result = run(source);
  assert.deepEqual(Object.keys(result), ['error'], JSON.stringify(result));
  assert.equal(result.error.message, budgetError);
};

const id = 'def id : (n : Nat) -> Nat := fun (n : Nat) => n ';
const double = 'def double : (n : Nat) -> Prod Nat Nat := fun (n : Nat) => pair n n ';
const both = 'def both : (n : Nat) -> Prod Nat Nat := fun (n : Nat) => double (id n) ';
// f0 is the identity. Each later function applies the previous function
// twice, so one application of fK evaluates 2^(K+1) - 1 bodies.
const link = k => k === 0 ? 'def f0 : (n : Nat) -> Nat := fun (n : Nat) => n '
  : `def f${k} : (n : Nat) -> Nat := fun (n : Nat) => f${k - 1} (f${k - 1} n) `;
const chain = depth => Array.from({ length: depth + 1 }, (_, k) => link(k)).join('');
const padTo = (source, bytes) => {
  assert.ok(source.length <= bytes, `the source has ${source.length} bytes`);
  return source + ' '.repeat(bytes - source.length);
};

test('a function body applies earlier functions', () => {
  const left = 'def left : (n : Nat) -> Nat := fun (n : Nat) => first (double n) ';
  const source = `${id}${double}${both}${left}def x : Prod Nat Nat := both 4 ` +
    'def y : Nat := first (both (second x)) def z : Nat := left 9';
  assert.deepEqual(values(source), [{ first: 4, second: 4 }, 4, 9]);
});

test('map applies a function whose body applies other functions', () => {
  const source = `${id}${double}${both}def xs : List (Prod Nat Nat) := map both (cons 1 (cons 2 nil))`;
  assert.deepEqual(values(source), [[{ first: 1, second: 1 }, { first: 2, second: 2 }]]);
});

test('a body evaluates in the scope of its definition', () => {
  const source = 'def k : Nat := 5 def g : (n : Nat) -> Nat := fun (n : Nat) => k ' +
    'def f : (k : Nat) -> Nat := fun (k : Nat) => g k def x : Nat := f 7';
  assert.deepEqual(values(source), [5, 5]);
  const inner = `${double}def h : (n : Nat) -> (m : Nat) -> Prod Nat Nat := fun (n : Nat) (m : Nat) => double m ` +
    'def y : Prod Nat Nat := h 1 2';
  assert.deepEqual(values(inner), [{ first: 2, second: 2 }]);
});

test('the check of a body checks an application by type', () => {
  const mismatch = 'term does not have the declared type';
  reject(`${id}def g : (n : Nat) -> Nat := fun (n : Nat) => id "a"`, mismatch, '"a"');
  reject(`${double}def g : (n : Nat) -> Nat := fun (n : Nat) => double n`, mismatch, 'double n');
  assert.deepEqual(Object.keys(run('def g : (n : Nat) -> Nat := fun (n : Nat) => g n')), ['error']);
  assert.deepEqual(Object.keys(run(`def g : (n : Nat) -> Nat := fun (n : Nat) => id n ${id}`)), ['error']);
});

test('constructor errors in a nested body surface at the application', () => {
  const byte = 'def b : (n : Nat) -> Text := fun (n : Nat) => textByte n textEnd ' +
    'def c : (n : Nat) -> Text := fun (n : Nat) => b n ';
  assert.deepEqual(values(`${byte}def t : Text := c 65`), ['A']);
  reject(`${byte}def t : Text := c 300`, 'textByte requires a byte below 256', 'textByte');
});

test('the work budget is 8 bodies for each source byte plus 64', () => {
  // id and f12 evaluate 1 + 8191 = 8192 bodies. 8 * 1016 + 64 = 8192.
  const source = `${id}${chain(12)}def x : Nat := id (f12 1)`;
  assert.deepEqual(values(padTo(source, 1016)), [1]);
  overBudget(padTo(source, 1015));
});

test('all definitions of a program share the work budget', () => {
  // Each application of f11 evaluates 4095 bodies. 8 * 1016 + 64 >= 8190.
  const one = `${chain(11)}def x : Nat := f11 1 `;
  const two = `${one}def y : Nat := f11 2`;
  assert.deepEqual(values(one), [1]);
  assert.deepEqual(values(padTo(two, 1016)), [1, 2]);
  overBudget(padTo(two, 1015));
});

test('a deep doubling chain stops at the work budget', () => {
  overBudget(`${chain(40)}def x : Nat := f40 1`);
});

test('the nested example compiles', () => {
  const source = readFileSync(new URL('../examples/nested.lang', import.meta.url), 'utf8');
  assert.deepEqual(values(source), [{ first: 4, second: 4 }, 9]);
});

test('the sides of an equality type use the work budget', () => {
  assert.deepEqual(values(`${chain(3)}def p : Eq Nat (f3 2) 2 := refl def q : Nat := f3 5`), [5]);
  overBudget(`${chain(40)}def p : Eq Nat (f40 1) 1 := refl`);
});
