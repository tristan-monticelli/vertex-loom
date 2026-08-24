import { execFileSync } from 'node:child_process';
import { readdirSync } from 'node:fs';
import { join } from 'node:path';
import {
  isAdr,
  isArchitectureEvidence,
  isCodePath,
  isContractPath,
  isGeneratedPath,
  isIgnoredPath,
  isSourcePath,
  isStarterPath,
  isTestPath,
  loadProjectConfig,
  validateProjectConfig,
} from './project-policy.mjs';

const allMode = process.argv.includes('--all');
const { config, failures: configFailures } = allMode ? loadProjectConfig() : loadIndexConfig();
const failures = [...configFailures];

if (!config) finish();

const files = allMode ? allFiles() : stagedFiles();
const added = allMode ? new Set() : new Set(stagedFiles('A'));
const architectureEvidence = files.some(path => isArchitectureEvidence(path, config));
const adrEvidence = files.some(isAdr);

if (config.status === 'template') {
  for (const path of files) {
    if (!isStarterPath(path, config) && !isTestPath(path, config) && !isGeneratedPath(path, config)) failures.push(`${path}: product file forbidden while status is template`);
  }
  if (!allMode) {
    const changedContracts = files.filter(path => isContractPath(path, config) && !added.has(path));
    if (changedContracts.length && !adrEvidence) failures.push(`ADR required for contracts: ${changedContracts.join(', ')}`);
  }
  finish();
}

for (const path of files) {
  if (isCodePath(path, config) && !isSourcePath(path, config) && !isTestPath(path, config) && !isStarterPath(path, config) && !isGeneratedPath(path, config)) {
    failures.push(`${path}: code outside declared source or test directories`);
  }
}

if (!allMode) {
  const newModules = [...added].filter(path => isSourcePath(path, config) && !isTestPath(path, config));
  const structuralFiles = files.filter(path => isSourcePath(path, config) && hasStructuralDiff(path));
  if ((newModules.length || structuralFiles.length) && !architectureEvidence) {
    failures.push(`Architecture diagram required for: ${[...new Set([...newModules, ...structuralFiles])].join(', ')}`);
  }

  const contracts = files.filter(path => isContractPath(path, config));
  if (contracts.length && !adrEvidence) failures.push(`ADR required for contracts: ${contracts.join(', ')}`);
}

finish();

function stagedFiles(filter = 'ACMR') {
  try {
    return execFileSync('git', ['diff', '--cached', '--name-only', `--diff-filter=${filter}`, '-z'], { encoding: 'utf8' })
      .split('\0').filter(Boolean);
  } catch {
    return [];
  }
}

function loadIndexConfig() {
  try {
    const policy = JSON.parse(execFileSync('git', ['show', ':.codex/architecture-policy.json'], { encoding: 'utf8' }));
    const path = policy.projectConfig ?? '.project/project-config.json';
    const config = JSON.parse(execFileSync('git', ['show', `:${path}`], { encoding: 'utf8' }));
    return { config, failures: validateProjectConfig(config, process.cwd(), policy) };
  } catch {
    return { config: null, failures: ['Missing or invalid configuration/policy in the Git index'] };
  }
}

function allFiles(directory = '.', prefix = '') {
  const ignoredRoots = new Set(['.git', 'node_modules']);
  const values = [];
  for (const entry of readdirSync(directory, { withFileTypes: true })) {
    if (!prefix && ignoredRoots.has(entry.name)) continue;
    const path = prefix ? `${prefix}/${entry.name}` : entry.name;
    if (isIgnoredPath(path)) continue;
    if (isGeneratedPath(path, config)) continue;
    if (entry.isDirectory()) values.push(...allFiles(join(directory, entry.name), path));
    else values.push(path);
  }
  return values;
}

function hasStructuralDiff(path) {
  try {
    const diff = execFileSync('git', ['diff', '--cached', '--unified=0', '--', path], { encoding: 'utf8' });
    return /^\+[^+].*\b(?:class|interface|struct|module|namespace|trait|protocol|enum|record|service|message|export|extends|implements)\b|^\+[^+].*\b(?:def|func|fn)\s+[A-Za-z_]/mu.test(diff);
  } catch {
    return false;
  }
}

function finish() {
  if (failures.length) {
    console.error([...new Set(failures)].join('\n'));
    process.exit(1);
  }
  process.exit(0);
}
