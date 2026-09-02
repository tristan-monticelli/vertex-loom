import { createHash } from 'node:crypto';
import { readdirSync, readFileSync, writeFileSync } from 'node:fs';
import { basename, join } from 'node:path';

const directory = process.argv[2];
if (!directory) throw new Error('usage: generate-release-metadata.mjs <artifact-directory>');
const archives = readdirSync(directory)
  .filter(file => /\.(?:zip|tar\.gz|tgz)$/u.test(file)).sort();
if (!archives.length) throw new Error('no release archive found');
const hashes = archives.map(file => {
  const sha256 = createHash('sha256').update(readFileSync(join(directory, file))).digest('hex');
  return { file: basename(file), sha256 };
});
writeFileSync(join(directory, 'SHA256SUMS'),
  `${hashes.map(item => `${item.sha256}  ${item.file}`).join('\n')}\n`);
const packageJson = JSON.parse(readFileSync('package.json', 'utf8'));
const components = [
  ['nlohmann-json', '3.11.3'], ['box2d', '3.1.1'], ['Catch2', '3.15.3'],
  ['SDL2', '2.32.10'], ['SDL2_image', '2.8.12'],
  ['nativefiledialog-extended', '1.3.0'], ['Dear ImGui', '1.92.9'],
].map(([name, version]) => ({ type: 'library', name, version }));
writeFileSync(join(directory, 'vertex-loom.sbom.cdx.json'), JSON.stringify({
  bomFormat: 'CycloneDX', specVersion: '1.5', version: 1,
  metadata: { component: { type: 'application', name: packageJson.name,
    version: packageJson.version } }, components,
  externalReferences: hashes.map(item => ({ type: 'distribution', url: item.file,
    hashes: [{ alg: 'SHA-256', content: item.sha256 }] })),
}, null, 2) + '\n');
