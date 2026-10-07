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
const both = 'def Pick : Type 0 := (a : Nat) -> (b : Nat) -> Nat ' +
  'def both : (g : Pick) -> Nat := fun (g : Pick) => g 3 4 ';
const xs = 'def xs : List Nat := cons 4 (cons 7 nil) ';
const wrap = 'def Spread : Type 0 := (n : Nat) -> List Nat ' +
  'def wrap : (f : Spread) -> List Nat := fun (f : Spread) => f 7 ';
const prefix = rule + apply + both + xs + wrap;
const typeError = /term does not have the declared type/;

test('a function takes an inline function argument', () => {
  assert.equal(last(`${prefix}def n : Nat := apply (fun (x : Nat) => x) 5`), 5);
  assert.equal(last(`${prefix}def n : Nat := apply (fun (x : Nat) => 2) 5`), 2);
  assert.equal(last(`${prefix}def n : Nat := apply ((fun (x : Nat) => x)) 6`), 6);
  assert.equal(last(`${prefix}def n : Nat := both (fun (a : Nat) (b : Nat) => b)`), 4);
  assert.equal(last(`${prefix}def n : Nat := both (fun (a : Nat) (b : Nat) => a)`), 3);
  assert.deepEqual(last(`${prefix}def ys : List Nat := wrap (fun (n : Nat) => cons n (cons n nil))`), [7, 7]);
});

test('the body of an inline argument sees the scope of the application', () => {
  const tag = 'def tag : (k : Nat) -> Nat := fun (k : Nat) => apply (fun (n : Nat) => k) 0 ';
  assert.equal(last(`${prefix}${tag}def out : Nat := tag 9`), 9);
  assert.equal(last(`${prefix}def nine : Nat := 9 def out : Nat := apply (fun (n : Nat) => nine) 1`), 9);
  assert.equal(last(`${prefix}def x : Nat := 1 def out : Nat := apply (fun (x : Nat) => x) 5`), 5);
  const forward = 'def forward : (f : Rule) -> Nat := fun (f : Rule) => apply (fun (n : Nat) => f n) 4 ';
  assert.equal(last(`${prefix}${forward}def out : Nat := forward (fun (m : Nat) => m)`), 4);
  assert.equal(last(`${prefix}def out : Nat := apply (fun (x : Nat) => apply (fun (y : Nat) => x) 1) 8`), 8);
  assert.deepEqual(last(`${prefix}def ys : List Nat := wrap (fun (n : Nat) => map (fun (y : Nat) => n) xs)`), [7, 7]);
});

test('an inline argument must fit the parameter type', () => {
  refused(`${prefix}def n : Nat := apply fun (x : Nat) => x 5`, /argument needs parentheses/);
  refused(`${prefix}def n : Nat := apply (fun (x : Text) => 0) 5`, typeError);
  refused(`${prefix}def n : Nat := apply (fun (x : Nat) (y : Nat) => x) 5`, typeError);
  refused(`${prefix}def n : Nat := both (fun (a : Nat) => a)`, typeError);
  refused(`${prefix}def n : Nat := (fun (x : Nat) => x)`, /./);
  const use = 'def use : (h : Rule) -> Nat := fun (h : Rule) => h 1 ';
  refused(`${prefix}${use}def n : Nat := use (apply (apply (fun (x : Text) => 0)))`, typeError);
});

test('the body of an inline argument is checked at the argument', () => {
  refused(`${prefix}def n : Nat := apply (fun (x : Nat) => "a") 5`, /./);
  refused(`${prefix}def n : Nat := apply (fun (x : Nat) x) 5`, /./);
  refused(`${prefix}def n : Nat := apply (fun (fun : Nat) => 0) 5`, /./);
  refused(`${prefix}def n : Nat := apply (fun (x : Nat) => y) 5`, /./);
  refused(`${prefix}def n : Nat := apply (fun (x : Nat) => x 5`, /./);
  refused(`${prefix}def f : (k : Text) -> Nat := fun (k : Text) => apply (fun (x : Nat) => k) 1`, /./);
});

test('the inline argument example compiles', async () => {
  const source = await readFile(new URL('../examples/inline-argument.lang', import.meta.url), 'utf8');
  const result = run(source);
  assert.equal(result['sample-lang'], 1, JSON.stringify(result));
  assert.deepEqual(result.instances.map(item => item.value), [5, 9, 4, [4, 7], [7, 7]]);
});
