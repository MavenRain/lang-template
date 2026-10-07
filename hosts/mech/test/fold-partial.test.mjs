import assert from 'node:assert/strict';
import test from 'node:test';
import { readFile } from 'node:fs/promises';
import { compile, encoder, run, reject } from './helper.mjs';

const last = source => {
  const result = run(source);
  assert.equal(result['sample-lang'], 1, JSON.stringify(result));
  return result.instances.at(-1).value;
};
const three = 'def Three : Type 0 := (k : Nat) -> (x : Nat) -> (acc : Nat) -> Nat ';
const pick = 'def pick : (a : Nat) -> (b : Nat) -> Nat := fun (a : Nat) (b : Nat) => a ';
const pick3 = 'def pick3 : Three := fun (k : Nat) (x : Nat) (acc : Nat) => k ';
const takeX = 'def takeX : Three := fun (k : Nat) (x : Nat) (acc : Nat) => x ';
const keepAcc = 'def keepAcc : Three := fun (k : Nat) (x : Nat) (acc : Nat) => acc ';
const headOf = 'def headOf : (x : Nat) -> (acc : Nat) -> Nat := fun (x : Nat) (acc : Nat) => x ';
const consK = 'def consK : (k : Nat) -> (x : Nat) -> (acc : List Nat) -> List Nat := ' +
  'fun (k : Nat) (x : Nat) (acc : List Nat) => cons k acc ';
const consTwo = 'def consTwo : (a : Nat) -> (b : Nat) -> (x : Nat) -> (acc : List Nat) -> List Nat := ' +
  'fun (a : Nat) (b : Nat) (x : Nat) (acc : List Nat) => cons a (cons b acc) ';
const consList = 'def consList : (ks : List Nat) -> (x : Nat) -> (acc : List Nat) -> List Nat := ' +
  'fun (ks : List Nat) (x : Nat) (acc : List Nat) => ks ';
const viaThree = 'def viaThree : (h : Three) -> (x : Nat) -> (acc : Nat) -> Nat := ' +
  'fun (h : Three) (x : Nat) (acc : Nat) => h 1 x acc ';
const pick4 = 'def pick4 : (j : Nat) -> (k : Nat) -> (x : Nat) -> (acc : Nat) -> Nat := ' +
  'fun (j : Nat) (k : Nat) (x : Nat) (acc : Nat) => k ';
const xs = 'def xs : List Nat := cons 4 (cons 7 nil) ';
const count = 'def count : Nat := 3 ';
const prefix = three + pick + pick3 + takeX + keepAcc + headOf + consK + consTwo + consList + viaThree + pick4 + xs + count;

test('fold takes a partial application as its function', () => {
  assert.equal(last(`${prefix}def result : Nat := fold (pick3 9) 0 xs`), 9);
  assert.equal(last(`${prefix}def result : Nat := fold (takeX 9) 0 xs`), 4);
  assert.equal(last(`${prefix}def result : Nat := fold (keepAcc 9) 0 xs`), 0);
  assert.equal(last(`${prefix}def result : Nat := fold ((pick3 9)) 0 xs`), 9);
  assert.equal(last(`${prefix}def result : Nat := fold (headOf) 0 xs`), 4);
  assert.equal(last(`${prefix}def result : Nat := fold (pick3 (pick 2 5)) 0 xs`), 2);
  assert.deepEqual(last(`${prefix}def result : List Nat := fold (consK 1) nil xs`), [1, 1]);
  assert.deepEqual(last(`${prefix}def result : List Nat := fold (consTwo 1 2) nil xs`), [1, 2, 1, 2]);
  assert.deepEqual(last(`${prefix}def result : List Nat := fold (consList xs) nil xs`), [4, 7]);
  assert.equal(last(`${prefix}def result : Nat := fold (pick 1) 0 count`), 1);
});

test('partial fold functions bind values from the caller scope', () => {
  assert.equal(last(prefix +
    'def choose : (k : Nat) -> Nat := fun (k : Nat) => fold (pick3 k) 0 xs ' +
    'def result : Nat := choose 3'), 3);
  assert.equal(last(prefix +
    'def captured : Nat := 42 ' +
    'def target : Three := fun (k : Nat) (x : Nat) (acc : Nat) => captured ' +
    'def forward : (captured : Nat) -> Nat := fun (captured : Nat) => fold (target captured) 0 xs ' +
    'def result : Nat := forward 9'), 42);
});

test('partial fold functions bind function names and apply function parameters', () => {
  assert.equal(last(`${prefix}def result : Nat := fold (viaThree pick3) 0 xs`), 1);
  assert.equal(last(`${prefix}def result : Nat := fold (viaThree takeX) 0 xs`), 4);
  const useThree = 'def useThree : (h : Three) -> Nat := fun (h : Three) => fold (h 5) 0 xs ';
  assert.equal(last(`${prefix}${useThree}def result : Nat := useThree pick3`), 5);
  assert.equal(last(`${prefix}${useThree}def result : Nat := useThree takeX`), 4);
});

test('partial fold functions bind nested partial applications', () => {
  assert.equal(last(`${prefix}def result : Nat := fold (viaThree (pick4 1)) 0 xs`), 1);
});

test('partial fold functions reject bad bound arguments', () => {
  const mistyped = `${prefix}def result : Nat := fold (pick3 "x") 0 xs`;
  assert.deepEqual(run(mistyped), { error: {
    byte: mistyped.lastIndexOf('"x"'), message: 'term does not have the declared type',
  } });
  const nested = `${prefix}def result : Nat := fold (viaThree (pick4 "x")) 0 xs`;
  assert.deepEqual(run(nested), { error: {
    byte: nested.lastIndexOf('"x"'), message: 'term does not have the declared type',
  } });
  const polymorphic = prefix +
    'def generic : (A : Type 0) -> (a : A) -> (x : Nat) -> (acc : Nat) -> Nat := ' +
    'fun (A : Type 0) (a : A) (x : Nat) (acc : Nat) => acc ' +
    'def result : Nat := fold (generic Nat 1) 0 xs';
  assert.equal(run(polymorphic).instances.at(-1).value, 0);
  const data = `${prefix}def result : Nat := fold (xs) 0 xs`;
  assert.deepEqual(run(data), { error: {
    byte: data.lastIndexOf('(xs)') + 1, message: 'expected the name of a function',
  } });
  const extra = `${prefix}def result : Nat := fold (pick3 1 2 3 4) 0 xs`;
  assert.deepEqual(run(extra), { error: {
    byte: extra.lastIndexOf('4)'), message: 'expected closing parenthesis',
  } });
  const number = `${prefix}def result : List Nat := unfold (1) 3 0`;
  assert.deepEqual(run(number), { error: {
    byte: number.lastIndexOf('(1)') + 1, message: 'expected the name of a function',
  } });
  assert.ok(run(`${prefix}def result : Nat := fold (pick3 1 2 3) 0 xs`).error);
  assert.ok(run(`${prefix}def result : Nat := fold (pick3) 0 xs`).error);
  assert.ok(run(`${prefix}def result : Nat := fold pick3 1 0 xs`).error);
  assert.ok(run(`${prefix}def result : Nat := fold (pick3 1 0 xs`).error);
  assert.ok(run(`${prefix}def result : Nat := fold () 0 xs`).error);
});

test('the fold-partial example compiles', async () => {
  const source = await readFile(new URL('../examples/fold-partial.lang', import.meta.url), 'utf8');
  const result = run(source);
  assert.equal(result['sample-lang'], 1, JSON.stringify(result));
  const values = Object.fromEntries(result.instances.map(item => [item.name, item.value]));
  assert.equal(values.nines, 9);
  assert.deepEqual(values.ones, [1, 1]);
  assert.equal(values.chosen, 3);
  assert.equal(values.fives, 5);
});
