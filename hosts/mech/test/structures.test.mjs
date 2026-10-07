import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import test from 'node:test';
import { compile, encoder, run, reject } from './helper.mjs';

const values = source => {
  const result = run(source);
  assert.equal(result['sample-lang'], 1, JSON.stringify(result));
  return result.instances.map(item => item.value);
};
const structureError = 'the type has no instance of this structure';
const unaryError = 'expected a function with one parameter';
const typeError = 'term does not have the declared type';
// Each case names the last occurrence of `at` in the source as the error byte.

const fns = 'def tag : (n : Nat) -> Prod Nat Nat := fun (n : Nat) => pair n 0 ' +
  'def twice : (n : Nat) -> List Nat := fun (n : Nat) => cons n (cons n nil) ' +
  'def wrap : (n : Nat) -> Option Nat := fun (n : Nat) => some n ' +
  'def yes : (n : Nat) -> Flag := fun (n : Nat) => flagYes ' +
  'def no : (n : Nat) -> Flag := fun (n : Nat) => flagNo ' +
  'def len : (t : Text) -> Nat := fun (t : Text) => 7 ' +
  'def id : (n : Nat) -> Nat := fun (n : Nat) => n ' +
  'def add : (a : Nat) -> (b : Nat) -> Nat := fun (a : Nat) (b : Nat) => a ';

test('pure lifts a value into Option, List and Sum E', () => {
  assert.deepEqual(values('def a : Option Nat := pure 3 def b : List Text := pure "x" ' +
    'def c : Sum Text Nat := pure 4 def d : Option (Option Nat) := pure none'),
  [3, ['x'], { inr: 4 }, { some: null }]);
});

test('map applies a function to each payload', () => {
  assert.deepEqual(values(`${fns}def xs : List Nat := cons 1 (cons 2 nil) ` +
    'def a : List (Prod Nat Nat) := map tag xs def b : Option (Prod Nat Nat) := map tag (some 5) ' +
    'def c : Option (Prod Nat Nat) := map tag none def d : Sum Text (Prod Nat Nat) := map tag (inr 9) ' +
    'def e : Sum Text (Prod Nat Nat) := map tag (inl "e")'),
  [[1, 2], [{ first: 1, second: 0 }, { first: 2, second: 0 }], { first: 5, second: 0 }, null,
    { inr: { first: 9, second: 0 } }, { inl: 'e' }]);
});

test('map over Option encodes some again for the result type', () => {
  const opt = 'def opt : (n : Nat) -> Option Nat := fun (n : Nat) => none ';
  assert.deepEqual(values(`${opt}def a : Option (Option Nat) := map opt (some 1)`), [{ some: null }]);
});

test('bind joins lists and passes none and inl through', () => {
  const left = 'def left : (n : Nat) -> Sum Text Nat := fun (n : Nat) => inl "stop" ';
  assert.deepEqual(values(`${fns}${left}def a : List Nat := bind twice (cons 1 (cons 2 nil)) ` +
    'def b : Option Nat := bind wrap (some 4) def c : Option Nat := bind wrap none ' +
    'def d : Sum Text Nat := bind left (inr 3) def e : Sum Text Nat := bind left (inl "x") ' +
    'def f : List Nat := bind twice nil'),
  [[1, 1, 2, 2], 4, null, { inl: 'stop' }, { inl: 'x' }, []]);
});

test('filter keeps the payloads with the result flagYes', () => {
  assert.deepEqual(values(`${fns}def xs : List Nat := cons 1 (cons 2 nil) ` +
    'def a : List Nat := filter yes xs def b : List Nat := filter no xs ' +
    'def c : Option Nat := filter yes (some 1) def d : Option Nat := filter no (some 1)'),
  [[1, 2], [1, 2], [], 1, null]);
});

test('either synthesizes the shared result type', () => {
  assert.deepEqual(values(`${fns}def a : Nat := either len id (inl "k") ` +
    'def b : Nat := either len id (inr 4) def c : Option Nat := some (either len id (inr 6))'),
  [7, 4, 6]);
});

test('structure forms compose with applications and nest', () => {
  assert.deepEqual(values(`${fns}def a : List Nat := map id (bind twice (pure 3)) ` +
    'def b : List Nat := filter yes (map id (cons 5 nil))'),
  [[3, 3], [5]]);
});

test('a declared type without the structure is refused', () => {
  reject(`${fns}def x : Nat := pure 3`, structureError, 'pure');
  reject(`${fns}def x : Prod Nat Nat := map id (pair 1 2)`, structureError, 'map');
  reject(`${fns}def x : Sum Nat Nat := filter yes (inr 3)`, structureError, 'filter');
});

test('the function argument must name a function with one parameter', () => {
  reject(`${fns}def x : List Nat := map 3 nil`, unaryError, '3 nil');
  reject(`${fns}def x : List Nat := map add nil`, unaryError, 'add nil');
  reject(`${fns}def k : Nat := 1 def x : List Nat := map k nil`, unaryError, 'k nil');
  reject(`${fns}def x : Nat := either len add (inr 1)`, unaryError, 'add (inr');
});

test('the function types must match the declared type', () => {
  reject(`${fns}def x : List Text := map id nil`, typeError, 'id nil');
  reject(`${fns}def x : List Nat := bind wrap nil`, typeError, 'wrap nil');
  reject(`${fns}def x : List Nat := filter id nil`, typeError, 'id nil');
  reject(`${fns}def x : Nat := either len tag (inr 1)`, typeError, 'tag (inr');
  reject(`${fns}def x : List Nat := map id (cons "a" nil)`, typeError, '"a"');
});

test('structure forms need parentheses as arguments', () => {
  reject(`${fns}def x : Option (List Nat) := some pure 3`, 'argument needs parentheses', 'pure');
  reject(`${fns}def x : Option Nat := some either len id (inr 1)`, 'argument needs parentheses', 'either');
});

test('a function body can use pure and the forms that apply a function', () => {
  assert.deepEqual(values(`${fns}def one : (n : Nat) -> List Nat := fun (n : Nat) => pure n ` +
    'def x : List Nat := one 8'), [[8]]);
  assert.deepEqual(values(`${fns}def h : (n : Nat) -> List Nat := fun (n : Nat) => map id (cons n nil) ` +
    'def y : List Nat := h 8'), [[8]]);
  assert.deepEqual(values(`${fns}def h : (n : Nat) -> Nat := fun (n : Nat) => either len id (inr n) ` +
    'def y : Nat := h 8'), [8]);
});

test('the structures example compiles', async () => {
  const source = await readFile(new URL('../examples/structures.lang', import.meta.url), 'utf8');
  const result = run(source);
  assert.equal(result['sample-lang'], 1, JSON.stringify(result));
  assert.deepEqual(result.instances.map(item => [item.name, item.value]), [
    ['counts', [1, 2]], ['single', [7]],
    ['pairs', [{ first: 'seen', second: 1 }, { first: 'seen', second: 2 }]],
    ['doubled', [1, 1, 2, 2]], ['empty', []], ['every', [1, 2]],
    ['found', { first: 'seen', second: 4 }], ['failed', { inl: 'no stock' }], ['settled', 5],
  ]);
});
