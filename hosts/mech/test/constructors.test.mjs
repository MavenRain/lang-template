import assert from 'node:assert/strict';
import test from 'node:test';
import { run } from './helper.mjs';

for (const [type, expression, value] of [
  ['Option (Nat)', 'some 1', 1],
  ['Option (Option (Nat))', 'some none', { some: null }],
  ['Value', 'valueFlag flagYes', true],
  ['Value', 'valueNat 1', 1],
  ['Value', 'valueText "x"', 'x'],
  ['Value', 'valueItems (valuesItem valueNull valuesEnd)', [null]],
  ['Value', 'valueAttrs (attrsField "x" (valueNat 1) attrsEnd)', { x: 1 }],
]) {
  test(`construct ${type} with ${expression}`, () => {
    assert.deepEqual(run(`def x : ${type} := ${expression}`), {
      'sample-lang': 1,
      instances: [{ name: 'x', type, value }],
    });
  });
}
