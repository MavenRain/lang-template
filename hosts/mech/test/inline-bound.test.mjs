import assert from 'node:assert/strict';
import test from 'node:test';
import { readFile } from 'node:fs/promises';
import { compile, encoder, run, reject } from './helper.mjs';

const last = source => {
  const result = run(source);
  assert.equal(result['sample-lang'], 1, JSON.stringify(result));
  return result.instances.at(-1).value;
};
const refused = (source, message) => {
  const result = run(source);
  assert.ok(result.error, JSON.stringify(result));
  assert.match(JSON.stringify(result.error), message);
  assert.doesNotMatch(JSON.stringify(result.error), /supported type/);
};
const rule = 'def Rule : Type 0 := (n : Nat) -> Nat ';
const apply = 'def apply : (f : Rule) -> (n : Nat) -> Nat := fun (f : Rule) (n : Nat) => f n ';
const use = 'def use : (h : Rule) -> Nat := fun (h : Rule) => h 1 ';
const late = 'def late : (n : Nat) -> (f : Rule) -> (m : Nat) -> Nat := fun (n : Nat) (f : Rule) (m : Nat) => f n ';
const xs = 'def xs : List Nat := cons 4 (cons 7 nil) ';
const keep = 'def keepWith : (f : Rule) -> (x : Nat) -> (acc : Nat) -> Nat := fun (f : Rule) (x : Nat) (acc : Nat) => f x ';
const prefix = rule + apply + use + late + xs + keep;
const typeError = /term does not have the declared type/;

test('a partial application binds an inline function argument', () => {
  assert.equal(last(`${prefix}def n : Nat := use (apply (fun (x : Nat) => x))`), 1);
  assert.equal(last(`${prefix}def n : Nat := use (apply (fun (x : Nat) => 6))`), 6);
  assert.equal(last(`${prefix}def n : Nat := use (apply ((fun (x : Nat) => x)))`), 1);
  assert.equal(last(`${prefix}def n : Nat := use (late 3 (fun (x : Nat) => x))`), 3);
  assert.equal(last(`${prefix}def n : Nat := use (late 3 (fun (x : Nat) => 5))`), 5);
});

test('structure forms and folds take a partial application with an inline function', () => {
  assert.deepEqual(last(`${prefix}def ys : List Nat := map (apply (fun (x : Nat) => 2)) xs`), [2, 2]);
  assert.deepEqual(last(`${prefix}def ys : List Nat := map (apply (fun (x : Nat) => x)) xs`), [4, 7]);
  assert.equal(last(`${prefix}def n : Nat := fold (keepWith (fun (y : Nat) => y)) 0 xs`), 4);
  assert.equal(last(`${prefix}def n : Nat := fold (keepWith (fun (y : Nat) => 3)) 0 xs`), 3);
});

test('the inline function sees the scope of the partial application', () => {
  const tag = 'def tag : (k : Nat) -> Nat := fun (k : Nat) => use (apply (fun (n : Nat) => k)) ';
  assert.equal(last(`${prefix}${tag}def out : Nat := tag 9`), 9);
  const forward = 'def forward : (f : Rule) -> Nat := fun (f : Rule) => use (apply (fun (n : Nat) => f n)) ';
  assert.equal(last(`${prefix}${forward}def out : Nat := forward (fun (m : Nat) => m)`), 1);
  const spread = 'def spread : (k : Nat) -> List Nat := fun (k : Nat) => map (apply (fun (n : Nat) => k)) xs ';
  assert.deepEqual(last(`${prefix}${spread}def out : List Nat := spread 5`), [5, 5]);
  assert.equal(last(`${prefix}def x : Nat := 1 def out : Nat := use (late 8 (fun (x : Nat) => x))`), 8);
});

test('an inline bound argument must fit and is checked', () => {
  refused(`${prefix}def n : Nat := use (apply (fun (x : Text) => 0))`, typeError);
  refused(`${prefix}def n : Nat := use (apply (fun (x : Nat) (y : Nat) => x))`, typeError);
  refused(`${prefix}def n : Nat := use (apply fun (x : Nat) => x)`, /argument needs parentheses/);
  refused(`${prefix}def n : Nat := use (apply (fun (x : Nat) => "a"))`, /./);
  refused(`${prefix}def n : Nat := use (apply (fun (x : Nat) => y))`, /./);
  refused(`${prefix}def n : Nat := use (apply (apply (fun (x : Text) => 0)))`, typeError);
});

test('the inline bound argument example compiles', async () => {
  const source = await readFile(new URL('../examples/inline-bound.lang', import.meta.url), 'utf8');
  const result = run(source);
  assert.equal(result['sample-lang'], 1, JSON.stringify(result));
  assert.deepEqual(result.instances.map(item => item.value), [1, 9, [4, 7], [2, 2]]);
});
