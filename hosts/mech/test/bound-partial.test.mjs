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
const pickK = 'def pickK : (k : Nat) -> (n : Nat) -> Nat := fun (k : Nat) (n : Nat) => k ';
const xs = 'def xs : List Nat := cons 4 (cons 7 nil) ';
const keep = 'def keepWith : (f : Rule) -> (x : Nat) -> (acc : Nat) -> Nat := fun (f : Rule) (x : Nat) (acc : Nat) => f x ';
const prefix = rule + apply + use + late + pickK + xs + keep;

test('a partial application binds a partial application', () => {
  assert.equal(last(`${prefix}def n : Nat := use (apply (pickK 6))`), 6);
  assert.equal(last(`${prefix}def n : Nat := use (apply (apply (fun (x : Nat) => x)))`), 1);
  assert.equal(last(`${prefix}def n : Nat := use (apply (apply (pickK 4)))`), 4);
  assert.equal(last(`${prefix}def n : Nat := use (late 3 (pickK 5))`), 5);
  assert.equal(last(`${prefix}def n : Nat := use (apply (late 2 (pickK 7)))`), 7);
});

test('structure forms and folds take a nested partial application', () => {
  assert.deepEqual(last(`${prefix}def ys : List Nat := map (apply (pickK 2)) xs`), [2, 2]);
  assert.deepEqual(last(`${prefix}def ys : List Nat := map (apply (apply (fun (x : Nat) => x))) xs`), [4, 7]);
  assert.equal(last(`${prefix}def n : Nat := fold (keepWith (pickK 3)) 0 xs`), 3);
  assert.equal(last(`${prefix}def n : Nat := fold (keepWith (apply (fun (y : Nat) => y))) 0 xs`), 4);
});

test('partial closures retain every argument beyond the former binding limit', () => {
  const names = Array.from({ length: 170 }, (_, index) => `p${index}`);
  const signature = names.map(name => `(${name} : Nat) -> `).join('');
  const binders = names.map(name => `(${name} : Nat)`).join(' ');
  const arguments_ = names.map(() => '7').join(' ');
  const wide = `def p169 : Nat := 99 def wide : ${signature}(n : Nat) -> Nat := ` +
    `fun ${binders} (n : Nat) => p169 `;
  assert.equal(last(`${prefix}${wide}def out : Nat := use (wide ${arguments_})`), 7);
  assert.deepEqual(last(`${prefix}${wide}def out : List Nat := map (wide ${arguments_}) xs`), [7, 7]);
  const step = `def wideStep : ${signature}(x : Nat) -> (acc : Nat) -> Nat := ` +
    `fun ${binders} (x : Nat) (acc : Nat) => p169 `;
  assert.equal(last(`${prefix}${wide}${step}def out : Nat := fold (wideStep ${arguments_}) 0 xs`), 7);
});

test('deeply nested partial closures preserve their innermost function', () => {
  const nested = `${'(apply '.repeat(60)}(pickK 9)${')'.repeat(60)}`;
  assert.equal(last(`${prefix}def out : Nat := use ${nested}`), 9);
  assert.deepEqual(last(`${prefix}def out : List Nat := map ${nested} xs`), [9, 9]);
  assert.equal(last(`${prefix}def out : Nat := fold (keepWith ${nested}) 0 xs`), 9);
});

test('a nested partial application sees the scope of the outer one', () => {
  const tag = 'def tag : (k : Nat) -> Nat := fun (k : Nat) => use (apply (pickK k)) ';
  assert.equal(last(`${prefix}${tag}def out : Nat := tag 9`), 9);
  const forward = 'def forward : (f : Rule) -> Nat := fun (f : Rule) => use (apply (apply f)) ';
  assert.equal(last(`${prefix}${forward}def out : Nat := forward (pickK 8)`), 8);
  const spread = 'def spread : (k : Nat) -> List Nat := fun (k : Nat) => map (apply (pickK k)) xs ';
  assert.deepEqual(last(`${prefix}${spread}def out : List Nat := spread 5`), [5, 5]);
  assert.equal(last(`${prefix}def x : Nat := 1 def out : Nat := use (late 8 (apply (fun (x : Nat) => x)))`), 8);
});

test('a nested bound partial application must fit and is checked', () => {
  refused(`${prefix}def n : Nat := use (apply (pickK "a"))`, /./);
  refused(`${prefix}def n : Nat := use (apply (pickK 1 2))`, /./);
  refused(`${prefix}def n : Nat := use (apply (apply pickK))`, /./);
  refused(`${prefix}def n : Nat := use (apply (late (pickK 1)))`, /./);
  refused(`${prefix}def n : Nat := use (apply (apply (fun (x : Text) => 0)))`, /./);
  const pick = 'def pick : (A : Type 0) -> (x : A) -> A := fun (A : Type 0) (x : A) => x ';
  assert.equal(last(`${prefix}${pick}def n : Nat := use (apply (pick Nat))`), 1);
});

test('the bound partial application example compiles', async () => {
  const source = await readFile(new URL('../examples/bound-partial.lang', import.meta.url), 'utf8');
  const result = run(source);
  assert.equal(result['sample-lang'], 1, JSON.stringify(result));
  assert.deepEqual(result.instances.map(item => item.value), [6, 1, 9, [4, 7], [2, 2]]);
});
