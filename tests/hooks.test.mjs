import test from 'node:test';
import assert from 'node:assert/strict';
import { execFileSync, spawnSync } from 'node:child_process';
import { copyFileSync, mkdirSync, mkdtempSync, readFileSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { fileURLToPath } from 'node:url';

const root = fileURLToPath(new URL('..', import.meta.url));

function run(script, input, options = {}) {
  return spawnSync('node', [join(root, script)], { cwd: options.cwd ?? root, input: JSON.stringify(input), encoding: 'utf8' });
}

test('PostToolUse audits a direct code path', () => {
  const result = run('.codex/hooks/post-tool-audit.mjs', { tool_name: 'Edit', tool_input: { file_path: 'src/main.ts' } });
  assert.match(result.stdout, /src\/main\.ts/u);
});

test('PostToolUse also audits a root document', () => {
  const result = run('.codex/hooks/post-tool-audit.mjs', { tool_name: 'Write', tool_input: { file_path: 'README.md' } });
  assert.match(result.stdout, /Documentation/u);
});

test('PostToolUse audits instructions and hooks', () => {
  const result = run('.codex/hooks/post-tool-audit.mjs', { tool_name: 'Write', tool_input: { file_path: '.codex/hooks.json' } });
  assert.match(result.stdout, /Instructions\/hooks/u);
});

test('PostToolUse reminds about documentation after a code change', () => {
  const result = run('.codex/hooks/post-tool-audit.mjs', { tool_name: 'Edit', tool_input: { file_path: 'src/engine.ts' } });
  assert.match(result.stdout, /Read relevant documentation/u);
  assert.match(result.stdout, /Update documentation/u);
});

test('PostToolUse audits documents and Mermaid diagrams', () => {
  const result = run('.codex/hooks/post-tool-audit.mjs', { tool_name: 'Edit', tool_input: { file_path: 'docs/architecture/runtime-loop.mmd' } });
  assert.match(result.stdout, /Mermaid/u);
});

test('an active Stop hook does not loop', () => {
  const result = run('.codex/hooks/stop-review.mjs', { stop_hook_active: true });
  assert.match(result.stdout, /continue/u);
});

test('Stop recognizes valid JSON', () => {
  const cwd = starterWorkspace();
  git(cwd, ['init', '-q']);
  git(cwd, ['config', 'user.email', 'fixture@example.invalid']);
  git(cwd, ['config', 'user.name', 'Fixture']);
  git(cwd, ['add', '.']);
  git(cwd, ['commit', '-qm', 'chore: fixture']);
  writeFileSync(join(cwd, 'change.json'), '{}\n');
  const result = run('.codex/hooks/stop-review.mjs', {}, { cwd });
  assert.doesNotMatch(result.stdout, /Syntax failures/u);
  assert.match(result.stdout, /JSON, JavaScript, and supported shell syntax checked/u);
});

test('commit-msg accepts Conventional Commits', () => {
  const directory = mkdtempSync(join(tmpdir(), 'hooks-test-'));
  const message = join(directory, 'message');
  writeFileSync(message, 'feat(core): add deterministic loop\n');
  const result = spawnSync('node', [join(root, '.githooks/validate-commit-message.mjs'), message], { encoding: 'utf8' });
  assert.equal(result.status, 0);
});

test('configuration rejects a missing npm script', () => {
  const cwd = starterWorkspace();
  const configPath = join(cwd, '.project/project-config.json');
  const config = JSON.parse(readFileSync(configPath, 'utf8'));
  config.commands.test = 'npm run absent';
  writeFileSync(configPath, JSON.stringify(config));
  const result = spawnSync('node', [join(root, '.githooks/validate-project-config.mjs')], { cwd, encoding: 'utf8' });
  assert.equal(result.status, 1);
  assert.match(result.stderr, /does not exist/u);
});

test('configuration rejects incomplete initialization', () => {
  const cwd = starterWorkspace();
  const configPath = join(cwd, '.project/project-config.json');
  const config = JSON.parse(readFileSync(configPath, 'utf8'));
  config.status = 'initialized';
  writeFileSync(configPath, JSON.stringify(config));
  const result = spawnSync('node', [join(root, '.githooks/validate-project-config.mjs')], { cwd, encoding: 'utf8' });
  assert.equal(result.status, 1);
  assert.match(result.stderr, /decisions\.language/u);
  assert.match(result.stderr, /directories\.source/u);
});

test('configuration rejects a missing declared starter file', () => {
  const cwd = starterWorkspace();
  const configPath = join(cwd, '.project/project-config.json');
  const config = JSON.parse(readFileSync(configPath, 'utf8'));
  config.starter.rootFiles.push('MISSING-STARTER.md');
  writeFileSync(configPath, JSON.stringify(config));
  const result = spawnSync('node', [join(root, '.githooks/validate-project-config.mjs')], { cwd, encoding: 'utf8' });
  assert.equal(result.status, 1);
  assert.match(result.stderr, /Missing starter root file: MISSING-STARTER\.md/u);
});

test('documentation rejects a broken local link', () => {
  const cwd = starterWorkspace();
  writeFileSync(join(cwd, 'docs/broken.md'), '[Document](missing.md)\n');
  const result = spawnSync('node', [join(root, '.githooks/validate-docs.mjs'), '--all'], { cwd, encoding: 'utf8' });
  assert.equal(result.status, 1);
  assert.match(result.stderr, /local link not found/u);
});

test('documentation rejects guides after initialization', () => {
  const cwd = initializedWorkspace();
  writeFileSync(join(cwd, 'docs/brief.md'), 'Decision: to be defined\n');
  const result = spawnSync('node', [join(root, '.githooks/validate-docs.mjs'), '--all'], { cwd, encoding: 'utf8' });
  assert.equal(result.status, 1);
  assert.match(result.stderr, /guide or placeholder/u);
});

test('invalid Mermaid is rejected', () => {
  const cwd = starterWorkspace();
  writeFileSync(join(cwd, 'docs/bad.md'), '# Diagram\n\n```mermaid\nflowchart TD\n  A -->\n```\n');
  const result = spawnSync('node', [join(root, '.githooks/validate-docs.mjs'), '--all'], { cwd, encoding: 'utf8' });
  assert.equal(result.status, 1);
  assert.match(result.stderr, /invalid Mermaid/u);
});

test('mutation configured for pre-commit is executed', () => {
  const cwd = starterWorkspace();
  const configPath = join(cwd, '.project/project-config.json');
  const config = JSON.parse(readFileSync(configPath, 'utf8'));
  config.commands.mutation = 'node -e "process.exit(7)"';
  config.quality.mutation.preCommit = true;
  writeFileSync(configPath, JSON.stringify(config));
  const result = spawnSync('node', [join(root, '.githooks/validate-push.mjs'), '--pre-commit'], { cwd, encoding: 'utf8' });
  assert.equal(result.status, 7);
});

test('disabled mutation remains skipped for trivial code', () => {
  const cwd = starterWorkspace();
  const configPath = join(cwd, '.project/project-config.json');
  const config = JSON.parse(readFileSync(configPath, 'utf8'));
  config.commands.mutation = 'node -e "process.exit(7)"';
  writeFileSync(configPath, JSON.stringify(config));
  const result = spawnSync('node', [join(root, '.githooks/validate-push.mjs'), '--pre-commit'], { cwd, encoding: 'utf8' });
  assert.equal(result.status, 0);
});

test('CTXRoute wiring validates and injects a matching project rule', () => {
  const validation = spawnSync('node', [join(root, '.githooks/validate-ctxroute.mjs')], { cwd: root, encoding: 'utf8' });
  assert.equal(validation.status, 0, validation.stderr);

  const session = `test-${process.pid}-${Date.now()}`;
  const result = spawnSync('node', [join(root, '.codex/hooks/ctxroute.mjs'), 'codex-doc-inject.js', '--budget', '0'], {
    cwd: root,
    input: JSON.stringify({ session_id: session, cwd: root, tool_name: 'apply_patch', tool_input: { patch: '*** Update File: package.json' } }),
    encoding: 'utf8',
  });
  assert.equal(result.status, 0, result.stderr);
  assert.match(result.stdout, /Project governance/u);
});

test('CTXRoute wrapper directs missing installations to project setup', () => {
  const cwd = mkdtempSync(join(tmpdir(), 'ctxroute-wrapper-'));
  const hookDirectory = join(cwd, '.codex/hooks');
  mkdirSync(hookDirectory, { recursive: true });
  const hook = join(hookDirectory, 'ctxroute.mjs');
  copyFileSync(join(root, '.codex/hooks/ctxroute.mjs'), hook);
  const result = spawnSync('node', [hook, 'session-inject.js'], { cwd, encoding: 'utf8' });
  assert.equal(result.status, 0);
  assert.match(result.stderr, /Run npm run setup/u);
});

test('setup prerequisite check is available before dependency installation', () => {
  const npm = process.platform === 'win32' ? 'npm.cmd' : 'npm';
  const result = spawnSync(npm, ['run', 'setup:check'], { cwd: root, encoding: 'utf8' });
  assert.equal(result.status, 0, result.stderr);
  assert.match(result.stdout, /Setup prerequisites are available/u);
});

function starterWorkspace() {
  const cwd = mkdtempSync(join(tmpdir(), 'starter-validation-'));
  const config = JSON.parse(readFileSync(join(root, '.project/project-config.json'), 'utf8'));
  for (const directory of [...config.starter.infrastructureRoots, '.project', 'docs/architecture']) mkdirSync(join(cwd, directory), { recursive: true });
  for (const file of config.starter.rootFiles) {
    if (file === 'package.json') continue;
    writeFileSync(join(cwd, file), file.endsWith('.json') ? '{}\n' : '');
  }
  writeFileSync(join(cwd, '.codex/architecture-policy.json'), JSON.stringify({ policyVersion: 1, projectConfig: '.project/project-config.json', supportedStatuses: ['template', 'initialized'] }));
  writeFileSync(join(cwd, '.project/project-config.json'), readFileSync(join(root, '.project/project-config.json')));
  writeFileSync(join(cwd, 'package.json'), JSON.stringify({ scripts: { test: 'node --test', 'validate:docs': 'node validate-docs.mjs' } }));
  return cwd;
}

function initializedWorkspace() {
  const cwd = starterWorkspace();
  const configPath = join(cwd, '.project/project-config.json');
  const config = JSON.parse(readFileSync(configPath, 'utf8'));
  config.status = 'initialized';
  for (const key of Object.keys(config.decisions)) config.decisions[key] = key === 'language' ? 'JavaScript' : 'none';
  config.directories.source = ['src/'];
  config.codeExtensions = ['.js'];
  config.quality.mutation.decision = 'not-applicable';
  writeFileSync(configPath, JSON.stringify(config));
  mkdirSync(join(cwd, 'src'));
  writeFileSync(join(cwd, 'docs/architecture/context.md'), '# Context\n\n```mermaid\nC4Context\n  Person(user, "User")\n  System(system, "System")\n  Rel(user, system, "Uses")\n```\n');
  writeFileSync(join(cwd, 'docs/architecture/containers.md'), '# Containers\n\n```mermaid\nC4Container\n  Person(user, "User")\n  Container(app, "App", "JavaScript")\n  Rel(user, app, "Uses")\n```\n');
  return cwd;
}

function git(cwd, args) {
  execFileSync('git', args, { cwd, stdio: 'ignore' });
}
