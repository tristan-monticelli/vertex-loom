import { accessSync, constants, existsSync, readFileSync } from 'node:fs';
import { execFileSync } from 'node:child_process';
import { delimiter, isAbsolute, join } from 'node:path';
import { loadProjectConfig, validateProjectConfig } from './project-policy.mjs';

const indexMode = process.argv.includes('--index');
const { config, failures: structureFailures } = indexMode ? loadIndexConfig() : loadProjectConfig();
const failures = [...structureFailures];
const packageJson = readJson('package.json', indexMode) ?? { scripts: {} };

if (config) {
  for (const [name, command] of Object.entries(config.commands ?? {})) {
    if (typeof command !== 'string' || !command.trim()) continue;
    const npmScript = command.trim().match(/^npm(?:\s+run)?\s+([^\s]+)/u)?.[1];
    if (npmScript) {
      if (!packageJson.scripts?.[npmScript]) failures.push(`commands.${name}: npm script "${npmScript}" does not exist`);
      continue;
    }

    const executable = commandTokens(command).find(token => !/^[A-Za-z_][A-Za-z0-9_]*=/u.test(token));
    if (!executable || !findExecutable(executable)) failures.push(`commands.${name}: executable not found "${executable ?? ''}"`);
  }
}

if (failures.length) {
  console.error([...new Set(failures)].join('\n'));
  process.exit(1);
}

function readJson(path, fromIndex = false) {
  if (!fromIndex && !existsSync(path)) return null;
  try { return JSON.parse(fromIndex ? indexSource(path) : readFileSync(path, 'utf8')); }
  catch {
    failures.push(`${path}: invalid JSON`);
    return null;
  }
}

function loadIndexConfig() {
  try {
    const policy = JSON.parse(indexSource('.codex/architecture-policy.json'));
    const config = JSON.parse(indexSource(policy.projectConfig ?? '.project/project-config.json'));
    return { config, failures: validateProjectConfig(config, process.cwd(), policy) };
  } catch {
    return { config: null, failures: ['Missing or invalid configuration/policy in the Git index'] };
  }
}

function indexSource(path) {
  return execFileSync('git', ['show', `:${path}`], { encoding: 'utf8' });
}

function commandTokens(command) {
  return [...command.matchAll(/"([^"]*)"|'([^']*)'|([^\s]+)/gu)].map(match => match[1] ?? match[2] ?? match[3]);
}

function findExecutable(executable) {
  const candidates = [];
  if (isAbsolute(executable) || executable.includes('/')) candidates.push(executable);
  else {
    const extensions = process.platform === 'win32' ? (process.env.PATHEXT ?? '.EXE;.CMD;.BAT').split(';') : [''];
    for (const directory of (process.env.PATH ?? '').split(delimiter).filter(Boolean)) {
      for (const extension of extensions) candidates.push(join(directory, `${executable}${extension}`));
    }
  }
  return candidates.some(candidate => {
    try {
      accessSync(candidate, process.platform === 'win32' ? constants.F_OK : constants.X_OK);
      return true;
    } catch {
      return false;
    }
  });
}
