import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import test from 'node:test';
import { parseSchema, renderPlans } from '../bin/gen-domain.mjs';
import { run, reject } from './helper.mjs';

const schema = await readFile(new URL('../domain/schema.mech', import.meta.url), 'utf8');
const plans = await readFile(new URL('../domain/plans.mech', import.meta.url), 'utf8');
const families = new Map(parseSchema(schema).families.map(family => [family.name, family]));
assert.equal(families.size, 3, 'the sample schema has three families');

test('domain/plans.mech is generated from domain/schema.mech', () => {
  assert.equal(plans, renderPlans(parseSchema(schema)));
});

// A source expression and its JSON value for a field type or a constructor.
function sample(type, family = families.get(type.name), chosen = family?.constructors[0]) {
  if (type.former === 'Option') {
    const inner = sample(type.inner);
    return { expression: `some (${inner.expression})`, value: inner.value };
  }
  if (type.former === 'List') {
    const inner = sample(type.inner);
    return { expression: `cons (${inner.expression}) nil`, value: [inner.value] };
  }
  if (type.name === 'Text') return { expression: '"sample"', value: 'sample' };
  if (type.name === 'Nat') return { expression: '7', value: 7 };
  if (type.name === 'Flag') return { expression: 'flagYes', value: true };
  assert.ok(chosen, `sample for ${type.name}`);
  const fields = chosen.fields.map(field => ({ key: field.key, ...sample(field.type) }));
  const record = Object.fromEntries(fields.map(field => [field.key, field.value]));
  return {
    expression: [chosen.name, ...fields.map(field => `(${field.expression})`)].join(' '),
    value: family.kind === 'enum' ? chosen.name : family.kind === 'tagged' ? { tag: chosen.name, ...record } : record,
  };
}

families.forEach(family => family.constructors.forEach(constructor => {
  test(`domain plan agrees with schema for ${constructor.name}`, () => {
    const expected = sample({ name: family.name }, family, constructor);
    assert.deepEqual(run(`def x : ${family.name} := ${expected.expression}`), {
      'sample-lang': 1,
      instances: [{ name: 'x', type: family.name, value: expected.value }],
    });
  });
}));

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
  families.forEach(family => [family.name, ...family.constructors.map(constructor => constructor.name)].forEach(name => {
    reject(`def ${name} : Nat := 1`, 'reserved definition name', name);
  }));
});
