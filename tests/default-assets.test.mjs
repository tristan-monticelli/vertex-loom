import test from 'node:test';
import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { existsSync, readFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { alphaBounds, decodeRgba8Png } from './support/png-rgba.mjs';

const root = fileURLToPath(new URL('..', import.meta.url));
const manifestPath = join(root, 'editors/asset_studio/assets/manifest.json');
const manifest = JSON.parse(readFileSync(manifestPath, 'utf8'));

test('default asset manifest and PNG payloads are internally consistent', () => {
  assert.equal(manifest.schemaVersion, 1);
  assert.equal(manifest.assets.length, 3);
  assert.equal(new Set(manifest.assets.map(asset => asset.id)).size, 3);
  for (const asset of manifest.assets) {
    assert.match(asset.id, /^[a-z][a-z0-9-]*$/u);
    assert.match(asset.file, /^[a-z0-9-]+\.png$/u);
    assert.equal(asset.format, 'PNG/RGBA8');
    assert.equal(asset.width, 2048);
    assert.equal(asset.height, 2048);
    for (const field of ['authorOrSource', 'rightsHolder', 'license', 'attribution']) {
      assert.equal(typeof asset[field], 'string');
      assert.ok(asset[field].trim(), `${asset.id}.${field} must not be empty`);
    }
    const source = readFileSync(join(dirname(manifestPath), asset.file));
    assert.equal(createHash('sha256').update(source).digest('hex'), asset.sha256);
    const decoded = decodeRgba8Png(source);
    assert.deepEqual([decoded.width, decoded.height], [asset.width, asset.height]);
    const alpha = alphaBounds(decoded);
    assert.ok(alpha.count > 0, `${asset.id} must contain visible alpha`);
    assert.deepEqual(alpha.bounds, asset.expectedOpaqueBounds,
      `${asset.id} contains pixels outside its reviewed silhouette bounds`);
  }
});

test('public release requires written redistribution evidence', () => {
  if (process.env.VERTEX_LOOM_PUBLIC_RELEASE !== '1') return;
  for (const asset of manifest.assets) {
    assert.equal(asset.redistribution.status, 'approved', `${asset.id} rights are not approved`);
    assert.notEqual(asset.license, 'UNRESOLVED', `${asset.id} license is unresolved`);
    assert.ok(asset.redistribution.evidenceFile, `${asset.id} lacks an evidence file`);
    assert.ok(existsSync(join(dirname(manifestPath), asset.redistribution.evidenceFile)),
      `${asset.id} redistribution evidence is missing`);
  }
});
