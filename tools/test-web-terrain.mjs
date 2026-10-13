#!/usr/bin/env node
// Browser-startup contract: terrain gates main, and failed downloads stay gated.
import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';
import vm from 'node:vm';

const source = await readFile(new URL('../moppe/platform/web/pre.js', import.meta.url), 'utf8');
async function startup(mode) {
  const dependencies = new Set();
  const written = new Map();
  const requested = [];
  const note = {};
  const context = {
    Module: {terrainManifest: '../terrain/terrain.json'},
    ENV: {}, URL, URLSearchParams, Uint8Array,
    location: {search: '', href: 'https://forge.test/artifacts/REV/web/index.html'},
    matchMedia: () => ({matches: false}),
    console: {error() {}},
    document: {getElementById: () => note},
    navigator: {gpu: {requestAdapter: async () => ({
      features: new Set(['float32-filterable']), limits: {},
      requestDevice: async () => ({addEventListener() {}, lost: new Promise(() => {})}),
    })}},
    FS: {mkdirTree() {}, writeFile: (path, bytes) => written.set(path, bytes)},
    addRunDependency: name => dependencies.add(name),
    removeRunDependency: name => dependencies.delete(name),
    fetch: async url => {
      requested.push(String(url));
      return {
        ok: mode !== 'missing', status: mode === 'missing' ? 404 : 200,
        json: async () => ({files: [mode === 'invalid' ? '../secret' : 'land-v3-play-1024-123-test.arrows']}),
        arrayBuffer: async () => Uint8Array.of(1, 2, 3).buffer,
      };
    },
  };
  vm.runInNewContext(source, context);
  context.Module.preRun[0]();
  assert(dependencies.has('terrain'));
  await new Promise(resolve => setImmediate(resolve));
  return {dependencies, written, requested, note};
}
const success = await startup('ok');
assert(!success.dependencies.has('terrain'));
assert.equal(success.written.size, 1);
assert.equal(success.requested[1], 'https://forge.test/artifacts/REV/terrain/land-v3-play-1024-123-test.arrows');
for (const mode of ['missing', 'invalid']) {
  const result = await startup(mode);
  assert(result.dependencies.has('terrain'));
  assert.equal(result.written.size, 0);
  assert(result.note.textContent);
}
console.log('Terrain startup: success, missing artifact and invalid manifest passed.');
