import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import test from 'node:test';
import { compile, encoder, run, reject } from './helper.mjs';

const last = source => {
  const result = run(source);
  assert.equal(result['sample-lang'], 1, JSON.stringify(result));
  return result.instances.at(-1);
};
// Each case names the last occurrence of `at` in the source as the error byte.

const id = 'def id : (n : Nat) -> Nat := fun (n : Nat) => n ';
// The bodies of `one` and `pick` use the global x. A caller that has a
// parameter x must not replace it.
const scoped = 'def x : Nat := 1 def one : (y : Nat) -> Nat := fun (y : Nat) => x ' +
  'def pick : (item : Nat) -> (acc : Nat) -> Nat := fun (item : Nat) (acc : Nat) => x ';
// b0 gives two copies. Each later function binds the previous function over
// its own result, so bK gives 2^(2^K) copies.
const fan = k => k === 0 ? 'def b0 : (n : Nat) -> List Nat := fun (n : Nat) => cons n (cons n nil) '
  : `def b${k} : (n : Nat) -> List Nat := fun (n : Nat) => bind b${k - 1} (b${k - 1} n) `;
const fans = depth => Array.from({ length: depth + 1 }, (unused, k) => fan(k)).join('');

test('a function body applies a function with a structure form', () => {
  assert.deepEqual(last(id + 'def h : (n : Nat) -> List Nat := fun (n : Nat) => map id (cons n (cons 7 nil)) ' +
    'def r : List Nat := h 9'), { name: 'r', type: 'List (Nat)', value: [9, 7] });
  assert.deepEqual(last(id + 'def inner : (n : Nat) -> List Nat := fun (n : Nat) => map id (cons n nil) ' +
    'def outer : (n : Nat) -> List (List Nat) := fun (n : Nat) => map inner (cons n (cons 3 nil)) ' +
    'def r : List (List Nat) := outer 4').value, [[4], [3]]);
  assert.deepEqual(last(fans(1) + 'def r : List Nat := b1 5').value, [5, 5, 5, 5]);
});

test('a function that a form applies evaluates in the scope of its definition', () => {
  assert.deepEqual(last(scoped + 'def g : (x : Nat) -> List Nat := fun (x : Nat) => map one (cons x (cons 7 nil)) ' +
    'def r : List Nat := g 9').value, [1, 1]);
  assert.equal(last(scoped + 'def g : (x : Nat) -> Nat := fun (x : Nat) => either one one (inl x) ' +
    'def r : Nat := g 9').value, 1);
  assert.equal(last(scoped + 'def g : (x : List Nat) -> Nat := fun (x : List Nat) => fold pick 0 x ' +
    'def r : Nat := g (cons 5 nil)').value, 1);
});

test('the definition checks a form in a function body and does not evaluate it', () => {
  assert.equal(last(id + 'def n : Nat := 2 def h : (k : Nat) -> List Nat := fun (k : Nat) => map id (cons k nil)').name, 'n');
  reject('def add : (a : Nat) -> (b : Nat) -> Nat := fun (a : Nat) (b : Nat) => a ' +
    'def g : (x : Nat) -> List Nat := fun (x : Nat) => map add (cons x nil)',
    'expected a function with one parameter', 'add');
  reject(id + 'def g : (x : Nat) -> Option (List Nat) := fun (x : Nat) => some map id (cons x nil)',
    'argument needs parentheses', 'map');
});

test('forms in function bodies use the shared work budget', () => {
  assert.equal(last(fans(2) + 'def r : List Nat := b2 5').value.length, 16);
  const result = run(fans(4) + 'def r : List Nat := b4 5');
  assert.deepEqual(Object.keys(result), ['error'], JSON.stringify(result).slice(0, 200));
  assert.equal(result.error.message, 'work budget exceeded');
  // The budget ends in the body of b0.
  assert.equal(result.error.byte, fans(4).indexOf('cons'));
});

const some = 'def keepSome : (n : Nat) -> Option Nat := fun (n : Nat) => some n ' +
  'def drop : (n : Nat) -> Option Nat := fun (n : Nat) => none ';
const flags = 'def yes : (n : Nat) -> Flag := fun (n : Nat) => flagYes def no : (n : Nat) -> Flag := fun (n : Nat) => flagNo ';
const sums = 'def ok : (n : Nat) -> Sum Text Nat := fun (n : Nat) => inr n ' +
  'def bad : (n : Nat) -> Sum Text Nat := fun (n : Nat) => inl "bad" ';
const algebra = 'def prepend : (n : Nat) -> (acc : List Nat) -> List Nat := fun (n : Nat) (acc : List Nat) => cons n acc ' +
  'def tick : (acc : List Text) -> List Text := fun (acc : List Text) => cons "tick" acc ';
// grow gives a layer of two fields for each seed. The five functions after it
// name the constructor that a fold over Value sees at the root.
const value = 'def grow : (s : Text) -> Option (Sum Nat (Sum Flag (Sum Text (Sum (List Text) (List (Prod Text Text)))))) := ' +
  'fun (s : Text) => some (inr (inr (inr (inr (cons (pair "left" s) (cons (pair "right" s) nil)))))) ' +
  'def onNat : (n : Nat) -> Text := fun (n : Nat) => "number" def onFlag : (b : Flag) -> Text := fun (b : Flag) => "flag" ' +
  'def onText : (t : Text) -> Text := fun (t : Text) => t def onItems : (results : List Text) -> Text := fun (results : List Text) => "items" ' +
  'def onAttrs : (results : List (Prod Text Text)) -> Text := fun (results : List (Prod Text Text)) => "attrs" ';
const folded = 'fold onNat onFlag onText onItems onAttrs "null"';

test('bind, filter and map in a function body use the instance of the result type', () => {
  const option = body => `def h : (n : Nat) -> Option Nat := fun (n : Nat) => ${body} def r : Option Nat := h 4`;
  assert.equal(last(some + option('bind keepSome (some n)')).value, 4);
  assert.equal(last(some + option('bind drop (some n)')).value, null);
  assert.equal(last(flags + option('filter yes (some n)')).value, 4);
  assert.equal(last(flags + option('filter no (some n)')).value, null);
  const sum = body => `def h : (n : Nat) -> Sum Text Nat := fun (n : Nat) => ${body} def r : Sum Text Nat := h 4`;
  assert.deepEqual(last(sums + sum('bind ok (inr n)')).value, { inr: 4 });
  assert.deepEqual(last(sums + sum('bind bad (inr n)')).value, { inl: 'bad' });
  assert.deepEqual(last(id + sum('map id (inr n)')).value, { inr: 4 });
  const text = body => `def h : (t : Text) -> Text := fun (t : Text) => ${body} def r : Text := h "Hi"`;
  assert.equal(last(flags + text('filter no t')).value, '');
  assert.equal(last(flags + text('filter yes t')).value, 'Hi');
});

test('fold in a function body consumes a parameter', () => {
  assert.deepEqual(last(algebra + 'def h : (k : Nat) -> List Text := fun (k : Nat) => fold tick nil k ' +
    'def r : List Text := h 2').value, ['tick', 'tick']);
  assert.deepEqual(last(algebra + 'def h : (t : Text) -> List Nat := fun (t : Text) => fold prepend nil t ' +
    'def r : List Nat := h "Hi"').value, [72, 105]);
  assert.deepEqual(last(algebra + 'def h : (items : List Nat) -> List Nat := fun (items : List Nat) => fold prepend (cons 0 nil) items ' +
    'def r : List Nat := h (cons 5 (cons 7 nil))').value, [5, 7, 0]);
});

test('fold and unfold over Value in a function body', () => {
  // The limit counts applications of grow in depth-first order. The second
  // application gives the layer of the first field, and each seed that is
  // left becomes valueNull.
  assert.deepEqual(last(value + 'def h : (s : Text) -> Value := fun (s : Text) => unfold grow 2 s ' +
    'def r : Value := h "seed"').value, { left: { left: null, right: null }, right: null });
  assert.deepEqual(last(value + 'def h : (s : Text) -> Value := fun (s : Text) => unfold grow 1 s ' +
    'def r : Value := h "seed"').value, { left: null, right: null });
  const fold = `def h : (v : Value) -> Text := fun (v : Value) => ${folded} v `;
  assert.equal(last(value + fold + 'def r : Text := h (valueText "plain")').value, 'plain');
  assert.equal(last(value + fold + 'def r : Text := h valueNull').value, 'null');
  assert.equal(last(value + fold + 'def r : Text := h (valueNat 3)').value, 'number');
  assert.equal(last(value + 'def h : (s : Text) -> Value := fun (s : Text) => unfold grow 2 s ' +
    `def k : (s : Text) -> Text := fun (s : Text) => ${folded} (h s) def r : Text := k "seed"`).value, 'attrs');
});

test('a function that unfold applies evaluates in the scope of its definition', () => {
  // The body of step uses the global x. The parameter x of g is the seed.
  assert.deepEqual(last('def x : Nat := 1 def step : (s : Nat) -> Option (Prod Nat Nat) := fun (s : Nat) => some (pair x s) ' +
    'def g : (x : Nat) -> List Nat := fun (x : Nat) => unfold step 2 x def r : List Nat := g 9').value, [1, 1]);
});

test('the definition refuses a source of the wrong type in a function body', () => {
  const mismatch = 'term does not have the declared type';
  reject(id + 'def g : (x : Text) -> List Nat := fun (x : Text) => map id (cons x nil)', mismatch, 'x nil');
  reject(some + 'def g : (x : Text) -> Option Nat := fun (x : Text) => bind keepSome (some x)', mismatch, 'x)');
  reject(flags + 'def g : (x : Flag) -> Text := fun (x : Flag) => filter no x', mismatch, 'x');
  reject(id + 'def g : (x : Nat) -> Nat := fun (x : Nat) => either id id x', mismatch, 'x');
  reject('def step : (s : Nat) -> Option (Prod Nat Nat) := fun (s : Nat) => some (pair s s) ' +
    'def g : (x : Text) -> List Nat := fun (x : Text) => unfold step 2 x', mismatch, 'x');
  reject(algebra + 'def g : (x : Flag) -> List Nat := fun (x : Flag) => fold prepend nil x',
    'the type has no instance of this structure', 'x');
});

test('the bodies example compiles', () => {
  const result = run(readFileSync(new URL('../examples/bodies.lang', import.meta.url), 'utf8'));
  assert.equal(result['sample-lang'], 1, JSON.stringify(result));
  assert.deepEqual(result.instances.map(instance => [instance.name, instance.value]),
    [['sevens', [7, 7]], ['four', 4], ['hi', [72, 105]], ['fives', [5, 5, 5]], ['six', 6]]);
});
