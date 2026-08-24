import { spawnSync } from 'node:child_process';
import { existsSync, readFileSync, readdirSync } from 'node:fs';
import { join, resolve } from 'node:path';

const root = process.cwd();
const commit = 'a2a3f372c4426f8e1286a6fbb0cb33bdbc374145';
const failures = [];
const packageJson = readJson('package.json');
const packageLock = readJson('package-lock.json');
const hooks = readJson('.codex/hooks.json');
const config = readJson('ctxroute-config.json');
const installedPackage = readJson('node_modules/ctxroute/package.json');

const archiveUrl = `https://github.com/zenonlab/ctxroute/archive/${commit}.tar.gz`;
if (packageJson?.devDependencies?.ctxroute !== archiveUrl) {
  failures.push('package.json must pin CTXRoute to the reviewed HTTPS archive');
}

const lockEntry = packageLock?.packages?.['node_modules/ctxroute'];
if (lockEntry?.resolved !== archiveUrl) {
  failures.push('package-lock.json must resolve the pinned CTXRoute HTTPS archive');
}

if (installedPackage?.version !== '2.0.0') failures.push('CTXRoute 2.0.0 is not installed');

const expected = new Map([
  ['SessionStart', new Map([['session-inject.js', 'node ./.codex/hooks/ctxroute.mjs session-inject.js --budget 0']])],
  ['PreToolUse', new Map([['codex-doc-inject.js', 'node ./.codex/hooks/ctxroute.mjs codex-doc-inject.js --budget 0']])],
  ['PostToolUse', new Map([['codex-doc-write-guard.js', 'node ./.codex/hooks/ctxroute.mjs codex-doc-write-guard.js']])],
  ['UserPromptSubmit', new Map([
    ['turn-count.js', 'node ./.codex/hooks/ctxroute.mjs turn-count.js'],
    ['canary-check.js', 'node ./.codex/hooks/ctxroute.mjs canary-check.js'],
  ])],
  ['PreCompact', new Map([['ctxroute-reset.js', 'node ./.codex/hooks/ctxroute.mjs ctxroute-reset.js']])],
]);

for (const [event, entries] of expected) {
  const commands = (hooks?.hooks?.[event] ?? []).flatMap(block => block.hooks ?? []).map(hook => hook.command ?? '');
  for (const [name, expectedCommand] of entries) {
    const matching = commands.filter(command => command === expectedCommand);
    if (matching.length !== 1) failures.push(`${event}: expected exactly one portable CTXRoute hook ${name}`);
  }
}

for (const event of ['SessionStart', 'PreToolUse']) {
  const emitters = (hooks?.hooks?.[event] ?? []).flatMap(block => block.hooks ?? []).filter(hook => /(?:session-inject|codex-doc-inject)\.js/u.test(hook.command ?? ''));
  if (!emitters.length || emitters.some(hook => hook.additionalContextLimit !== 0 || !hook.command.includes('--budget 0'))) {
    failures.push(`${event}: CTXRoute emitter must declare additionalContextLimit 0 and --budget 0`);
  }
}

if (config?.enabled !== true || config?.frames !== 1) failures.push('ctxroute-config.json must enable CTXRoute with one Codex frame');

const requiredHooks = [...expected.values()].flatMap(entries => [...entries.keys()]);
for (const name of requiredHooks) {
  if (!existsSync(resolve('node_modules', 'ctxroute', 'src', 'hooks', name))) failures.push(`Installed CTXRoute hook is missing: ${name}`);
}

const rulesDirectory = join('.claude', 'hooks', 'docs');
if (!existsSync(rulesDirectory) || !readdirSync(rulesDirectory).some(name => name.endsWith('.md'))) failures.push('.claude/hooks/docs/ must contain at least one CTXRoute document');

if (!failures.length) {
  const environment = {
    ...process.env,
    CTXROUTE_CONFIG_PATH: join(root, 'ctxroute-config.json'),
    CTXROUTE_DOCS_DIR: join(root, 'docs', 'mcp'),
    CTXROUTE_FILEDOCS_DIR: join(root, '.claude', 'hooks', 'docs'),
    CTXROUTE_FLEET_HOOKS_DIR: join(root, '.claude', 'hooks'),
    CTXROUTE_SESSIONDOCS_DIR: join(root, 'docs', 'session'),
    CTXROUTE_STATE_DIR: join(root, '.ctxroute', 'state'),
  };
  const doctor = spawnSync(process.execPath, ['node_modules/ctxroute/tools/doctor.js', '--quiet'], { cwd: root, env: environment, encoding: 'utf8' });
  if (doctor.status !== 0) failures.push(`CTXRoute doctor failed: ${(doctor.stderr || doctor.stdout).trim()}`);
  const corpus = spawnSync(process.execPath, ['node_modules/ctxroute/tools/lint-corpus.js', '--quiet'], { cwd: root, env: environment, encoding: 'utf8' });
  if (corpus.status !== 0) failures.push(`CTXRoute corpus lint failed: ${(corpus.stderr || corpus.stdout).trim()}`);
}

if (failures.length) {
  console.error([...new Set(failures)].join('\n'));
  process.exit(1);
}

function readJson(path) {
  try { return JSON.parse(readFileSync(path, 'utf8')); }
  catch { failures.push(`${path}: missing or invalid JSON`); return null; }
}
