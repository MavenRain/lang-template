import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import test from 'node:test';
import { run, reject } from './helper.mjs';

const schema = await readFile(new URL('../domain/schema.mech', import.meta.url), 'utf8');
const families = new Map();
for (const match of schema.matchAll(/^mu (\w+) : Type 0 with\n((?:\|[^\n]*\n?)+)/gm)) {
  const constructors = [...match[2].matchAll(/^\| (\w+) : (.+)$/gm)].map(([, name, signature]) => ({
    name,
    fields: [...signature.matchAll(/\((\w+) : (\w+)\)/g)].map(([, key, type]) => ({ key, type })),
    result: signature.split(' -> ').at(-1),
  }));
  assert.ok(constructors.length);
  for (const constructor of constructors) assert.equal(constructor.result, match[1]);
  families.set(match[1], constructors);
}
assert.equal(families.size, 3, 'the sample schema has three families');

function sample(type, chosen = families.get(type)?.[0]) {
  if (type === 'Text') return { expression: '"sample"', value: 'sample' };
  if (type === 'Nat') return { expression: '7', value: 7 };
  assert.ok(chosen, `sample for ${type}`);
  const fields = chosen.fields.map(field => ({ key: field.key, ...sample(field.type) }));
  return {
    expression: [chosen.name, ...fields.map(field => `(${field.expression})`)].join(' '),
    value: fields.length ? Object.fromEntries(fields.map(field => [field.key, field.value])) : chosen.name,
  };
}

for (const [family, constructors] of families) {
  for (const constructor of constructors) {
    test(`domain plan agrees with schema for ${constructor.name}`, () => {
      const expected = sample(family, constructor);
      assert.deepEqual(run(`def x : ${family} := ${expected.expression}`), {
        'sample-lang': 1,
        instances: [{ name: 'x', type: family, value: expected.value }],
      });
    });
  }
}

test('domain constructors check every argument and the result family', () => {
  for (const source of [
    'def x : Item := makeItem 1 measurePiece',
    'def x : Item := makeItem "x" flagYes',
    'def x : Stock := makeStock (makeItem "x" measureBox) "1"',
    'def x : Item := measurePiece',
    'def x : Measure := makeItem "x" measurePiece',
  ]) assert.ok(run(source).error, source);
});

test('domain family and constructor names are reserved', () => {
  for (const [family, constructors] of families) {
    for (const name of [family, ...constructors.map(constructor => constructor.name)]) {
      reject(`def ${name} : Nat := 1`, 'reserved definition name', name);
    }
  }
});
