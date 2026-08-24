import { existsSync } from 'node:fs';
import { execFileSync } from 'node:child_process';
import process from 'node:process';
import {
  isAdr,
  isArchitectureEvidence,
  isCodePath,
  isContractPath,
  isGeneratedPath,
  isSourcePath,
  isStarterPath,
  isTestPath,
  loadProjectConfig,
  normalizePath,
} from '../../.githooks/project-policy.mjs';

let input;
try { input = JSON.parse(await stdin()); }
catch { block('Write blocked: invalid hook input.'); }
const toolInput = input.tool_input ?? {};
const paths = extractPaths(toolInput);
const { config, failures } = loadProjectConfig();

if (failures.length) {
  const repairPaths = new Set(['.codex/architecture-policy.json', '.project/project-config.json']);
  if (paths.length && paths.every(path => repairPaths.has(path))) {
    context(`Configuration repair allowed: ${failures.join(', ')}`);
    process.exit(0);
  }
  block(['Write blocked: invalid project configuration.', ...failures]);
}

const toolName = String(input.tool_name ?? '');
const command = commandText(toolInput);
if (isShellTool(toolName) && config.status === 'template' && !isSafeTemplateCommand(command)) {
  block('Write blocked: only read and validation commands, plus the project bootstrap, are allowed before initialization is complete.');
}
if (isShellTool(toolName) && config.status === 'initialized' && isDirectMutationCommand(command)) {
  block('Write blocked: use a traceable editing tool for project files; direct shell writes cannot verify associated C4 documents and ADRs.');
}

if (!paths.length) {
  if (isShellTool(toolName) && command) {
    context('Command has no verifiable path; Git hooks will check produced files.');
  }
  process.exit(0);
}

const changePaths = [...new Set([...paths, ...gitChangedFiles()])];
const architectureEvidence = changePaths.some(path => isArchitectureEvidence(path, config));
const adrEvidence = changePaths.some(isAdr);
const contractPaths = paths.filter(path => isContractPath(path, config));

if (contractPaths.length && !adrEvidence) {
  block([
    'Write blocked: a contract or dependency requires an ADR in the same change.',
    `Contracts: ${contractPaths.join(', ')}`,
  ]);
}

if (config.status === 'template') {
  const projectPaths = paths.filter(path => !isStarterPath(path, config) && !isTestPath(path, config) && !isGeneratedPath(path, config));
  if (projectPaths.length) {
    block([
      'Write blocked: the project is still in template mode.',
      `Product files: ${projectPaths.join(', ')}`,
      'Complete the brief, decisions, C4 diagrams, and quality strategy, then set the configuration to initialized before writing product code.',
    ]);
  }
  process.exit(0);
}

const codeOutsideDeclaredRoots = paths.filter(path => isCodePath(path, config) && !isSourcePath(path, config) && !isTestPath(path, config) && !isStarterPath(path, config) && !isGeneratedPath(path, config));
if (codeOutsideDeclaredRoots.length) {
  block([
    'Write blocked: code is outside declared directories.',
    `Files: ${codeOutsideDeclaredRoots.join(', ')}`,
    'Declare the directory in .project/project-config.json or move the code.',
  ]);
}

const newSourceFiles = paths.filter(path => isSourcePath(path, config) && !existsSync(path) && !isTestPath(path, config));
const structuralChange = paths.some(path => isSourcePath(path, config)) && hasStructuralSignal(addedContent(toolInput));
if ((newSourceFiles.length || structuralChange) && !architectureEvidence) {
  block([
    'Write blocked: structural change has no associated diagram.',
    `Files: ${(newSourceFiles.length ? newSourceFiles : paths.filter(path => isSourcePath(path, config))).join(', ')}`,
    'Update a C4, components, or flows document in the same change.',
  ]);
}

if (paths.some(path => isSourcePath(path, config))) {
  context('Product code changed: verify documentation, side effects, and test strategy.');
}

function extractPaths(value) {
  const paths = new Set();
  visit(value, '');
  return [...paths].map(path => normalizePath(path)).filter(Boolean);

  function visit(current, key) {
    if (typeof current === 'string') {
      if (/^(?:file_?path|path|filename)$/iu.test(key) && !current.includes('\n')) paths.add(current);
      for (const match of current.matchAll(/\*\*\*\s+(?:Add|Update|Delete)\s+File:\s*([^\n]+)/giu)) paths.add(match[1].trim().replace(/^['"]|['"]$/gu, ''));
      return;
    }
    if (Array.isArray(current)) return current.forEach(item => visit(item, key));
    if (current && typeof current === 'object') {
      for (const [childKey, child] of Object.entries(current)) visit(child, childKey);
    }
  }
}

function addedContent(value) {
  const strings = [];
  visit(value);
  return strings.join('\n');

  function visit(current) {
    if (typeof current === 'string') return strings.push(current);
    if (Array.isArray(current)) return current.forEach(visit);
    if (current && typeof current === 'object') Object.values(current).forEach(visit);
  }
}

function hasStructuralSignal(content) {
  return /(?:^|\n)\+?.*\b(?:class|interface|struct|module|namespace|trait|protocol|enum|record|service|message|export|extends|implements)\b|(?:^|\n)\+?.*\b(?:def|func|fn)\s+[A-Za-z_]/u.test(content);
}

function commandText(value) {
  if (typeof value === 'string') return value;
  return typeof value?.cmd === 'string' ? value.cmd : typeof value?.command === 'string' ? value.command : '';
}

function isShellTool(name) {
  return /(?:exec_command|bash|shell)/iu.test(name);
}

function isSafeTemplateCommand(value) {
  if (!value.trim()) return true;
  if (/[;|><`]|\$\(|\b(?:rm|mv|cp|touch|mkdir|install|tee|truncate|dd)\b|\bsed\s+-i\b|\bfind\b[^\n]*\s-(?:delete|exec)\b/iu.test(value)) return false;
  const commands = value.split(/\r?\n|&&/u).map(line => line.trim()).filter(Boolean);
  return commands.every(line => /^(?:pwd|rg\b|ls\b|head\b|tail\b|wc\b|find\b|sed\s+-n\b|git\s+(?:status|diff|log|show|branch|rev-parse|ls-files)\b|npm\s+(?:test|run\s+(?:setup(?::check)?|test|validate(?::[\w-]+)?))\b|node\s+--check\b)/u.test(line));
}

function isDirectMutationCommand(value) {
  return /[>]|\b(?:rm|mv|cp|touch|mkdir|tee|truncate|dd|install)\b|\bsed\s+-i\b|\bfind\b[^\n]*\s-(?:delete|exec)\b|\b(?:node|python|python3|ruby|perl)\s+-e\b/iu.test(value);
}

function gitChangedFiles() {
  const files = new Set();
  for (const args of [
    ['diff', '--name-only', '-z'],
    ['diff', '--cached', '--name-only', '-z'],
    ['ls-files', '--others', '--exclude-standard', '-z'],
  ]) {
    try {
      execFileSync('git', args, { encoding: 'utf8', stdio: ['ignore', 'pipe', 'ignore'] }).split('\0').filter(Boolean).forEach(path => files.add(normalizePath(path)));
    } catch {}
  }
  return [...files];
}

function block(reason) {
  process.stdout.write(JSON.stringify({ decision: 'block', reason: Array.isArray(reason) ? reason.join('\n') : reason }));
  process.exit(0);
}

function context(message) {
  process.stdout.write(JSON.stringify({ hookSpecificOutput: { hookEventName: 'PreToolUse', additionalContext: message } }));
}

function stdin() {
  return new Promise(resolveInput => {
    let value = '';
    process.stdin.setEncoding('utf8');
    process.stdin.on('data', chunk => { value += chunk; });
    process.stdin.on('end', () => resolveInput(value || '{}'));
  });
}
