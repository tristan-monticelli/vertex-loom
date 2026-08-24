import { spawnSync } from 'node:child_process';

const checkOnly = process.argv.includes('--check');
const npmCli = process.env.npm_execpath;

requireVersion('Node.js', process.versions.node, 22);
if (!npmCli) fail('Run setup through npm: npm run setup');

const npmVersion = capture(process.execPath, [npmCli, '--version']);
requireVersion('npm', npmVersion, 10);
capture('git', ['--version']);

if (checkOnly) {
  console.log(`Setup prerequisites are available: Node.js ${process.versions.node}, npm ${npmVersion}.`);
  process.exit(0);
}

run('Install pinned dependencies', process.execPath, [npmCli, 'ci']);
run('Install the Mermaid browser', process.execPath, [npmCli, 'exec', '--yes=false', '--', 'puppeteer', 'browsers', 'install', 'chrome-headless-shell']);
run('Enable repository Git hooks', 'git', ['config', 'core.hooksPath', '.githooks']);
run('Validate the complete starter', process.execPath, [npmCli, 'run', 'validate']);

console.log('CTXRoute Blueprint setup is complete.');

function run(label, command, args) {
  console.log(`\n${label}...`);
  const result = spawnSync(command, args, { cwd: process.cwd(), stdio: 'inherit' });
  if (result.error) fail(`${label} failed: ${result.error.message}`);
  if (result.status !== 0) process.exit(result.status ?? 1);
}

function capture(command, args) {
  const result = spawnSync(command, args, { cwd: process.cwd(), encoding: 'utf8' });
  if (result.error || result.status !== 0) fail(`Required command is unavailable: ${command}`);
  return result.stdout.trim();
}

function requireVersion(name, version, minimumMajor) {
  const major = Number.parseInt(version.split('.')[0], 10);
  if (!Number.isInteger(major) || major < minimumMajor) fail(`${name} ${minimumMajor}+ is required; found ${version}.`);
}

function fail(message) {
  console.error(message);
  process.exit(1);
}
