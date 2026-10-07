import assert from 'node:assert/strict';
import test from 'node:test';
import { readFile } from 'node:fs/promises';
import { compile, encoder, run, reject } from './helper.mjs';

const last = source => {
  const result = run(source);
  assert.equal(result['sample-lang'], 1, JSON.stringify(result));
  return result.instances.at(-1).value;
};
const rule = 'def Rule : Type 0 := (n : Nat) -> Nat ';
const apply = 'def apply : (f : Rule) -> (n : Nat) -> Nat := fun (f : Rule) (n : Nat) => f n ';
const use = 'def use : (h : Rule) -> Nat := fun (h : Rule) => h 1 ';
const konst = 'def konst : (A : Type 0) -> (a : A) -> (n : Nat) -> A := fun (A : Type 0) (a : A) (n : Nat) => a ';
const same = 'def same : (A : Type 0) -> (a : A) -> A := fun (A : Type 0) (a : A) => a ';
const xs = 'def xs : List Nat := cons 4 (cons 7 nil) ';
const keep = 'def keepWith : (f : Rule) -> (x : Nat) -> (acc : Nat) -> Nat := fun (f : Rule) (x : Nat) (acc : Nat) => f x ';
const prefix = rule + apply + use + konst + same + xs + keep;

test('a function argument partially applies a function with type parameters', () => {
  assert.equal(last(`${prefix}def n : Nat := use (konst Nat 6)`), 6);
  assert.equal(last(`${prefix}def n : Nat := use (same Nat)`), 1);
  assert.equal(last(`${prefix}def n : Nat := apply (konst Nat 3) 9`), 3);
  assert.equal(last(`${prefix}def n : Nat := apply (same Nat) 9`), 9);
});

test('the closure binds the type arguments for its body', () => {
  const second = 'def snd : (A : Type 0) -> (p : Prod A Nat) -> (n : Nat) -> Nat := ' +
    'fun (A : Type 0) (p : Prod A Nat) (n : Nat) => second p ';
  assert.equal(last(`${prefix}${second}def pr : Prod Text Nat := pair "x" 5 def n : Nat := use (snd Text pr)`), 5);
  const count = 'def countFrom : (A : Type 0) -> (ys : List A) -> (n : Nat) -> Nat := ' +
    'fun (A : Type 0) (ys : List A) (n : Nat) => fold (fun (y : A) (acc : Nat) => acc) n ys ';
  assert.equal(last(`${prefix}${count}def names : List Text := cons "a" (cons "b" nil) def n : Nat := use (countFrom Text names)`), 1);
});

test('type arguments and bound arguments resolve in the scope of the application', () => {
  const tag = 'def tag : (k : Nat) -> Nat := fun (k : Nat) => use (konst Nat k) ';
  assert.equal(last(`${prefix}${tag}def n : Nat := tag 9`), 9);
  const second = 'def snd : (A : Type 0) -> (p : Prod A Nat) -> (n : Nat) -> Nat := ' +
    'fun (A : Type 0) (p : Prod A Nat) (n : Nat) => second p ';
  const outer = 'def outer : (B : Type 0) -> (b : B) -> Nat := fun (B : Type 0) (b : B) => use (snd B (pair b 5)) ';
  assert.equal(last(`${prefix}${second}${outer}def n : Nat := outer Text "x"`), 5);
});

test('nested partial applications, structure forms and folds take type arguments', () => {
  assert.equal(last(`${prefix}def n : Nat := use (apply (konst Nat 5))`), 5);
  assert.deepEqual(last(`${prefix}def ys : List Nat := map (konst Nat 2) xs`), [2, 2]);
  assert.equal(last(`${prefix}def n : Nat := fold (keepWith (same Nat)) 0 xs`), 4);
  const pick = 'def pickA : (A : Type 0) -> (k : A) -> (x : Nat) -> (acc : A) -> A := ' +
    'fun (A : Type 0) (k : A) (x : Nat) (acc : A) => k ';
  assert.equal(last(`${prefix}${pick}def n : Nat := fold (pickA Nat 3) 0 xs`), 3);
});

test('partial applications with type parameters refuse wrong arguments', () => {
  const source = `${prefix}def n : Nat := apply (konst Text "x") 7`;
  assert.deepEqual(run(source), { error: {
    byte: source.lastIndexOf('konst'), message: 'term does not have the declared type',
  } });
  for (const term of ['use (konst 6)', 'use (konst Nat "x")', 'apply same 7', 'map (konst 2) xs']) {
    const result = run(`${prefix}def n : Nat := ${term}`);
    assert.ok(result.error, `${term}: ${JSON.stringify(result)}`);
  }
});

test('unfold takes a partial application with type parameters', () => {
  const step = 'def stepK : (A : Type 0) -> (k : A) -> (n : Nat) -> Option (Prod A Nat) := ' +
    'fun (A : Type 0) (k : A) (n : Nat) => some (pair k n) ';
  const wrap = 'def wrap : (A : Type 0) -> (a : A) -> Option (Prod A A) := fun (A : Type 0) (a : A) => some (pair a a) ';
  const count = 'def countA : (A : Type 0) -> (a : A) -> (n : Nat) -> Option Nat := ' +
    'fun (A : Type 0) (a : A) (n : Nat) => some n ';
  const steps = prefix + step + wrap + count;
  assert.deepEqual(last(`${steps}def ys : List Nat := unfold (stepK Nat 3) 2 5`), [3, 3]);
  assert.deepEqual(last(`${steps}def ys : List Text := unfold (stepK Text "a") 2 5`), ['a', 'a']);
  assert.deepEqual(last(`${steps}def ys : List Nat := unfold (wrap Nat) 2 5`), [5, 5]);
  assert.equal(last(`${steps}def c : Nat := unfold (countA Text "x") 3 0`), 3);
  const generic = 'def g : (B : Type 0) -> (b : B) -> List B := fun (B : Type 0) (b : B) => unfold (stepK B b) 2 5 ';
  assert.deepEqual(last(`${steps}${generic}def r : List Text := g Text "q"`), ['q', 'q']);
  const wrong = run(`${steps}def ys : List Nat := unfold (stepK Text "a") 2 5`);
  assert.equal(wrong.error?.message, 'term does not have the declared type', JSON.stringify(wrong));
});

test('a fold and an unfold over Value take partial applications with type parameters', () => {
  const kon = 'def kon : (A : Type 0) -> (B : Type 0) -> (a : A) -> (b : B) -> A := ' +
    'fun (A : Type 0) (B : Type 0) (a : A) (b : B) => a ';
  const rest = '(kon Nat Flag 1) (kon Nat Text 2) (kon Nat (List Nat) 3) (kon Nat (List (Prod Text Nat)) 4)';
  const folded = leaf => last(`${prefix}${kon}def v : Value := ${leaf} def n : Nat := fold (kon Nat Nat 7) ${rest} 0 v`);
  assert.equal(folded('valueNat 9'), 7);
  assert.equal(folded('valueText "a"'), 2);
  assert.equal(folded('valueItems (valuesItem (valueNat 1) valuesEnd)'), 3);
  assert.equal(folded('valueAttrs (attrsField "n" (valueNat 1) attrsEnd)'), 4);
  assert.equal(folded('valueNull'), 0);
  assert.ok(run(`${prefix}${kon}def v : Value := valueNat 9 def n : Nat := fold (kon Nat Text 7) ${rest} 0 v`).error);
  const layer = A => `Option (Sum Nat (Sum Flag (Sum Text (Sum (List ${A}) (List (Prod Text ${A}))))))`;
  const grow = `def growA : (A : Type 0) -> (s : A) -> ${layer('A')} := ` +
    'fun (A : Type 0) (s : A) => some (inr (inr (inr (inr (cons (pair "left" s) nil))))) ';
  const leafK = `def leafK : (A : Type 0) -> (a : A) -> (k : Nat) -> (s : Text) -> ${layer('Text')} := ` +
    'fun (A : Type 0) (a : A) (k : Nat) (s : Text) => some (inl k) ';
  assert.deepEqual(last(`${prefix}${grow}def v : Value := unfold (growA Text) 2 "s"`), { left: { left: null } });
  assert.equal(last(`${prefix}${leafK}def v : Value := unfold (leafK Text "x" 4) 2 "s"`), 4);
  assert.ok(run(`${prefix}${grow}def v : Value := unfold (growA Nat) 2 "s"`).error);
});

test('a partial application with type parameters binds another one', () => {
  const applyA = 'def applyA : (A : Type 0) -> (f : Rule) -> (a : A) -> (n : Nat) -> Nat := ' +
    'fun (A : Type 0) (f : Rule) (a : A) (n : Nat) => f n ';
  const second = 'def snd : (A : Type 0) -> (p : Prod A Nat) -> (n : Nat) -> Nat := ' +
    'fun (A : Type 0) (p : Prod A Nat) (n : Nat) => second p ';
  const base = prefix + applyA + second;
  assert.equal(last(`${base}def n : Nat := use (applyA Text (konst Nat 4) "x")`), 4);
  const outer = body => 'def outer : (B : Type 0) -> (b : B) -> Nat := ' +
    `fun (B : Type 0) (b : B) => ${body} def n : Nat := outer Text "x"`;
  assert.equal(last(base + outer('use (applyA B (konst Nat 8) b)')), 8);
  assert.equal(last(base + outer('use (applyA B (snd B (pair b 6)) b)')), 6);
  for (const term of ['use (applyA Text (konst Text "y") "x")', 'use (applyA Text (konst Nat 4) 5)']) {
    const result = run(`${base}def n : Nat := ${term}`);
    assert.ok(result.error, `${term}: ${JSON.stringify(result)}`);
  }
});

test('the example compiles', async () => {
  const source = await readFile(new URL('../examples/poly-partial.lang', import.meta.url), 'utf8');
  const result = run(source);
  assert.equal(result['sample-lang'], 1, JSON.stringify(result));
  assert.deepEqual(result.instances.map(item => item.value), [6, 1, 9, [4, 7], [2, 2]]);
});
