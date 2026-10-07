import assert from 'node:assert/strict';
import { spawnSync } from 'node:child_process';
import { cp, mkdtemp, readFile, rm, writeFile } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import test from 'node:test';
import { createCompiler } from '../bin/bridge.mjs';

test('a replacement domain name is quoted as a JSON key', async () => {
  const root = await mkdtemp(join(tmpdir(), 'lang-header-'));
  try {
    for (const path of ['bin', 'compiler', 'domain', 'prelude.mech']) {
      await cp(new URL('../' + path, import.meta.url), join(root, path), { recursive: true });
    }
    const name = 'kit "quoted" \\ \n café';
    const bytes = new TextEncoder().encode(name);
    const literal = [...bytes].reduceRight((tail, byte) => `textByte ${byte} (${tail})`, 'textEnd');
    const path = join(root, 'domain/schema.mech');
    const schema = await readFile(path, 'utf8');
    assert.equal((schema.match(/^def domainName : Text := .+$/gm) || []).length, 1);
    await writeFile(path, schema.replace(/^def domainName : Text := .+$/m, `def domainName : Text := ${literal}`));
    const built = spawnSync(process.execPath, [join(root, 'bin/build.mjs')], {
      encoding: 'utf8', timeout: 120_000,
    });
    assert.equal(built.status, 0, built.error?.message || built.stderr);
    const compile = await createCompiler(join(root, 'build/langc.wasm'));
    assert.deepEqual(JSON.parse(compile(new Uint8Array())), { [name]: 1, instances: [] });
  } finally {
    await rm(root, { recursive: true, force: true });
  }
});
