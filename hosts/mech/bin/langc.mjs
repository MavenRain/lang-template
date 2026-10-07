#!/usr/bin/env node
import { spawnSync } from 'node:child_process';
import { open } from 'node:fs/promises';
import { fileURLToPath } from 'node:url';
import { createCompiler, sourceLimit } from './bridge.mjs';

// A 65,536-byte source needs more V8 stack than the default gives.
const nodeFlags = ['--stack-size=7000', '--max-old-space-size=1024'];

async function inputBytes(path) {
  const handle = await open(path, 'r');
  try {
    const bytes = new Uint8Array(sourceLimit + 1);
    let size = 0;
    while (size < bytes.length) {
      const result = await handle.read(bytes, size, bytes.length - size, null);
      if (result.bytesRead === 0) break;
      size += result.bytesRead;
    }
    if (size > sourceLimit) throw new Error(`source exceeds ${sourceLimit} bytes`);
    return bytes.subarray(0, size);
  } finally {
    await handle.close();
  }
}

async function main(path) {
  const source = await inputBytes(path);
  const compile = await createCompiler();
  const output = compile(source);
  const document = JSON.parse(output);
  if (document.error) {
    console.error(`${path}: byte ${document.error.byte}: ${document.error.message}`);
    process.exitCode = 1;
    return;
  }
  process.stdout.write(output + '\n');
}

// Run directly (`node bin/langc.mjs`), the launcher starts again with nodeFlags.
function relaunch(args) {
  const result = spawnSync(process.execPath, [...nodeFlags, fileURLToPath(import.meta.url), ...args], { stdio: 'inherit' });
  if (result.error) console.error(result.error.message);
  process.exitCode = result.status ?? 1;
}

const args = process.argv.slice(2);
if (args.length !== 1 || args[0].startsWith('--')) {
  console.error('usage: bin/langc PROGRAM.lang');
  process.exitCode = 1;
} else if (!process.execArgv.some(flag => flag.startsWith('--stack-size'))) {
  relaunch(args);
} else {
  main(args[0]).catch(error => { console.error(`${args[0]}: ${error.message}`); process.exitCode = 1; });
}
