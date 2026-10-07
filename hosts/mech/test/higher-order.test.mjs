import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import test from 'node:test';
import { compile, encoder, run, reject } from './helper.mjs';

const last = source => {
  const result = run(source);
  assert.equal(result['sample-lang'], 1, JSON.stringify(result));
  return result.instances.at(-1).value;
};
const rule = 'def Rule : Type 0 := (n : Nat) -> Nat ';
const identity = `${rule}def id : Rule := fun (n : Nat) => n `;
const apply = 'def apply : (f : Rule) -> (n : Nat) -> Nat := fun (f : Rule) (n : Nat) => f n ';

test('function parameters accept equivalent signatures and grouped arguments', () => {
  const source = `${identity}${apply}` +
    'def Other : Type 0 := (other : Nat) -> Nat ' +
    'def other : Other := fun (x : Nat) => x ';
  assert.equal(last(`${source}def result : Nat := apply ((other)) 7`), 7);
});

test('parentheses group function parameter types in signatures and binders', () => {
  for (const declared of ['Rule', '(Rule)', '((Rule))']) {
    for (const binder of ['Rule', '(Rule)', '((Rule))']) {
      assert.equal(last(`${identity}def apply : (f : ${declared}) -> Nat := ` +
        `fun (f : ${binder}) => f 4 def result : Nat := apply id`), 4);
    }
  }
});

test('a function parameter can be forwarded to another function', () => {
  assert.equal(last(`${identity}${apply}` +
    'def forward : (f : Rule) -> (n : Nat) -> Nat := fun (f : Rule) (n : Nat) => apply f n ' +
    'def result : Nat := forward id 8'), 8);
});

test('nested function parameters bind their own function arguments', () => {
  assert.equal(last(`${identity}${apply}` +
    'def Apply : Type 0 := (f : Rule) -> (n : Nat) -> Nat ' +
    'def use : (a : Apply) -> (f : Rule) -> Nat := fun (a : Apply) (f : Rule) => a f 9 ' +
    'def result : Nat := use apply id'), 9);
});

test('captured values and functions survive caller shadowing and forwarding', () => {
  const source = `${rule}def captured : Nat := 42 ` +
    'def target : Rule := fun (n : Nat) => captured ' +
    'def wrapped : Rule := fun (n : Nat) => target n ' +
    'def other : Rule := fun (n : Nat) => 3 ' + apply +
    'def forward : (f : Rule) -> (target : Rule) -> (captured : Nat) -> Nat := ' +
    'fun (f : Rule) (target : Rule) (captured : Nat) => apply f captured ';
  assert.equal(last(`${source}def result : Nat := forward wrapped other 9`), 42);
});

test('map applies a captured function parameter', () => {
  assert.deepEqual(last(`${identity}def mapWith : (f : Rule) -> List Nat := ` +
    'fun (f : Rule) => map f (cons 4 (cons 7 nil)) ' +
    'def result : List Nat := mapWith id'), [4, 7]);
});

test('fold applies a captured function parameter with multiple arguments', () => {
  assert.deepEqual(last('def Reducer : Type 0 := (a : Nat) -> (b : List Nat) -> List Nat ' +
    'def pick : Reducer := fun (a : Nat) (b : List Nat) => cons a b ' +
    'def xs : List Nat := cons 4 (cons 7 nil) ' +
    'def run : (f : Reducer) -> List Nat := fun (f : Reducer) => fold f nil xs ' +
    'def result : List Nat := run pick'), [4, 7]);
});

test('function parameters coexist with renamed type parameters', () => {
  assert.deepEqual(last(`${identity}def use : (A : Type 0) -> (f : Rule) -> (a : A) -> Prod Nat A := ` +
    'fun (B : Type 0) (f : Rule) (b : B) => pair (f 4) b ' +
    'def result : Prod Nat Text := use Text id "hello"'), { first: 4, second: 'hello' });
});

test('function arguments reject mismatched signatures and unbound prefixes', () => {
  for (const [definition, message = 'term does not have the declared type'] of [
    ['def wrong : (s : Text) -> Nat := fun (s : Text) => 0 '],
    ['def wrong : (n : Nat) -> Text := fun (n : Nat) => "x" '],
    ['def wrong : (n : Nat) -> (m : Nat) -> Nat := fun (n : Nat) (m : Nat) => n ',
      'argument needs parentheses'],
    ['def wrong : Nat := 0 '],
    ['def wrong : (A : Type 0) -> (a : A) -> A := fun (A : Type 0) (a : A) => a '],
  ]) {
    const source = `${rule}${definition}${apply}def result : Nat := apply wrong 4`;
    assert.deepEqual(run(source), { error: {
      byte: source.lastIndexOf('wrong'), message,
    } });
  }
});

test('grouping preserves rejection of polymorphic function parameters', () => {
  for (const ty of ['Poly', '(Poly)', '((Poly))']) {
    const source = 'def Poly : Type 1 := (A : Type 0) -> (a : A) -> A ' +
      `def use : (f : ${ty}) -> Nat := fun (f : Nat) => 0`;
    assert.deepEqual(run(source), { error: {
      byte: source.lastIndexOf('Poly'), message: 'expected a data type',
    } });
  }
});

test('grouped function parameter types require every closing parenthesis', () => {
  for (const ty of ['(Rule', '((Rule)']) {
    const source = `${rule}def use : (f : ${ty}`;
    const result = run(source);
    assert.ok(result.error, source);
    assert.equal(result.error.byte, source.length, source);
  }
});

test('the higher-order example compiles with captured scopes', () => {
  const source = readFileSync(new URL('../examples/higher.lang', import.meta.url), 'utf8');
  const result = run(source);
  assert.equal(result['sample-lang'], 1, JSON.stringify(result));
  const values = Object.fromEntries(result.instances.map(instance => [instance.name, instance.value]));
  assert.deepEqual(values.paired, { first: 3, second: 3 });
  assert.deepEqual(values.captured, { first: 5, second: 10 });
  assert.deepEqual(values.pairs, [{ first: 4, second: 4 }, { first: 7, second: 7 }]);
  assert.deepEqual(values.items, [4, 7]);
});
