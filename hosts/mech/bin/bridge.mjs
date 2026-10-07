import { readFile } from 'node:fs/promises';

export const sourceLimit = 65_536;
export const outputLimit = 4 * 1024 * 1024;

// The bridge only transfers bytes. All language work runs in the reactor.
export async function createCompiler(path = new URL('../build/langc.wasm', import.meta.url)) {
  const module = await WebAssembly.compile(await readFile(path));
  if (WebAssembly.Module.imports(module).length !== 0) throw new Error('compiler reactor has unexpected imports');
  const { exports } = await WebAssembly.instantiate(module, {});
  for (const name of ['compile', 'emptyText', 'consText', 'textIsEnd', 'textHead', 'textTail']) {
    if (typeof exports[name] !== 'function') throw new Error(`compiler reactor is missing ${name}`);
  }
  return source => {
    if (!(source instanceof Uint8Array)) throw new TypeError('source must be UTF-8 bytes');
    if (source.length > sourceLimit) throw new Error(`source exceeds ${sourceLimit} bytes`);
    let input = exports.emptyText();
    for (let index = source.length - 1; index >= 0; index--) input = exports.consText(source[index], input);
    let output = exports.compile(input);
    const bytes = [];
    while (!exports.textIsEnd(output)) {
      if (bytes.length === outputLimit) throw new Error(`compiler output exceeds ${outputLimit} bytes`);
      const byte = exports.textHead(output);
      if (!Number.isInteger(byte) || byte < 0 || byte > 255) throw new Error('compiler emitted a non-byte');
      bytes.push(byte);
      output = exports.textTail(output);
    }
    return new TextDecoder('utf-8', { fatal: true }).decode(Uint8Array.from(bytes));
  };
}
