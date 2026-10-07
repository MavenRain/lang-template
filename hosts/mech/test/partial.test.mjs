import assert from 'node:assert/strict';
import test from 'node:test';
import { compile, encoder, run, reject } from './helper.mjs';

const last = source => {
  const result = run(source);
  assert.equal(result['sample-lang'], 1, JSON.stringify(result));
  return result.instances.at(-1).value;
};
const rule = 'def Rule : Type 0 := (n : Nat) -> Nat ';
const apply = 'def apply : (f : Rule) -> (n : Nat) -> Nat := fun (f : Rule) (n : Nat) => f n ';
const pick = 'def pick : (a : Nat) -> (b : Nat) -> Nat := fun (a : Nat) (b : Nat) => a ';
const identity = 'def id : Rule := fun (n : Nat) => n ';
const prefix = rule + apply + pick + identity;

test('partial function arguments bind the leading value parameters', () => {
  for (const argument of ['(pick 42)', '((pick 42))', '(pick (id 42))']) {
    assert.equal(last(`${prefix}def result : Nat := apply ${argument} 7`), 42);
  }
  assert.deepEqual(last(prefix +
    'def Tagged : Type 0 := (n : Nat) -> Prod Text Nat ' +
    'def tag : (s : Text) -> (a : Nat) -> (n : Nat) -> Prod Text Nat := ' +
    'fun (s : Text) (a : Nat) (n : Nat) => pair s a ' +
    'def use : (f : Tagged) -> Prod Text Nat := fun (f : Tagged) => f 0 ' +
    'def result : Prod Text Nat := use (tag "hello" 42)'), { first: 'hello', second: 42 });
});

test('partial functions capture prefix arguments in the caller scope', () => {
  assert.equal(last(prefix +
    'def a : Nat := 42 ' +
    'def forward : (a : Nat) -> Nat := fun (a : Nat) => apply (pick a) 7 ' +
    'def result : Nat := forward 9'), 9);
  assert.equal(last(prefix +
    'def captured : Nat := 42 ' +
    'def target : (a : Nat) -> (b : Nat) -> Nat := fun (a : Nat) (b : Nat) => captured ' +
    'def forward : (captured : Nat) -> Nat := fun (captured : Nat) => apply (target captured) 7 ' +
    'def result : Nat := forward 9'), 42);
  assert.equal(last(prefix +
    'def forward : (pick : Nat) -> Nat := fun (pick : Nat) => apply (apply id) pick ' +
    'def result : Nat := forward 9'), 9);
});

test('partial functions bind named function arguments before caller shadowing', () => {
  assert.equal(last(prefix +
    'def captured : Nat := 42 ' +
    'def target : Rule := fun (n : Nat) => captured ' +
    'def consume : (f : Rule) -> (target : Rule) -> (captured : Nat) -> Nat := ' +
    'fun (f : Rule) (target : Rule) (captured : Nat) => f captured ' +
    'def result : Nat := consume (apply target) id 9'), 42);
  assert.equal(last(`${prefix}def result : Nat := apply (apply ((id))) 7`), 7);
});

test('partial applications of function parameters preserve their captured scope', () => {
  assert.equal(last(prefix +
    'def Binary : Type 0 := (a : Nat) -> (b : Nat) -> Nat ' +
    'def use : (f : Binary) -> Nat := fun (f : Binary) => apply (f 42) 7 ' +
    'def result : Nat := use pick'), 42);
  assert.equal(last(prefix +
    'def Binary : Type 0 := (a : Nat) -> (b : Nat) -> Nat ' +
    'def tri : (a : Nat) -> (b : Nat) -> (c : Nat) -> Nat := fun (a : Nat) (b : Nat) (c : Nat) => a ' +
    'def use : (f : Binary) -> Nat := fun (f : Binary) => apply (f 7) 9 ' +
    'def result : Nat := use (tri 42)'), 42);
});

test('a bound function parameter may itself hold a partial application', () => {
  assert.equal(last(`${prefix}def result : Nat := apply (apply (pick 42)) 7`), 42);
  assert.equal(last(prefix +
    'def forward : (g : Rule) -> Nat := fun (g : Rule) => apply (apply g) 7 ' +
    'def result : Nat := forward (pick 42)'), 42);
});

test('structure forms apply partial functions through named parameters', () => {
  assert.deepEqual(last(prefix +
    'def use : (f : Rule) -> List Nat := fun (f : Rule) => map f (cons 1 (cons 2 nil)) ' +
    'def result : List Nat := use (pick 42)'), [42, 42]);
});

test('partial arguments retain signature and prefix type checks', () => {
  for (const argument of ['(pick "bad")', '(pick)', '(pick 1 2)', '(id 1)', '(pick 1',
    '(apply (pick "bad"))']) {
    const source = `${prefix}def result : Nat := apply ${argument} 7`;
    assert.ok(run(source).error, argument);
  }
  const source = `${prefix}def result : Nat := apply pick 42 7`;
  assert.deepEqual(run(source), { error: {
    byte: source.lastIndexOf('pick'), message: 'argument needs parentheses',
  } });
});

test('partial arguments reject incompatible suffixes', () => {
  for (const definition of [
    'def wrong : (a : Nat) -> (s : Text) -> Nat := fun (a : Nat) (s : Text) => a ',
    'def wrong : (a : Nat) -> (n : Nat) -> Text := fun (a : Nat) (n : Nat) => "x" ',
  ]) {
    const source = `${prefix}${definition}def result : Nat := apply (wrong 42) 7`;
    assert.deepEqual(run(source), { error: {
      byte: source.lastIndexOf('wrong'), message: 'term does not have the declared type',
    } });
  }
});
