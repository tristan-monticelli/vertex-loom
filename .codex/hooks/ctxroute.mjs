import { existsSync } from 'node:fs';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { spawnSync } from 'node:child_process';

const projectRoot = resolve(dirname(fileURLToPath(import.meta.url)), '../..');
const hookName = process.argv[2];
const allowedHooks = new Set([
  'canary-check.js',
  'codex-doc-inject.js',
  'codex-doc-write-guard.js',
  'ctxroute-reset.js',
  'session-inject.js',
  'turn-count.js',
]);

if (!allowedHooks.has(hookName)) {
  console.error(`Unknown CTXRoute hook: ${hookName ?? '(missing)'}`);
  process.exit(1);
}

const hookPath = join(projectRoot, 'node_modules', 'ctxroute', 'src', 'hooks', hookName);
if (!existsSync(hookPath)) {
  console.error('CTXRoute is not installed. Run npm run setup.');
  process.exit(0);
}

const environment = {
  ...process.env,
  CTXROUTE_CONFIG_PATH: join(projectRoot, 'ctxroute-config.json'),
  CTXROUTE_DOCS_DIR: join(projectRoot, 'docs', 'mcp'),
  CTXROUTE_FILEDOCS_DIR: join(projectRoot, '.claude', 'hooks', 'docs'),
  CTXROUTE_FLEET_HOOKS_DIR: join(projectRoot, '.claude', 'hooks'),
  CTXROUTE_SESSIONDOCS_DIR: join(projectRoot, 'docs', 'session'),
  CTXROUTE_STATE_DIR: join(projectRoot, '.ctxroute', 'state'),
};

const result = spawnSync(process.execPath, [hookPath, ...process.argv.slice(3)], {
  cwd: projectRoot,
  env: environment,
  stdio: 'inherit',
});

if (result.error) {
  console.error(`CTXRoute failed to start: ${result.error.message}`);
  process.exit(0);
}

process.exit(result.status ?? 1);
