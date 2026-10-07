#!/usr/bin/env node
import { readFile, writeFile } from 'node:fs/promises';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';

// Generates domain/plans.mech from the mu declarations in domain/schema.mech.
const root = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const schemaPath = join(root, 'domain/schema.mech');
const plansPath = join(root, 'domain/plans.mech');

const coreTypes = new Map([
  ['Nat', 'tyNat'], ['Text', 'tyText'], ['Flag', 'tyFlag'],
  ['Value', 'tyValue'], ['Values', 'tyValues'], ['Attrs', 'tyAttrs'],
]);
const fixedNames = ['domainName', 'domainType', 'domainConstructorPlan', 'domainReservedName'];
const headPattern = /^(?:mu|and) (\w+) : Type 0 with$/;
const constructorPattern = /^\| (\w+) : (.+)$/;
const fieldPattern = /^\((\w+) : (.+)\)$/;

const fail = message => { throw new Error(`domain/schema.mech: ${message}`); };

// A UTF-8 byte chain of textByte and textEnd.
export const textChain = text =>
  [...new TextEncoder().encode(text)].reduceRight((rest, byte) => `textByte ${byte} (${rest})`, 'textEnd');

const textName = name => `domainText${name}`;
const planName = family => `domain${family}Plan`;

// Field types: a core carrier, a domain family, or Option or List of a field type.
function parseType(tokens, where) {
  const [head, ...rest] = tokens;
  if (head === 'Option' || head === 'List') {
    const [inner, remaining] = parseArgument(rest, where);
    return [{ former: head, inner }, remaining];
  }
  return parseArgument(tokens, where);
}

function parseArgument(tokens, where) {
  const [head, ...rest] = tokens;
  if (head === '(') {
    const [inner, remaining] = parseType(rest, where);
    if (remaining[0] !== ')') fail(`${where}: unbalanced parentheses`);
    return [inner, remaining.slice(1)];
  }
  if (head === undefined || head === ')' || head === 'Option' || head === 'List') fail(`${where}: incomplete type`);
  return [{ name: head }, rest];
}

function fieldType(text, where) {
  const [type, remaining] = parseType(text.match(/[()]|[^\s()]+/g) ?? [], where);
  if (remaining.length) fail(`${where}: unexpected ${remaining[0]}`);
  return type;
}

function parseConstructor(family, line) {
  const [, name, signature] = line.match(constructorPattern) ?? fail(`constructor line "${line}" must have the form | name : signature`);
  const parts = signature.split(' -> ');
  const result = parts.at(-1);
  if (result !== family) fail(`constructor ${name} must return ${family}, not ${result}`);
  const fields = parts.slice(0, -1).map((part, index) => {
    const [, key, type] = part.match(fieldPattern)
      ?? fail(`field ${index + 1} of ${name} must have the form (name : Type)`);
    return { key, type: fieldType(type, `field ${key} of ${name}`) };
  });
  return { name, fields };
}

// Lines outside a family block (definitions, comments) are left to mechanism-lang.
function collectFamilies(text) {
  const lines = text.split('\n').map(line => line.replace(/--.*$/, '').trim());
  return lines.reduce(({ families, current }, line) => {
    if (!line) return { families, current };
    const head = line.match(headPattern);
    if (head) return { families: [...families, { name: head[1], lines: [] }], current: families.length };
    if (/^(?:mu|and)\b/.test(line)) fail(`family head "${line}" must have the form mu Name : Type 0 with`);
    if (line.startsWith('|')) {
      if (current === undefined) fail(`constructor line "${line}" is outside a family`);
      return {
        families: families.map((family, index) => index === current ? { ...family, lines: [...family.lines, line] } : family),
        current,
      };
    }
    return { families, current: undefined };
  }, { families: [], current: undefined }).families;
}

function duplicates(names) {
  return names.filter((name, index) => names.indexOf(name) !== index);
}

function typeNames(type) {
  return type.former ? typeNames(type.inner) : [type.name];
}

export function parseSchema(text) {
  const families = collectFamilies(text).map(({ name, lines }) => {
    if (!lines.length) fail(`family ${name} has no constructors`);
    const constructors = lines.map(line => parseConstructor(name, line));
    const kind = constructors.every(constructor => !constructor.fields.length) ? 'enum'
      : constructors.length === 1 ? 'record' : 'tagged';
    return { name, kind, constructors };
  });
  if (!families.length) fail('declare at least one family with mu Name : Type 0 with');
  const familyNames = families.map(family => family.name);
  const constructors = families.flatMap(family => family.constructors.map(constructor => ({ family, constructor })));
  const repeated = duplicates([...familyNames, ...constructors.map(({ constructor }) => constructor.name)]);
  if (repeated.length) fail(`name ${repeated[0]} is declared twice`);
  constructors.forEach(({ family, constructor }) => {
    const keys = constructor.fields.map(field => field.key);
    if (duplicates(keys).length) fail(`constructor ${constructor.name} repeats field ${duplicates(keys)[0]}`);
    if (family.kind === 'tagged' && keys.includes('tag')) fail(`constructor ${constructor.name} of tagged family ${family.name} cannot use the field name tag`);
    constructor.fields.flatMap(field => typeNames(field.type)).forEach(name => {
      if (!coreTypes.has(name) && !familyNames.includes(name)) fail(`constructor ${constructor.name} uses unknown type ${name}`);
    });
  });
  const generated = [
    ...fixedNames, ...familyNames.map(planName),
    ...[...new Set(textNames({ families }))].map(textName),
  ];
  if (duplicates(generated).length) fail(`generated definition ${duplicates(generated)[0]} is not unique; rename a family, constructor or field`);
  return { families };
}

// Families, then constructors, then field names, each once in declaration order.
function textNames({ families }) {
  const constructors = families.flatMap(family => family.constructors);
  return [
    ...families.map(family => family.name),
    ...constructors.map(constructor => constructor.name),
    ...constructors.flatMap(constructor => constructor.fields.map(field => field.key)),
  ];
}

function renderType(type) {
  if (type.former === 'Option') return `(tyOption ${renderType(type.inner)})`;
  if (type.former === 'List') return `(tyList ${renderType(type.inner)})`;
  return coreTypes.get(type.name) ?? `(tyNamed ${textName(type.name)})`;
}

const renderList = items => items.reduceRight((rest, item) => `(cons ${item} ${rest})`, 'nil');

function renderPlan(family, constructor) {
  if (family.kind === 'enum') return `some (plan nil 14 ${textName(constructor.name)})`;
  const tag = family.kind === 'tagged' ? `(some ${textName(constructor.name)})` : 'none';
  const types = renderList(constructor.fields.map(field => renderType(field.type)));
  const keys = renderList(constructor.fields.map(field => textName(field.key)));
  return `some (recordPlan ${types} ${keys} ${tag})`;
}

// A chain of text comparisons on subject. Each entry is [text definition, result].
function renderLookup(subject, result, entries, fallback) {
  const chain = entries.reduceRight((rest, [key, value]) =>
    `(case (natEq (sameText ${subject} ${key}) 1) as matched return ${result} with\n`
    + `    | 1 (x : prod ()) => ${value}\n`
    + `    | 0 (x : prod ()) => ${rest})`, fallback);
  return `  ${chain}`;
}

export function renderPlans({ families }) {
  const header = [
    '-- Generated by bin/gen-domain.mjs from domain/schema.mech. Do not edit.',
    '-- Run make plans after a schema change. The core calls only',
    '-- domainConstructorPlan, domainType and domainReservedName (domain/README.md).',
  ].join('\n');
  const texts = [...new Set(textNames({ families }))]
    .map(name => `def ${textName(name)} : Text := ${textChain(name)}`).join('\n');
  const plans = families.map(family => `def ${planName(family.name)} : Text -> Option Plan := fun (name : Text) =>\n`
    + renderLookup('name', 'Option Plan',
      family.constructors.map(constructor => [textName(constructor.name), renderPlan(family, constructor)]), 'none'));
  const constructorPlan = 'def domainConstructorPlan : Text -> Text -> Option Plan := fun (family : Text) (name : Text) =>\n'
    + renderLookup('family', 'Option Plan',
      families.map(family => [textName(family.name), `${planName(family.name)} name`]), 'none');
  const domainType = 'def domainType : Text -> Option LType := fun (name : Text) =>\n'
    + renderLookup('name', 'Option LType',
      families.map(family => [textName(family.name), `some (tyNamed ${textName(family.name)})`]), 'none');
  const reserved = 'def domainReservedName : Text -> Nat := fun (name : Text) =>\n'
    + renderLookup('name', 'Nat', [
      ...families.map(family => [textName(family.name), '1']),
      ...families.flatMap(family => family.constructors.map(constructor => [textName(constructor.name), '1'])),
    ], '0');
  return `${[header, texts, ...plans, constructorPlan, domainType, reserved].join('\n\n')}\n`;
}

async function main() {
  const args = process.argv.slice(2);
  if (args.length > 1 || (args.length === 1 && args[0] !== '--check')) {
    throw new Error('usage: node bin/gen-domain.mjs [--check]');
  }
  const plans = renderPlans(parseSchema(await readFile(schemaPath, 'utf8')));
  if (args[0] === '--check') {
    const current = await readFile(plansPath, 'utf8');
    if (current !== plans) throw new Error('domain/plans.mech does not match domain/schema.mech; run make plans');
    console.log('domain plans match domain/schema.mech');
  } else {
    await writeFile(plansPath, plans);
    console.log('wrote domain/plans.mech');
  }
}

if (process.argv[1] && import.meta.url === pathToFileURL(resolve(process.argv[1])).href) {
  main().catch(error => { console.error(error.message); process.exitCode = 1; });
}
