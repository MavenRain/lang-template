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
const two = 'def Two : Type 0 := (a : Nat) -> (b : Nat) -> Nat ';
const pick = 'def pick : (a : Nat) -> (b : Nat) -> Nat := fun (a : Nat) (b : Nat) => a ';
const latter = 'def latter : (a : Nat) -> (b : Nat) -> Nat := fun (a : Nat) (b : Nat) => b ';
const keep = 'def keep : (f : Flag) -> (n : Nat) -> Flag := fun (f : Flag) (n : Nat) => f ';
const twice = 'def twice : (k : Nat) -> (n : Nat) -> List Nat := ' +
  'fun (k : Nat) (n : Nat) => cons k (cons n nil) ';
const identity = 'def id : Rule := fun (n : Nat) => n ';
const via = 'def via : (f : Rule) -> (n : Nat) -> Nat := fun (f : Rule) (n : Nat) => f n ';
const xs = 'def xs : List Nat := cons 4 (cons 7 nil) ';
const prefix = rule + two + pick + latter + keep + twice + identity + via + xs;

test('structure forms take a partial application as their function', () => {
  assert.deepEqual(last(`${prefix}def result : List Nat := map (pick 1) xs`), [1, 1]);
  assert.deepEqual(last(`${prefix}def result : List Nat := map (latter 9) xs`), [4, 7]);
  assert.deepEqual(last(`${prefix}def result : List Nat := map ((pick 1)) xs`), [1, 1]);
  assert.deepEqual(last(`${prefix}def result : List Nat := map (id) xs`), [4, 7]);
  assert.deepEqual(last(`${prefix}def result : List Nat := map (pick (id 3)) xs`), [3, 3]);
  assert.deepEqual(last(`${prefix}def result : List Nat := filter (keep flagYes) xs`), [4, 7]);
  assert.deepEqual(last(`${prefix}def result : List Nat := filter (keep flagNo) xs`), []);
  assert.deepEqual(last(`${prefix}def result : List Nat := bind (twice 1) xs`), [1, 4, 1, 7]);
});

test('partial form functions bind values from the caller scope', () => {
  assert.deepEqual(last(prefix +
    'def choose : (k : Nat) -> List Nat := fun (k : Nat) => map (pick k) xs ' +
    'def result : List Nat := choose 3'), [3, 3]);
  assert.deepEqual(last(prefix +
    'def captured : Nat := 42 ' +
    'def target : (a : Nat) -> (b : Nat) -> Nat := fun (a : Nat) (b : Nat) => captured ' +
    'def forward : (captured : Nat) -> List Nat := fun (captured : Nat) => map (target captured) xs ' +
    'def result : List Nat := forward 9'), [42, 42]);
});

test('partial form functions bind function names and apply function parameters', () => {
  assert.deepEqual(last(`${prefix}def result : List Nat := map (via id) xs`), [4, 7]);
  assert.deepEqual(last(prefix +
    'def through : (f : Rule) -> List Nat := fun (f : Rule) => map (via f) xs ' +
    'def result : List Nat := through id'), [4, 7]);
  const useTwo = 'def useTwo : (h : Two) -> List Nat := fun (h : Two) => map (h 5) xs ';
  assert.deepEqual(last(`${prefix}${useTwo}def result : List Nat := useTwo pick`), [5, 5]);
  assert.deepEqual(last(`${prefix}${useTwo}def result : List Nat := useTwo latter`), [4, 7]);
});

test('partial form functions bind nested partial applications', () => {
  assert.deepEqual(last(`${prefix}def result : List Nat := map (via (pick 1)) xs`), [1, 1]);
});

test('partial form functions reject bad bound arguments', () => {
  const mistyped = `${prefix}def result : List Nat := map (pick "x") xs`;
  assert.deepEqual(run(mistyped), { error: {
    byte: mistyped.lastIndexOf('"x"'), message: 'term does not have the declared type',
  } });
  const nested = `${prefix}def result : List Nat := map (via (pick "x")) xs`;
  assert.deepEqual(run(nested), { error: {
    byte: nested.lastIndexOf('"x"'), message: 'term does not have the declared type',
  } });
  const polymorphic = prefix +
    'def generic : (A : Type 0) -> (a : A) -> (n : Nat) -> Nat := fun (A : Type 0) (a : A) (n : Nat) => n ' +
    'def result : List Nat := map (generic Nat 1) xs';
  assert.deepEqual(run(polymorphic).instances.at(-1).value, run(`${prefix}def result : List Nat := xs`).instances.at(-1).value);
  const data = `${prefix}def result : List Nat := map (xs 1) xs`;
  assert.deepEqual(run(data), { error: {
    byte: data.lastIndexOf('xs 1'), message: 'expected a function with one parameter',
  } });
  const extra = `${prefix}def result : List Nat := map (pick 1 2) xs`;
  assert.deepEqual(run(extra), { error: {
    byte: extra.lastIndexOf('2)'), message: 'expected closing parenthesis',
  } });
  const bare = `${prefix}def result : List Nat := map pick 1 xs`;
  assert.deepEqual(run(bare), { error: {
    byte: bare.lastIndexOf('pick'), message: 'expected a function with one parameter',
  } });
  for (const argument of ['(pick)', '(pick 1', '(1)', '()']) {
    assert.ok(run(`${prefix}def result : List Nat := map ${argument} xs`).error, argument);
  }
});

test('the form-partial example compiles', async () => {
  const source = await readFile(new URL('../examples/form-partial.lang', import.meta.url), 'utf8');
  const result = run(source);
  assert.equal(result['sample-lang'], 1, JSON.stringify(result));
  const values = result.instances.map(item => JSON.stringify(item.value));
  assert.ok(values.includes('[1,1]'), 'fixed');
  assert.ok(values.includes('[0,4,0,7]'), 'paired');
  assert.deepEqual(result.instances.at(-1).value, [5, 5]);
});
