#!/usr/bin/env node
import { spawnSync } from 'node:child_process';
import { mkdtemp, readFile, rename, mkdir, rm, writeFile } from 'node:fs/promises';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

export const root = resolve(dirname(fileURLToPath(import.meta.url)), '..');
export const sources = [
  'prelude.mech', 'domain/schema.mech',
  'compiler/runtime.mech', 'compiler/literals.mech', 'compiler/lexer.mech',
  'domain/plans.mech', 'compiler/types.mech', 'compiler/evaluate.mech', 'compiler/parser.mech',
  'compiler/checker.mech', 'compiler/json.mech', 'compiler/program.mech',
];
const exports = ['compile', 'emptyText', 'consText', 'textIsEnd', 'textHead', 'textTail'];

async function main() {
  const args = process.argv.slice(2);
  if (args.length > 1 || (args.length === 1 && args[0] !== '--check')) {
    throw new Error('usage: node bin/build.mjs [--check]');
  }
  const check = args[0] === '--check';
  const mech = process.env.MECH_BIN || 'mech';
  // The temporary directory sits on the same file system as build/, so rename works.
  await mkdir(join(root, 'build'), { recursive: true });
  const directory = await mkdtemp(join(root, 'build', 'tmp-'));
  try {
    const absoluteSources = sources.map(path => join(root, path));
    let command;
    if (check) {
      const combined = join(directory, 'compiler.mech');
      await writeFile(combined, (await Promise.all(absoluteSources.map(path => readFile(path, 'utf8')))).join('\n'));
      command = ['check', combined];
    } else {
      command = ['build', ...absoluteSources, '-o', join(directory, 'langc.wasm'),
        ...exports.flatMap(name => ['--export', name])];
    }
    const result = spawnSync(mech, command, { encoding: 'utf8', timeout: 120_000, maxBuffer: 1024 * 1024 });
    if (result.error) throw result.error;
    if (result.status !== 0) throw new Error(result.stderr.trim() || `mechanism-lang exited ${result.status}`);
    if (!check) await rename(join(directory, 'langc.wasm'), join(root, 'build/langc.wasm'));
    console.log(check ? 'compiler check passed' : 'built build/langc.wasm');
  } finally {
    await rm(directory, { recursive: true, force: true });
  }
}

main().catch(error => { console.error(error.message); process.exitCode = 1; });
