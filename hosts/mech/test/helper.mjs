import assert from 'node:assert/strict';
import { createCompiler } from '../bin/bridge.mjs';

export const compile = await createCompiler();
export const encoder = new TextEncoder();
export const run = source => JSON.parse(compile(encoder.encode(source)));
export const reject = (source, message, at) => {
  const result = run(source);
  assert.deepEqual(Object.keys(result), ['error'], JSON.stringify(result));
  if (typeof message === 'string') assert.equal(result.error.message, message);
  else assert.match(result.error.message, message);
  assert.equal(result.error.byte, source.lastIndexOf(at));
};
