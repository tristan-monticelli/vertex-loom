import test from 'node:test';
import assert from 'node:assert/strict';
import { execFileSync, spawnSync } from 'node:child_process';
import { mkdirSync, mkdtempSync, readFileSync, writeFileSync } from 'node:fs';
import { join } from 'node:path';
import { tmpdir } from 'node:os';
import { fileURLToPath } from 'node:url';

const root = fileURLToPath(new URL('..', import.meta.url));
const validator = join(root, '.githooks/validate-architecture.mjs');
const docsValidator = join(root, '.githooks/validate-docs.mjs');

test('Git blocks a new module without a diagram in the same change', () => {
  const cwd = repository();
  writeFileSync(join(cwd, 'src/service.rb'), 'class Service\nend\n');
  git(cwd, ['add', 'src/service.rb']);
  const blocked = spawnSync('node', [validator], { cwd, encoding: 'utf8' });
  assert.equal(blocked.status, 1);
  assert.match(blocked.stderr, /Architecture diagram required/u);

  writeFileSync(join(cwd, 'docs/architecture/containers.md'), `${readFileSync(join(cwd, 'docs/architecture/containers.md'), 'utf8')}\nService responsibility.\n`);
  git(cwd, ['add', 'docs/architecture/containers.md']);
  const allowed = spawnSync('node', [validator], { cwd, encoding: 'utf8' });
  assert.equal(allowed.status, 0);
});

test('Git blocks a contract without an ADR then accepts the associated ADR', () => {
  const cwd = repository();
  writeFileSync(join(cwd, 'package.json'), JSON.stringify({ private: true, dependencies: { example: '1.0.0' } }));
  git(cwd, ['add', 'package.json']);
  const blocked = spawnSync('node', [validator], { cwd, encoding: 'utf8' });
  assert.equal(blocked.status, 1);
  assert.match(blocked.stderr, /ADR required/u);

  writeFileSync(join(cwd, 'docs/decisions/ADR-0001-example.md'), '# ADR-0001 — Example\n');
  git(cwd, ['add', 'docs/decisions/ADR-0001-example.md']);
  const allowed = spawnSync('node', [validator], { cwd, encoding: 'utf8' });
  assert.equal(allowed.status, 0);
});

test('the full audit blocks code outside declared roots', () => {
  const cwd = repository();
  writeFileSync(join(cwd, 'application.rb'), 'puts :outside\n');
  const result = spawnSync('node', [validator, '--all'], { cwd, encoding: 'utf8' });
  assert.equal(result.status, 1);
  assert.match(result.stderr, /outside declared source/u);
});

test('the full audit ignores files excluded by Git', () => {
  const cwd = repository();
  writeFileSync(join(cwd, '.DS_Store'), 'ignored metadata\n');
  const result = spawnSync('node', [validator, '--all'], { cwd, encoding: 'utf8' });
  assert.equal(result.status, 0, result.stderr);
});

test('the documentation audit ignores files excluded by Git', () => {
  const cwd = repository();
  writeFileSync(join(cwd, '.gitignore'), '.DS_Store\ndocs/generated.md\n');
  writeFileSync(join(cwd, 'docs/generated.md'), '# Generated\n\n```mermaid\nflowchart TD\n  A -->\n```\n');
  git(cwd, ['add', '.gitignore']);
  const result = spawnSync('node', [docsValidator, '--all'], { cwd, encoding: 'utf8' });
  assert.equal(result.status, 0, result.stderr);
});

function repository() {
  const cwd = mkdtempSync(join(tmpdir(), 'git-architecture-'));
  for (const directory of ['.codex', '.project', 'docs/architecture', 'docs/decisions', 'src', 'tests']) mkdirSync(join(cwd, directory), { recursive: true });
  writeFileSync(join(cwd, '.codex/architecture-policy.json'), JSON.stringify({ policyVersion: 1, projectConfig: '.project/project-config.json', supportedStatuses: ['template', 'initialized'] }));
  const config = JSON.parse(readFileSync(join(root, '.project/project-config.json'), 'utf8'));
  config.status = 'initialized';
  for (const key of Object.keys(config.decisions)) config.decisions[key] = key === 'language' ? 'Ruby' : 'none';
  config.directories.source = ['src/'];
  config.codeExtensions = ['.rb'];
  config.quality.mutation.decision = 'not-applicable';
  writeFileSync(join(cwd, '.project/project-config.json'), JSON.stringify(config));
  writeFileSync(join(cwd, 'docs/architecture/context.md'), '# Context\n\n```mermaid\nC4Context\n  Person(user, "User")\n  System(system, "System")\n  Rel(user, system, "Uses")\n```\n');
  writeFileSync(join(cwd, 'docs/architecture/containers.md'), '# Containers\n\n```mermaid\nC4Container\n  Person(user, "User")\n  Container(app, "App", "Ruby")\n  Rel(user, app, "Uses")\n```\n');
  writeFileSync(join(cwd, 'package.json'), JSON.stringify({ private: true }));
  writeFileSync(join(cwd, '.gitignore'), '.DS_Store\n');
  git(cwd, ['init', '-q']);
  git(cwd, ['config', 'user.email', 'fixture@example.invalid']);
  git(cwd, ['config', 'user.name', 'Fixture']);
  git(cwd, ['add', '.']);
  git(cwd, ['commit', '-qm', 'chore: fixture']);
  return cwd;
}

function git(cwd, args) {
  execFileSync('git', args, { cwd, stdio: 'ignore' });
}
