import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import test from 'node:test';
import { compile, encoder, run, reject } from './helper.mjs';

const values = source => {
  const result = run(source);
  assert.equal(result['sample-lang'], 1, JSON.stringify(result));
  return result.instances.map(item => item.value);
};

const fieldStep = 'def step : (n : Nat) -> Option (Prod (Prod Text Value) Nat) := ' +
  'fun (n : Nat) => some (pair (pair "key" (valueNat n)) 2) ';

test('unfold rejects duplicate attribute keys instead of emitting lossy JSON', () => {
  reject(fieldStep + 'def result : Attrs := unfold step 2 1', 'duplicate attribute key', 'unfold');
});

test('unfold checks duplicate keys even when the Attrs result is consumed by a fold', () => {
  const consume = 'def keep : (field : Prod Text Value) -> (acc : Nat) -> Nat := ' +
    'fun (field : Prod Text Value) (acc : Nat) => acc ';
  const wrap = 'def id : (a : Attrs) -> Attrs := fun (a : Attrs) => a ';
  reject(fieldStep + consume + wrap +
    'def result : Nat := fold keep 0 (id (unfold step 2 1))', 'duplicate attribute key', 'unfold');
});

test('unfold stops at the requested limit before generating a duplicate field', () => {
  assert.deepEqual(values(fieldStep +
    'def empty : Attrs := unfold step 0 1 def one : Attrs := unfold step 1 1'), [{}, { key: 1 }]);
});

test('unfold and filter preserve distinct attribute keys and field order', () => {
  const source = 'def step : (s : Prod Text Text) -> Option (Prod (Prod Text Value) (Prod Text Text)) := ' +
    'fun (s : Prod Text Text) => some (pair (pair (first s) valueNull) (pair (second s) (first s))) ' +
    'def yes : (field : Prod Text Value) -> Flag := fun (field : Prod Text Value) => flagYes ' +
    'def no : (field : Prod Text Value) -> Flag := fun (field : Prod Text Value) => flagNo ' +
    'def fields : Attrs := unfold step 2 (pair "z" "a") ' +
    'def kept : Attrs := filter yes fields def removed : Attrs := filter no fields';
  const result = values(source);
  assert.deepEqual(result, [{ z: null, a: null }, { z: null, a: null }, {}]);
  assert.deepEqual(Object.keys(result[0]), ['z', 'a']);
  assert.deepEqual(Object.keys(result[1]), ['z', 'a']);
});

test('fold traverses lists from the right and returns the initial value for an empty list', () => {
  const step = 'def prepend : (n : Nat) -> (acc : List Nat) -> List Nat := ' +
    'fun (n : Nat) (acc : List Nat) => cons n acc ';
  assert.deepEqual(values(step + 'def xs : List Nat := cons 1 (cons 2 nil) ' +
    'def empty : List Nat := nil def result : List Nat := fold prepend (cons 3 nil) xs ' +
    'def initial : List Nat := fold prepend (cons 3 nil) empty'), [[1, 2], [], [1, 2, 3], [3]]);
});

test('fold over Nat applies its unary step exactly n times', () => {
  const step = 'def prepend : (acc : List Nat) -> List Nat := fun (acc : List Nat) => cons 7 acc ';
  assert.deepEqual(values(step + 'def n : Nat := 3 def z : Nat := 0 ' +
    'def three : List Nat := fold prepend nil n ' +
    'def zero : List Nat := fold prepend (cons 9 nil) z'), [3, 0, [7, 7, 7], [9]]);
});

for (const [carrier, element, input, expected] of [
  ['Text', 'Nat', '"é"', [195, 169]],
  ['Values', 'Value', 'valuesItem valueNull (valuesItem (valueNat 2) valuesEnd)', [null, 2]],
  ['Attrs', 'Prod Text Value', 'attrsField "z" valueNull (attrsField "a" (valueNat 2) attrsEnd)',
    [{ first: 'z', second: null }, { first: 'a', second: 2 }]],
]) {
  test(`fold exposes the ordered elements of ${carrier}`, () => {
    const item = element.includes(' ') ? `(${element})` : element;
    const step = `def prepend : (x : ${element}) -> (acc : List ${item}) -> List ${item} := ` +
      `fun (x : ${element}) (acc : List ${item}) => cons x acc `;
    assert.deepEqual(values(step + `def source : ${carrier} := ${input} ` +
      `def result : List ${item} := fold prepend nil source`).at(-1), expected);
  });
}

test('unfold threads its seed and preserves element order', () => {
  const step = 'def step : (s : Prod Nat Nat) -> Option (Prod Nat (Prod Nat Nat)) := ' +
    'fun (s : Prod Nat Nat) => some (pair (first s) (pair (second s) (first s))) ';
  assert.deepEqual(values(step + 'def result : List Nat := unfold step 3 (pair 1 2) ' +
    'def text : Text := unfold step 2 (pair 65 66)'), [[1, 2, 1], 'AB']);
});

test('unfold into Nat counts successful steps and distinguishes some none from none', () => {
  const next = 'def next : (s : Option Nat) -> Option (Option Nat) := ' +
    'fun (s : Option Nat) => some none ';
  const stop = 'def stop : (s : Option Nat) -> Option (Option Nat) := ' +
    'fun (s : Option Nat) => none ';
  assert.deepEqual(values(next + stop + 'def count : Nat := unfold next 3 none ' +
    'def empty : Nat := unfold stop 3 none'), [3, 0]);
});

test('unfold into Values retains null payloads and accepts an Option seed', () => {
  const step = 'def step : (s : Option Nat) -> Option (Prod Value (Option Nat)) := ' +
    'fun (s : Option Nat) => some (pair valueNull none) ';
  assert.deepEqual(values(step + 'def result : Values := unfold step 2 (some 7)'), [[null, null]]);
});

test('unfold into Text rejects a non-byte element', () => {
  const step = 'def step : (n : Nat) -> Option (Prod Nat Nat) := fun (n : Nat) => some (pair n n) ';
  reject(step + 'def result : Text := unfold step 1 256', 'textByte requires a byte below 256', 'unfold');
});

for (const [carrier, element, input, expected, empty] of [
  ['Text', 'Nat', '"é"', 'é', ''],
  ['Values', 'Value', 'valuesItem valueNull (valuesItem (valueNat 2) valuesEnd)', [null, 2], []],
]) {
  test(`filter retains or removes all elements of ${carrier}`, () => {
    const predicates = `def yes : (x : ${element}) -> Flag := fun (x : ${element}) => flagYes ` +
      `def no : (x : ${element}) -> Flag := fun (x : ${element}) => flagNo `;
    assert.deepEqual(values(predicates + `def source : ${carrier} := ${input} ` +
      `def kept : ${carrier} := filter yes source def removed : ${carrier} := filter no source`),
    [expected, expected, empty]);
  });
}

const keep = 'def keep : (acc : Nat) -> Nat := fun (acc : Nat) => acc ';
const again = 'def again : (s : Nat) -> Option Nat := fun (s : Nat) => some s ';
const structureError = 'the type has no instance of this structure';
const termError = 'term does not have the declared type';
const stepError = 'expected the name of a function';
const casesError = 'the number of functions does not fit the source of this fold';

test('fold refuses a source without a carrier, or a Value with one function, at the source', () => {
  reject(keep + 'def src : Flag := flagYes def r : Nat := fold keep 0 src', structureError, 'src');
  reject(keep + 'def blank : Value := valueNull def r : Nat := fold keep 0 blank',
    casesError, 'blank');
});

test('unfold refuses a declared type without a carrier at the keyword', () => {
  reject(again + 'def r : Flag := unfold again 1 0', structureError, 'unfold');
});

test('fold and unfold work in a function body and are refused bare as an argument', () => {
  assert.deepEqual(values(keep + 'def h : (n : Nat) -> Nat := fun (n : Nat) => fold keep 0 n def y : Nat := h 2'), [0]);
  values(again + 'def h : (n : Nat) -> Nat := fun (n : Nat) => unfold again 1 n def y : Nat := h 2');
  reject(keep + 'def n : Nat := 2 def r : Option Nat := some fold keep 0 n',
    'argument needs parentheses', 'fold');
  reject(again + 'def r : Option Nat := some unfold again 1 0', 'argument needs parentheses', 'unfold');
  assert.deepEqual(values(keep + again + 'def n : Nat := 2 def r : Option Nat := some (fold keep 0 n) ' +
    'def u : Option Nat := some (unfold again 2 0)'), [2, 0, 2]);
});

test('the function argument must name a function', () => {
  reject('def zero : Nat := 0 def n : Nat := 2 def r : Nat := fold zero 0 n', stepError, 'zero');
  reject('def n : Nat := 2 def r : Nat := fold missing 0 n', stepError, 'missing');
  reject('def n : Nat := 2 def r : Nat := fold 3 0 n', stepError, '3');
  reject('def r : Nat := unfold "g" 1 0', stepError, '"g"');
});

test('fold and unfold check the function, initial value, limit and seed types', () => {
  reject('def add : (a : Nat) -> (b : Nat) -> Nat := fun (a : Nat) (b : Nat) => a ' +
    'def n : Nat := 2 def r : Nat := fold add 0 n', termError, 'add');
  reject(keep + 'def xs : List Nat := cons 1 nil def r : Nat := fold keep 0 xs', termError, 'keep');
  reject(again + 'def r : List Nat := unfold again 1 0', termError, 'again');
  reject(keep + 'def n : Nat := 2 def r : Nat := fold keep "x" n', termError, '"x"');
  reject(again + 'def r : Nat := unfold again "x" 0', termError, '"x"');
  reject(again + 'def r : Nat := unfold again 1 "x"', termError, '"x"');
});

test('the fold source must synthesize its type', () => {
  reject(keep + 'def r : Nat := fold keep 0 nil', 'cannot infer the type of this term', 'nil');
});

test('each element uses one step of the depth fuel', () => {
  assert.deepEqual(values(keep + 'def n : Nat := 40 def r : Nat := fold keep 5 n'), [40, 5]);
  reject(keep + 'def n : Nat := 600 def r : Nat := fold keep 0 n', 'compiler fuel exhausted', 'fold');
  const source = again + 'def r : Nat := unfold again 600 0';
  const result = run(source);
  assert.deepEqual(Object.keys(result), ['error'], JSON.stringify(result));
  assert.equal(result.error.message, 'compiler fuel exhausted');
  assert.equal(result.error.byte, source.indexOf('some s') + 'some '.length);
});

test('the algebra example compiles', async () => {
  const source = await readFile(new URL('../examples/algebra.lang', import.meta.url), 'utf8');
  const result = run(source);
  assert.equal(result['sample-lang'], 1, JSON.stringify(result));
  assert.deepEqual(result.instances.map(item => [item.name, item.value]), [
    ['counts', [1, 2]], ['copied', [1, 2]], ['three', 3], ['ticks', ['tick', 'tick', 'tick']],
    ['greeting', 'Hi'], ['bytes', [72, 105]], ['alternating', [1, 2, 1, 2]], ['letters', 'ABA'],
    ['fields', { id: null, name: null }], ['present', { id: null, name: null }], ['cleared', ''],
    ['valueTree', { left: { left: null, right: null }, right: null }], ['treeShape', 'attrs'],
    ['plainLeaf', 'plain'], ['leafText', 'plain'],
  ]);
});

const onNat = 'def onNat : (n : Nat) -> Text := fun (n : Nat) => "nat" ';
const onRest = 'def onFlag : (b : Flag) -> Text := fun (b : Flag) => "flag" ' +
  'def onText : (t : Text) -> Text := fun (t : Text) => t ' +
  'def onItems : (xs : List Text) -> Text := fun (xs : List Text) => "items" ' +
  'def onAttrs : (fs : List (Prod Text Text)) -> Text := fun (fs : List (Prod Text Text)) => "attrs" ';
const onValue = onNat + onRest;
const foldValue = 'fold onNat onFlag onText onItems onAttrs "null"';
const message = (source, expected) => {
  const result = run(source);
  assert.deepEqual(Object.keys(result), ['error'], JSON.stringify(result));
  assert.equal(result.error.message, expected);
};

for (const [term, expected] of [
  ['valueNull', 'null'], ['valueNat 3', 'nat'], ['valueFlag flagYes', 'flag'],
  ['valueText "leaf"', 'leaf'], ['valueItems valuesEnd', 'items'],
  ['valueItems (valuesItem valueNull valuesEnd)', 'items'], ['valueAttrs attrsEnd', 'attrs'],
  ['valueAttrs (attrsField "k" (valueNat 1) attrsEnd)', 'attrs'],
]) {
  test(`fold over Value selects the case of ${term}`, () => {
    assert.deepEqual(values(onValue + `def v : Value := ${term} def r : Text := ${foldValue} v`).at(-1),
      expected);
  });
}

test('fold over Value gives each scalar payload to its function', () => {
  const sum = 'Sum Nat (Sum Flag Text)';
  const functions = `def a : (n : Nat) -> ${sum} := fun (n : Nat) => inl n ` +
    `def b : (f : Flag) -> ${sum} := fun (f : Flag) => inr (inl f) ` +
    `def c : (t : Text) -> ${sum} := fun (t : Text) => inr (inr t) ` +
    `def d : (xs : List (${sum})) -> ${sum} := fun (xs : List (${sum})) => inl 4 ` +
    `def e : (fs : List (Prod Text (${sum}))) -> ${sum} := fun (fs : List (Prod Text (${sum}))) => inl 5 `;
  const use = name => `fold a b c d e (inl 0) ${name}`;
  assert.deepEqual(values(functions + 'def vn : Value := valueNat 7 def vf : Value := valueFlag flagYes ' +
    'def vt : Value := valueText "x" def vz : Value := valueNull ' +
    'def vi : Value := valueItems valuesEnd def va : Value := valueAttrs attrsEnd ' +
    `def rn : ${sum} := ${use('vn')} def rf : ${sum} := ${use('vf')} ` +
    `def rt : ${sum} := ${use('vt')} def rz : ${sum} := ${use('vz')} ` +
    `def ri : ${sum} := ${use('vi')} def ra : ${sum} := ${use('va')}`).slice(6),
  [{ inl: 7 }, { inr: { inl: true } }, { inr: { inr: 'x' } }, { inl: 0 }, { inl: 4 }, { inl: 5 }]);
});

test('fold over Value folds the children of items and of fields', () => {
  const byte = 'def onNat : (n : Nat) -> Text := fun (n : Nat) => textByte n "!" ';
  const nested = number => 'def v : Value := valueItems (valuesItem (valueText "a") (valuesItem ' +
    `(valueAttrs (attrsField "k" (valueItems (valuesItem (valueNat ${number}) valuesEnd)) attrsEnd)) valuesEnd)) `;
  assert.deepEqual(values(byte + onRest + nested(65) + `def r : Text := ${foldValue} v`).at(-1), 'items');
  message(byte + onRest + nested(256) + `def r : Text := ${foldValue} v`,
    'textByte requires a byte below 256');
});

test('fold over Value checks each function and the initial value against the declared type', () => {
  const other = 'def other : (p : Prod Nat Nat) -> Text := fun (p : Prod Nat Nat) => "x" ';
  const names = ['onNat', 'onFlag', 'onText', 'onItems', 'onAttrs'];
  for (const index of names.keys()) {
    const used = names.map((name, at) => (at === index ? 'other' : name)).join(' ');
    reject(onValue + other + `def v : Value := valueNull def r : Text := fold ${used} "null" v`,
      termError, 'other');
  }
  reject(onValue + 'def wrongResult : (n : Nat) -> Nat := fun (n : Nat) => n def v : Value := valueNull ' +
    'def r : Text := fold wrongResult onFlag onText onItems onAttrs "null" v', termError, 'wrongResult');
  reject(onValue + 'def v : Value := valueNull def r : Text := fold onNat onFlag onText onItems onAttrs 7 v',
    termError, '7');
});

test('the number of functions must fit the source of a fold', () => {
  reject(onValue + `def n : Nat := 2 def r : Text := ${foldValue} n`, casesError, 'n');
  reject(onValue + 'def v : Value := valueNull def r : Text := fold onNat onFlag "null" v',
    stepError, '"null"');
  reject(onValue + 'def v : Value := valueNull ' +
    'def r : Text := fold onNat onFlag onText onItems onAttrs onNat "null" v',
    'argument needs parentheses', 'onNat');
  reject(onValue + `def r : Text := ${foldValue} valueNull`,
    'cannot infer the type of this term', 'valueNull');
});

test('a fold over Value works in a function body and is refused bare as an argument', () => {
  values(onValue + `def h : (v : Value) -> Text := fun (v : Value) => ${foldValue} v def y : Text := h valueNull`);
  reject(onValue + `def v : Value := valueNull def r : Option Text := some ${foldValue} v`,
    'argument needs parentheses', 'fold');
  assert.deepEqual(values(onValue +
    `def v : Value := valueNull def r : Option Text := some (${foldValue} v)`), [null, 'null']);
});

const layer = seed => `Option (Sum Nat (Sum Flag (Sum Text (Sum (List ${seed}) (List (Prod Text ${seed}))))))`;
const grow = (name, seed, body) =>
  `def ${name} : (s : ${seed}) -> ${layer(seed)} := fun (s : ${seed}) => ${body} `;
const growChain = grow('growChain', 'Nat', 'some (inr (inr (inr (inl (cons s nil)))))');

test('unfold into Value builds each scalar layer and gives null for none', () => {
  assert.deepEqual(values(
    grow('growNat', 'Nat', 'some (inl s)') + grow('growFlag', 'Flag', 'some (inr (inl s))') +
    grow('growText', 'Text', 'some (inr (inr (inl s)))') + grow('growNone', 'Nat', 'none') +
    'def a : Value := unfold growNat 1 7 def b : Value := unfold growFlag 1 flagYes ' +
    'def c : Value := unfold growText 1 "x" def d : Value := unfold growNone 5 7 ' +
    'def e : Value := unfold growNat 0 7'), [7, true, 'x', null, null]);
});

test('unfold into Value applies its coalgebra at most n times, depth first', () => {
  const twice = grow('growTwice', 'Nat', 'some (inr (inr (inr (inl (cons s (cons s nil))))))');
  assert.deepEqual(
    values(twice + [0, 1, 2, 3].map(n => `def t${n} : Value := unfold growTwice ${n} 5`).join(' ')),
    [null, [null, null], [[null, null], null], [[[null, null], null], null]]);
});

test('unfold into Value keeps the field order and refuses a duplicate key', () => {
  const fields = grow('growFields', 'Text',
    'some (inr (inr (inr (inr (cons (pair "z" s) (cons (pair "a" s) nil))))))');
  const result = values(fields +
    'def one : Value := unfold growFields 1 "s" def two : Value := unfold growFields 2 "s"');
  assert.deepEqual(result, [{ z: null, a: null }, { z: { z: null, a: null }, a: null }]);
  assert.deepEqual(Object.keys(result[0]), ['z', 'a']);
  assert.deepEqual(Object.keys(result[1].z), ['z', 'a']);
  const twin = grow('growTwin', 'Text',
    'some (inr (inr (inr (inr (cons (pair "k" s) (cons (pair "k" s) nil))))))');
  reject(twin + 'def bad : Value := unfold growTwin 1 "s"', 'duplicate attribute key', 'unfold');
});

test('unfold into Value checks the coalgebra, the limit and the seed', () => {
  reject(again + 'def r : Value := unfold again 1 0', termError, 'again');
  reject(`def mixed : (s : Nat) -> ${layer('Text')} := fun (s : Nat) => none ` +
    'def r : Value := unfold mixed 1 0', termError, 'mixed');
  reject(growChain + 'def r : Value := unfold growChain "x" 0', termError, '"x"');
  reject(growChain + 'def r : Value := unfold growChain 1 "x"', termError, '"x"');
  values(growChain + 'def h : (n : Nat) -> Value := fun (n : Nat) => unfold growChain 1 n def y : Value := h 0');
});

test('fold consumes a Value that unfold built', () => {
  assert.deepEqual(values(onValue + growChain +
    `def v : Value := unfold growChain 30 1 def r : Text := ${foldValue} v`).at(-1), 'items');
});

test('each application into Value uses one step of the depth fuel', () => {
  message(growChain + 'def v : Value := unfold growChain 600 1', 'compiler fuel exhausted');
});
