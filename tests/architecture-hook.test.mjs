import test from 'node:test';
import assert from 'node:assert/strict';
import { execFileSync, spawnSync } from 'node:child_process';
import { mkdirSync, mkdtempSync, readFileSync, writeFileSync } from 'node:fs';
import { join } from 'node:path';
import { tmpdir } from 'node:os';
import { fileURLToPath } from 'node:url';

const root = fileURLToPath(new URL('..', import.meta.url));
const hook = join(root, '.codex/hooks/pre-tool-architecture.mjs');

function run(tool_input, options = {}) {
  return spawnSync('node', [hook], {
    cwd: options.cwd ?? root,
    input: JSON.stringify({ tool_name: options.toolName ?? 'apply_patch', tool_input }),
    encoding: 'utf8',
  });
}

test('blocks a new product file without C4 evidence', () => {
  const result = run({ command: '*** Add File: src/new-module.ts' });
  assert.match(result.stdout, /Write blocked/u);
});

test('allows a new test file', () => {
  const result = run({ command: '*** Add File: tests/new-module.test.ts' });
  assert.equal(result.stdout, '');
});

test('blocks a new module during discovery even with a diagram', () => {
  const result = run({ command: '*** Add File: src/new-module.ts\n*** Update File: docs/architecture/containers.md' });
  assert.match(result.stdout, /template mode/u);
});

test('does not block an existing hook', () => {
  const result = run({ file_path: '.codex/hooks/pre-tool-architecture.mjs' });
  assert.equal(result.stdout, '');
});

test('does not block a new infrastructure hook', () => {
  const result = run({ command: '*** Add File: .githooks/new-check.mjs' });
  assert.equal(result.stdout, '');
});

test('blocks a new dependency without architecture documentation', () => {
  const result = run({ file_path: 'package.json' }, { cwd: initializedWorkspace() });
  assert.match(result.stdout, /Write blocked/u);
});

test('reads patches supplied through a patch property', () => {
  const result = run({ patch: '*** Add File: application.rb' });
  assert.match(result.stdout, /template mode/u);
});

test('blocks a shell write command during discovery', () => {
  const result = run({ cmd: 'touch application.rb' }, { toolName: 'exec_command' });
  assert.match(result.stdout, /read and validation commands/u);
});

test('allows a validation command during discovery', () => {
  const result = run({ cmd: 'npm test' }, { toolName: 'exec_command' });
  assert.doesNotMatch(result.stdout, /decision":"block/u);
});

test('allows the documented template setup commands during discovery', () => {
  for (const cmd of ['npm run setup:check', 'npm run setup']) {
    const result = run({ cmd }, { toolName: 'exec_command' });
    assert.doesNotMatch(result.stdout, /decision":"block/u, cmd);
  }
});

test('allows a new module with C4 evidence after initialization', () => {
  const cwd = initializedWorkspace();
  const result = run({ patch: '*** Add File: src/service.rb\n*** Update File: docs/architecture/containers.md' }, { cwd });
  assert.doesNotMatch(result.stdout, /decision":"block/u);
});

test('blocks code outside declared directories after initialization', () => {
  const cwd = initializedWorkspace();
  const result = run({ patch: '*** Add File: application.rb\n*** Update File: docs/architecture/containers.md' }, { cwd });
  assert.match(result.stdout, /outside declared directories/u);
});

test('blocks direct shell writes after initialization', () => {
  const cwd = initializedWorkspace();
  const result = run({ cmd: 'touch src/service.rb' }, { cwd, toolName: 'exec_command' });
  assert.match(result.stdout, /traceable editing tool/u);
});

test('allows a contract with a real ADR', () => {
  const cwd = initializedWorkspace();
  const result = run({ patch: '*** Update File: package.json\n*** Add File: docs/decisions/ADR-0001-dependency.md' }, { cwd });
  assert.equal(result.stdout, '');
});

test('recognizes a C4 document changed in a previous step', () => {
  const cwd = initializedWorkspace();
  git(cwd, ['init', '-q']);
  git(cwd, ['config', 'user.email', 'fixture@example.invalid']);
  git(cwd, ['config', 'user.name', 'Fixture']);
  git(cwd, ['add', '.']);
  git(cwd, ['commit', '-qm', 'chore: fixture']);
  writeFileSync(join(cwd, 'docs/architecture/containers.md'), '# Containers\n\nNew service.\n');
  const result = run({ patch: '*** Add File: src/service.rb' }, { cwd });
  assert.doesNotMatch(result.stdout, /decision":"block/u);
});

test('fails closed when configuration is invalid', () => {
  const cwd = initializedWorkspace();
  writeFileSync(join(cwd, '.project/project-config.json'), '{');
  const result = run({ patch: '*** Add File: src/service.rb' }, { cwd });
  assert.match(result.stdout, /invalid project configuration/u);
});

test('allows only invalid configuration repair', () => {
  const cwd = initializedWorkspace();
  writeFileSync(join(cwd, '.project/project-config.json'), '{');
  const result = run({ patch: '*** Update File: .project/project-config.json' }, { cwd });
  assert.doesNotMatch(result.stdout, /decision":"block/u);
});

function initializedWorkspace() {
  const cwd = mkdtempSync(join(tmpdir(), 'architecture-hook-'));
  for (const directory of ['.codex', '.project', 'docs/architecture', 'src']) mkdirSync(join(cwd, directory), { recursive: true });
  writeFileSync(join(cwd, '.codex/architecture-policy.json'), JSON.stringify({ policyVersion: 1, projectConfig: '.project/project-config.json', supportedStatuses: ['template', 'initialized'] }));
  const config = JSON.parse(readFileSync(join(root, '.project/project-config.json'), 'utf8'));
  config.status = 'initialized';
  for (const key of Object.keys(config.decisions)) config.decisions[key] = key === 'language' ? 'Ruby' : 'none';
  config.directories.source = ['src/'];
  config.codeExtensions = ['.rb'];
  config.quality.mutation.decision = 'not-applicable';
  writeFileSync(join(cwd, '.project/project-config.json'), JSON.stringify(config));
  writeFileSync(join(cwd, 'docs/architecture/context.md'), '# Context\n');
  writeFileSync(join(cwd, 'docs/architecture/containers.md'), '# Containers\n');
  return cwd;
}

function git(cwd, args) {
  execFileSync('git', args, { cwd, stdio: 'ignore' });
}
