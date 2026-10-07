import assert from 'node:assert/strict';
import test from 'node:test';
import { readFile } from 'node:fs/promises';
import { compile, encoder, run, reject } from './helper.mjs';

const last = source => {
  const result = run(source);
  assert.equal(result['sample-lang'], 1, JSON.stringify(result));
  return result.instances.at(-1).value;
};
const picker = 'def Picker : Type 0 := (k : Nat) -> (n : Nat) -> Nat ';
const onNat = 'def onNat : (n : Nat) -> Nat := fun (n : Nat) => n ';
const onFlag = 'def onFlag : (b : Flag) -> Nat := fun (b : Flag) => 20 ';
const onText = 'def onText : (t : Text) -> Nat := fun (t : Text) => 30 ';
const onItems = 'def onItems : (results : List Nat) -> Nat := fun (results : List Nat) => 40 ';
const onAttrs = 'def onAttrs : (results : List (Prod Text Nat)) -> Nat := fun (results : List (Prod Text Nat)) => 50 ';
const pickK = 'def pickK : Picker := fun (k : Nat) (n : Nat) => k ';
const pickN = 'def pickN : Picker := fun (k : Nat) (n : Nat) => n ';
const pickTwo = 'def pickTwo : (a : Nat) -> (b : Nat) -> (n : Nat) -> Nat := fun (a : Nat) (b : Nat) (n : Nat) => b ';
const flagK = 'def flagK : (k : Nat) -> (b : Flag) -> Nat := fun (k : Nat) (b : Flag) => k ';
const textK = 'def textK : (k : Nat) -> (t : Text) -> Nat := fun (k : Nat) (t : Text) => k ';
const itemsK = 'def itemsK : (k : Nat) -> (results : List Nat) -> Nat := fun (k : Nat) (results : List Nat) => k ';
const attrsK = 'def attrsK : (k : Nat) -> (results : List (Prod Text Nat)) -> Nat := ' +
  'fun (k : Nat) (results : List (Prod Text Nat)) => k ';
const headOf = 'def headOf : (x : Nat) -> (acc : Nat) -> Nat := fun (x : Nat) (acc : Nat) => x ';
const vNull = 'def vNull : Value := valueNull ';
const vNumber = 'def vNumber : Value := valueNat 9 ';
const vText = 'def vText : Value := valueText "leaf" ';
const vItems = 'def vItems : Value := valueItems (valuesItem (valueNat 1) (valuesItem (valueText "a") valuesEnd)) ';
const vAttrs = 'def vAttrs : Value := valueAttrs (attrsField "k" (valueNat 1) attrsEnd) ';
const xs = 'def xs : List Nat := cons 4 (cons 7 nil) ';
const count = 'def count : Nat := 3 ';
const prefix = picker + onNat + onFlag + onText + onItems + onAttrs + pickK + pickN + pickTwo + flagK + textK +
  itemsK + attrsK + headOf + vNull + vNumber + vText + vItems + vAttrs + xs + count;
const rest = 'onFlag onText onItems onAttrs';

test('a fold over Value takes partial applications as its functions', () => {
  assert.equal(last(`${prefix}def result : Nat := fold (pickK 7) ${rest} 0 vNumber`), 7);
  assert.equal(last(`${prefix}def result : Nat := fold (pickN 7) ${rest} 0 vNumber`), 9);
  assert.equal(last(`${prefix}def result : Nat := fold (pickTwo 1 2) ${rest} 0 vNumber`), 2);
  assert.equal(last(`${prefix}def result : Nat := fold ((pickK 7)) ${rest} 0 vNumber`), 7);
  assert.equal(last(`${prefix}def result : Nat := fold (pickK (pickK 6 0)) ${rest} 0 vNumber`), 6);
  assert.equal(last(`${prefix}def result : Nat := fold (pickK 7) ${rest} 0 vNull`), 0);
  assert.equal(last(`${prefix}def result : Nat := fold onNat (flagK 2) onText onItems onAttrs 0 vNumber`), 9);
  assert.equal(last(`${prefix}def result : Nat := fold onNat onFlag (textK 3) onItems onAttrs 0 vText`), 3);
  assert.equal(last(`${prefix}def result : Nat := fold onNat onFlag onText (itemsK 4) onAttrs 0 vItems`), 4);
  assert.equal(last(`${prefix}def result : Nat := fold onNat onFlag onText onItems (attrsK 5) 0 vAttrs`), 5);
  const five = 'fold (pickK 1) (flagK 2) (textK 3) (itemsK 4) (attrsK 5) 0';
  assert.equal(last(`${prefix}def result : Nat := ${five} vNumber`), 1);
  assert.equal(last(`${prefix}def result : Nat := ${five} vText`), 3);
  assert.equal(last(`${prefix}def result : Nat := ${five} vItems`), 4);
  assert.equal(last(`${prefix}def result : Nat := ${five} vAttrs`), 5);
});

test('a second term that is not a function still selects the fold over a sequence', () => {
  assert.equal(last(`${prefix}def result : Nat := fold (headOf) 0 xs`), 4);
  assert.equal(last(`${prefix}def result : Nat := fold (pickK 1) 0 count`), 1);
  assert.equal(last(`${prefix}def result : Nat := fold onNat count count`), 3);
  assert.equal(last(`${prefix}def result : Nat := fold headOf (0) xs`), 4);
  assert.equal(last(`${prefix}def result : Nat := fold (headOf) (pickK 3 4) xs`), 4);
  assert.equal(last(`${prefix}def result : Nat := fold onNat (pickK 3 4) count`), 3);
});

test('partial algebra arguments consume the shared work budget once', () => {
  let work = 'def d0 : (n : Nat) -> Nat := fun (n : Nat) => n ';
  for (let depth = 1; depth <= 12; depth++) {
    work += `def d${depth} : (n : Nat) -> Nat := fun (n : Nat) => d${depth - 1} (d${depth - 1} n) `;
  }
  const setup = picker + onNat + onFlag + onText + onItems + onAttrs + pickK + flagK + vNumber + work;
  const sourceFor = functions => {
    const source = `${setup}def result : Nat := fold ${functions} onText onItems onAttrs 0 vNumber`;
    assert.ok(source.length <= 1800);
    return source.padEnd(1800, ' ');
  };
  // d12 applies 8191 bodies. The 14464-body budget permits one call, but not two.
  assert.equal(last(sourceFor('(pickK (d12 7)) onFlag')), 7);
  assert.equal(last(sourceFor('onNat (flagK (d12 7))')), 9);
  assert.equal(run(sourceFor('(pickK (d12 7)) (flagK (d12 7))')).error.message, 'work budget exceeded');
});

test('partial algebra functions bind caller values and apply function parameters', () => {
  assert.equal(last(prefix +
    `def choose : (k : Nat) -> Nat := fun (k : Nat) => fold (pickK k) ${rest} 0 vNumber ` +
    'def result : Nat := choose 3'), 3);
  assert.equal(last(prefix +
    'def captured : Nat := 42 ' +
    'def target : Picker := fun (k : Nat) (n : Nat) => captured ' +
    `def forward : (captured : Nat) -> Nat := fun (captured : Nat) => fold (target captured) ${rest} 0 vNumber ` +
    'def result : Nat := forward 9'), 42);
  const useK = `def useK : (h : Picker) -> Nat := fun (h : Picker) => fold (h 5) ${rest} 0 vNumber `;
  assert.equal(last(`${prefix}${useK}def result : Nat := useK pickK`), 5);
  assert.equal(last(`${prefix}${useK}def result : Nat := useK pickN`), 9);
});

test('partial algebra functions reject bad bound arguments and bad fits', () => {
  const first = `${prefix}def result : Nat := fold (flagK 2) ${rest} 0 vNumber`;
  assert.deepEqual(run(first), { error: {
    byte: first.lastIndexOf('(flagK 2)'), message: 'term does not have the declared type',
  } });
  const mistyped = `${prefix}def result : Nat := fold (pickK "x") ${rest} 0 vNumber`;
  assert.deepEqual(run(mistyped), { error: {
    byte: mistyped.lastIndexOf('"x"'), message: 'term does not have the declared type',
  } });
  const third = `${prefix}def result : Nat := fold onNat onFlag (flagK 2) onItems onAttrs 0 vText`;
  assert.deepEqual(run(third), { error: {
    byte: third.lastIndexOf('(flagK 2)'), message: 'term does not have the declared type',
  } });
  const fifth = `${prefix}def result : Nat := fold onNat onFlag onText onItems (itemsK 5) 0 vAttrs`;
  assert.deepEqual(run(fifth), { error: {
    byte: fifth.lastIndexOf('(itemsK 5)'), message: 'term does not have the declared type',
  } });
  const polymorphic = prefix +
    'def generic : (A : Type 0) -> (a : A) -> (t : Text) -> Nat := fun (A : Type 0) (a : A) (t : Text) => 1 ' +
    'def result : Nat := fold onNat onFlag (generic Nat 1) onItems onAttrs 0 vText';
  assert.equal(run(polymorphic).instances.at(-1).value, 1);
  assert.ok(run(`${prefix}def result : Nat := fold onNat (xs) onText onItems onAttrs 0 vNumber`).error);
  assert.ok(run(`${prefix}def result : Nat := fold onNat (textK 2) onText onItems onAttrs 0 vNumber`).error);
  assert.ok(run(`${prefix}def result : Nat := fold (pickTwo 1 2 3) ${rest} 0 vNumber`).error);
  assert.ok(run(`${prefix}def result : Nat := fold onNat (flagK 2 onText onItems onAttrs 0 vNumber`).error);
});

test('the value-fold-partial example compiles', async () => {
  const source = await readFile(new URL('../examples/value-fold-partial.lang', import.meta.url), 'utf8');
  const result = run(source);
  assert.equal(result['sample-lang'], 1, JSON.stringify(result));
  const values = Object.fromEntries(result.instances.map(item => [item.name, item.value]));
  assert.equal(values.sevens, 7);
  assert.equal(values.nines, 9);
  assert.equal(values.fives, 5);
  assert.equal(values.chosen, 3);
  assert.equal(values.picked, 9);
});
