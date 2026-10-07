import assert from 'node:assert/strict';
import test from 'node:test';
import { readFile } from 'node:fs/promises';
import { compile, encoder, run, reject } from './helper.mjs';

const last = source => {
  const result = run(source);
  assert.equal(result['sample-lang'], 1, JSON.stringify(result));
  return result.instances.at(-1).value;
};
const xs = 'def xs : List Nat := cons 4 (cons 7 nil) ';
const flags = 'def flags : List Flag := cons flagYes (cons flagNo (cons flagYes nil)) ';
const maybe = 'def maybe : Option Nat := some 5 ';
const pickK = 'def pickK : (k : Nat) -> (n : Nat) -> Nat := fun (k : Nat) (n : Nat) => k ';
const prefix = xs + flags + maybe + pickK;

test('structure forms take an inline function with one binder', () => {
  assert.deepEqual(last(`${prefix}def ys : List (List Nat) := map (fun (x : Nat) => cons x nil) xs`), [[4], [7]]);
  assert.deepEqual(last(`${prefix}def ys : List Nat := bind (fun (x : Nat) => cons x (cons x nil)) xs`), [4, 4, 7, 7]);
  assert.deepEqual(last(`${prefix}def kept : List Flag := filter (fun (b : Flag) => b) flags`), [true, true]);
  assert.deepEqual(last(`${prefix}def ys : Option (List Nat) := map (fun (x : Nat) => cons x nil) maybe`), [5]);
  assert.deepEqual(last(`${prefix}def ys : List Nat := map ((fun (x : Nat) => x)) xs`), [4, 7]);
});

test('an inline function body keeps the scope of the form', () => {
  const scoped = `${prefix}def dup : (k : Nat) -> (ys : List Nat) -> List Nat := ` +
    'fun (k : Nat) (ys : List Nat) => map (fun (y : Nat) => pickK k y) ys def out : List Nat := dup 9 xs';
  assert.deepEqual(last(scoped), [9, 9]);
  assert.deepEqual(last(`${prefix}def x : Nat := 1 def ys : List Nat := map (fun (x : Nat) => x) xs`), [4, 7]);
  const nested = `${prefix}def zs : List (List Nat) := map (fun (x : Nat) => map (fun (y : Nat) => pickK x y) xs) xs`;
  assert.deepEqual(last(nested), [[4, 4], [7, 7]]);
  const checkedOnly = `${prefix}def f : (ys : List Nat) -> List Nat := fun (ys : List Nat) => map (fun (y : Nat) => y) ys`;
  assert.equal(run(checkedOnly)['sample-lang'], 1);
});

test('an inline function is checked at the form', () => {
  const reserved = `${prefix}def ys : List Nat := map (fun (map : Nat) => map) xs`;
  assert.deepEqual(run(reserved), { error: {
    byte: reserved.lastIndexOf('map : Nat'), message: 'reserved definition name',
  } });
  const noArrow = `${prefix}def ys : List Nat := map (fun (x : Nat) x) xs`;
  assert.deepEqual(run(noArrow), { error: {
    byte: noArrow.lastIndexOf('x) xs'), message: 'expected fun with the declared parameters',
  } });
  const mistyped = `${prefix}def ys : List Nat := map (fun (x : Nat) => "s") xs`;
  assert.deepEqual(run(mistyped), { error: {
    byte: mistyped.lastIndexOf('"s"'), message: 'term does not have the declared type',
  } });
  const source = `${prefix}def ys : List Text := map (fun (t : Text) => t) xs`;
  assert.deepEqual(run(source), { error: {
    byte: source.lastIndexOf('xs'), message: 'term does not have the declared type',
  } });
  const filterParam = `${prefix}def ys : List Nat := filter (fun (t : Text) => flagYes) xs`;
  assert.deepEqual(run(filterParam), { error: {
    byte: filterParam.lastIndexOf('(fun'), message: 'term does not have the declared type',
  } });
  const filterBody = `${prefix}def ys : List Nat := filter (fun (x : Nat) => x) xs`;
  assert.deepEqual(run(filterBody), { error: {
    byte: filterBody.lastIndexOf('x) xs'), message: 'term does not have the declared type',
  } });
  const unknown = `${prefix}def ys : List Nat := map (fun (x : Nat) => q) xs`;
  assert.deepEqual(run(unknown), { error: {
    byte: unknown.lastIndexOf('q)'), message: 'unknown name or constructor',
  } });
});

test('an inline function needs parentheses, one data binder, and a structure form', () => {
  const bare = `${prefix}def ys : List Nat := map fun (x : Nat) => x xs`;
  assert.deepEqual(run(bare), { error: {
    byte: bare.lastIndexOf('fun'), message: 'expected a function with one parameter',
  } });
  const universe = `${prefix}def ys : List Nat := map (fun (A : Type 0) => 1) xs`;
  assert.deepEqual(run(universe), { error: {
    byte: universe.lastIndexOf('Type 0'), message: 'expected a data type',
  } });
  const folded = `${prefix}def n : Nat := fold (fun (x : Nat) => x) 0 xs`;
  assert.deepEqual(run(folded), { error: {
    byte: folded.lastIndexOf('(fun'), message: 'term does not have the declared type',
  } });
  assert.ok(run(`${prefix}def ys : List Nat := map (fun (x : Nat) (y : Nat) => x) xs`).error);
  assert.ok(run(`${prefix}def ys : List Nat := map (fun (f : (n : Nat) -> Nat) => 1) xs`).error);
  assert.ok(run(`${prefix}def ys : List Nat := map (fun (x : Nat) => x xs`).error);
  assert.ok(run(`${prefix}def ys : List Nat := map (fun => x) xs`).error);
});

test('the inline-fun example compiles', async () => {
  const source = await readFile(new URL('../examples/inline-fun.lang', import.meta.url), 'utf8');
  const result = run(source);
  assert.equal(result['sample-lang'], 1, JSON.stringify(result));
  const values = Object.fromEntries(result.instances.map(item => [item.name, item.value]));
  assert.deepEqual(values.wrapped, [[4], [7]]);
  assert.deepEqual(values.doubled, [4, 4, 7, 7]);
  assert.deepEqual(values.kept, [true, true]);
  assert.deepEqual(values.nines, [9, 9]);
});
