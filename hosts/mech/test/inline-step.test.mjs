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
const count = 'def count : Nat := 3 ';
const pickK = 'def pickK : (k : Nat) -> (n : Nat) -> Nat := fun (k : Nat) (n : Nat) => k ';
const natId = 'def natId : (n : Nat) -> Nat := fun (n : Nat) => n ';
const leaf = 'def leaf : Value := valueNat 6 ';
const prefix = xs + count + pickK + natId + leaf;
const rest = '(fun (b : Flag) => 0) (fun (t : Text) => 0) (fun (ns : List Nat) => 0) ' +
  '(fun (fs : List (Prod Text Nat)) => 0)';

test('fold takes an inline function', () => {
  assert.equal(last(`${prefix}def n : Nat := fold (fun (x : Nat) (acc : Nat) => x) 0 xs`), 4);
  assert.equal(last(`${prefix}def n : Nat := fold (fun (x : Nat) (acc : Nat) => acc) 0 xs`), 0);
  assert.deepEqual(last(`${prefix}def ys : List Nat := fold (fun (x : Nat) (acc : List Nat) => cons x acc) nil xs`), [4, 7]);
  assert.deepEqual(last(`${prefix}def ys : List Nat := fold (fun (acc : List Nat) => cons 1 acc) nil count`), [1, 1, 1]);
  assert.equal(last(`${prefix}def n : Nat := fold ((fun (x : Nat) (acc : Nat) => pickK x acc)) 0 xs`), 4);
  const scoped = `${prefix}def tag : (k : Nat) -> (ys : List Nat) -> List Nat := ` +
    'fun (k : Nat) (ys : List Nat) => fold (fun (y : Nat) (acc : List Nat) => cons k acc) nil ys ' +
    'def out : List Nat := tag 9 xs';
  assert.deepEqual(last(scoped), [9, 9]);
  const nested = `${prefix}def ys : List (List Nat) := ` +
    'fold (fun (x : Nat) (acc : List (List Nat)) => cons (map (fun (y : Nat) => x) xs) acc) nil xs';
  assert.deepEqual(last(nested), [[4, 4], [7, 7]]);
  assert.equal(last(`${prefix}def x : Nat := 1 def n : Nat := fold (fun (x : Nat) (acc : Nat) => x) 0 xs`), 4);
});

test('unfold takes an inline function', () => {
  assert.deepEqual(last(`${prefix}def ys : List Nat := unfold (fun (n : Nat) => some (pair n n)) 2 5`), [5, 5]);
  assert.deepEqual(last(`${prefix}def ys : List Text := unfold (fun (t : Text) => some (pair t t)) 2 "a"`), ['a', 'a']);
  assert.deepEqual(last(`${prefix}def ys : List Nat := unfold (fun (n : Nat) => none) 2 5`), []);
  assert.equal(last(`${prefix}def v : Value := unfold (fun (n : Nat) => none) 3 7`), null);
  assert.ok(run(`${prefix}def ys : List Nat := unfold (fun (n : Nat) => some n) 2 5`).error);
  assert.ok(run(`${prefix}def v : Value := unfold (fun (n : Nat) => some n) 3 7`).error);
});

test('each function of a fold over Value can be inline', () => {
  assert.equal(last(`${prefix}def n : Nat := fold (fun (n : Nat) => n) ${rest} 0 leaf`), 6);
  assert.equal(last(`${prefix}def n : Nat := fold natId ${rest} 0 leaf`), 6);
  assert.equal(last(`${prefix}def v : Value := valueNull def n : Nat := fold natId ${rest} 1 v`), 1);
  const flag = `${prefix}def v : Value := valueFlag flagYes def n : Nat := fold natId (fun (b : Flag) => 8) ` +
    '(fun (t : Text) => 0) (fun (ns : List Nat) => 0) (fun (fs : List (Prod Text Nat)) => 0) 0 v';
  assert.equal(last(flag), 8);
  assert.ok(run(`${prefix}def n : Nat := fold (fun (n : Text) => 0) ${rest} 0 leaf`).error);
});

test('an inline function of fold is checked at the form', () => {
  const unary = `${prefix}def n : Nat := fold (fun (x : Nat) => x) 0 xs`;
  assert.deepEqual(run(unary), { error: {
    byte: unary.lastIndexOf('(fun'), message: 'term does not have the declared type',
  } });
  const mistyped = `${prefix}def n : Nat := fold (fun (x : Nat) (acc : Nat) => "s") 0 xs`;
  assert.deepEqual(run(mistyped), { error: {
    byte: mistyped.lastIndexOf('"s"'), message: 'term does not have the declared type',
  } });
  const source = `${prefix}def n : Nat := fold (fun (x : Text) (acc : Nat) => acc) 0 xs`;
  assert.deepEqual(run(source), { error: {
    byte: source.lastIndexOf('(fun'), message: 'term does not have the declared type',
  } });
  const reserved = `${prefix}def n : Nat := fold (fun (fold : Nat) (acc : Nat) => acc) 0 xs`;
  assert.deepEqual(run(reserved), { error: {
    byte: reserved.lastIndexOf('fold : Nat'), message: 'reserved definition name',
  } });
  const universe = `${prefix}def n : Nat := fold (fun (A : Type 0) (acc : Nat) => acc) 0 xs`;
  assert.deepEqual(run(universe), { error: {
    byte: universe.lastIndexOf('Type 0'), message: 'expected a data type',
  } });
  const noArrow = `${prefix}def n : Nat := fold (fun (x : Nat) (acc : Nat) acc) 0 xs`;
  assert.deepEqual(run(noArrow), { error: {
    byte: noArrow.lastIndexOf('acc) 0'), message: 'expected fun with the declared parameters',
  } });
  assert.ok(run(`${prefix}def n : Nat := fold (fun => 0) 0 xs`).error);
  assert.ok(run(`${prefix}def n : Nat := fold (fun (x : Nat) (acc : Nat) => acc 0 xs`).error);
  assert.ok(run(`${prefix}def n : Nat := fold (fun (x : Nat) (acc : Nat) => y) 0 xs`).error);
});

test('inline step binders reject repeated names', () => {
  for (const source of [
    `${xs}def n : Nat := fold (fun (x : Nat) (x : Nat) => x) 0 xs`,
    'def xs : List Nat := nil def ys : List Nat := ' +
      'fold (fun (x : Nat) (x : List Nat) => cons x nil) nil xs',
  ]) {
    assert.deepEqual(run(source), { error: {
      byte: source.lastIndexOf('(x :') + 1, message: 'duplicate definition name',
    } });
  }
});

test('inline step binders shadow outer names in subsequent annotations', () => {
  const alias = `def A : Type 0 := Nat ${xs}def n : Nat := ` +
    'fold (fun (A : Nat) (acc : A) => acc) 0 xs';
  assert.deepEqual(run(alias), { error: {
    byte: alias.lastIndexOf('A) =>'), message: 'expected a supported type',
  } });
  const scoped = `def A : Type 0 := Nat ${xs}def n : Nat := ` +
    'fold (fun (A : A) (acc : Nat) => A) 0 xs def after : A := 2';
  assert.deepEqual(run(scoped).instances.map(item => item.value), [[4, 7], 4, 2]);
});

test('the inline-step example compiles', async () => {
  const source = await readFile(new URL('../examples/inline-step.lang', import.meta.url), 'utf8');
  const result = run(source);
  assert.equal(result['sample-lang'], 1, JSON.stringify(result));
  const values = Object.fromEntries(result.instances.map(item => [item.name, item.value]));
  assert.deepEqual(values.copied, [4, 7]);
  assert.deepEqual(values.ones, [1, 1, 1]);
  assert.deepEqual(values.fives, [5, 5]);
  assert.equal(values.six, 6);
});
