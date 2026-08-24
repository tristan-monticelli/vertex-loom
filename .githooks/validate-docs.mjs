import { execFileSync } from 'node:child_process';
import { existsSync, mkdtempSync, readFileSync, readdirSync, rmSync, writeFileSync } from 'node:fs';
import { dirname, join, relative, resolve } from 'node:path';
import { tmpdir } from 'node:os';
import { createRequire } from 'node:module';
import { isIgnoredPath, loadProjectConfig, validateProjectConfig } from './project-policy.mjs';

const require = createRequire(import.meta.url);
const mermaidCli = join(dirname(require.resolve('@mermaid-js/mermaid-cli')), 'cli.js');

const allMode = process.argv.includes('--all');
const indexMode = process.argv.includes('--index');
const files = indexMode ? indexFiles() : allMode ? repositoryDocs() : stagedFiles();
const targets = files.filter(file => /\.(?:md|mmd)$/iu.test(file));
const failures = [];
const temporary = mkdtempSync(join(tmpdir(), 'diagram-validation-'));
const { config, failures: configFailures } = indexMode ? loadIndexConfig() : loadProjectConfig();

failures.push(...configFailures);

try {
  rejectStarterGuidesWhenInitialized(targets);
  validateRequiredArchitecture();
  for (const file of targets) {
    const source = readSource(file);
    if (file.endsWith('.md')) validateLinks(file, source);
    const diagrams = file.endsWith('.mmd') ? [source] : extractMermaid(source);
    if (!diagrams.length && file.endsWith('.mmd')) failures.push(`${file}: empty diagram`);
    for (const [index, diagram] of diagrams.entries()) validateDiagram(file, index, diagram);
  }
} finally {
  rmSync(temporary, { recursive: true, force: true });
}

function validateRequiredArchitecture() {
  if (config?.status !== 'initialized') return;
  const expected = new Map([
    ['docs/architecture/context.md', 'C4Context'],
    ['docs/architecture/containers.md', 'C4Container'],
  ]);
  for (const [file, diagramType] of expected) {
    if (!(config.architecture?.documents ?? []).includes(file)) failures.push(`${file}: required document missing from architecture.documents`);
    else if (!existsSync(file)) failures.push(`${file}: required document is missing`);
    else if (!new RegExp(`\`\`\`mermaid\\s*\\n${diagramType}\\b`, 'u').test(readSource(file))) failures.push(`${file}: ${diagramType} diagram required after initialization`);
  }
}

if (failures.length) {
  console.error([...new Set(failures)].join('\n'));
  process.exit(1);
}

function rejectStarterGuidesWhenInitialized(markdownFiles) {
  if (config?.status !== 'initialized') return;
  for (const file of markdownFiles.filter(file => file.endsWith('.md'))) {
    const source = readSource(file);
    if (/<!--\s*Guide\b|TODO|to be defined|to be decided|to be specified|ADR-NNNN|YYYY-MM-DD/iu.test(source)) failures.push(`${file}: guide or placeholder forbidden after initialization`);
  }
}

function validateLinks(file, source) {
  for (const match of source.matchAll(/!?\[[^\]]*\]\(([^)]+)\)/gu)) {
    const rawTarget = match[1].trim().replace(/^<|>$/gu, '').split(/\s+["']/u)[0];
    if (!rawTarget || rawTarget.startsWith('#') || /^(?:[a-z][a-z0-9+.-]*:|\/)/iu.test(rawTarget)) continue;
    let path;
    try { path = decodeURIComponent(rawTarget.split('#')[0]); }
    catch {
      failures.push(`${file}: malformed local link "${rawTarget}"`);
      continue;
    }
    if (!path) continue;
    const absolute = resolve(dirname(resolve(file)), path);
    const repositoryPath = relative(process.cwd(), absolute);
    if (repositoryPath.startsWith('..') || repositoryPath === '') failures.push(`${file}: local link outside repository "${rawTarget}"`);
    else if (!existsSync(absolute)) failures.push(`${file}: local link not found "${rawTarget}"`);
  }
}

function validateDiagram(file, index, source) {
  const trimmed = source.trim();
  const label = `${file}${file.endsWith('.md') ? `#${index + 1}` : ''}`;
  if (!trimmed) return failures.push(`${label}: empty diagram`);
  if (/Project under development|Replace the placeholders|TODO/u.test(trimmed)) return failures.push(`${label}: placeholder forbidden in a diagram`);
  if (!/^(?:flowchart|graph|sequenceDiagram|stateDiagram|erDiagram|classDiagram|C4Context|C4Container|C4Component|architecture)\b/mu.test(trimmed)) return failures.push(`${label}: unsupported Mermaid type`);
  validateC4Identifiers(label, trimmed);

  const stem = `${file}-${index}`.replace(/\W/gu, '_');
  const input = join(temporary, `${stem}.mmd`);
  const output = join(temporary, `${stem}.svg`);
  writeFileSync(input, trimmed);
  try {
    execFileSync(process.execPath, [mermaidCli, '-i', input, '-o', output, '-q'], { stdio: 'pipe' });
  } catch (error) {
    failures.push(`${label}: invalid Mermaid (${String(error.stderr ?? '').trim().split(/\r?\n/u).at(-1) ?? 'render failed'})`);
  }
}

function validateC4Identifiers(label, source) {
  if (!/^C4/mu.test(source)) return;
  const definitions = new Set();
  for (const match of source.matchAll(/^\s*(?:Person|Person_Ext|System|System_Ext|SystemDb|Container|ContainerDb|Component|ComponentDb|System_Boundary|Container_Boundary)\s*\(\s*([A-Za-z_][\w-]*)/gmu)) {
    if (definitions.has(match[1])) failures.push(`${label}: duplicate C4 identifier "${match[1]}"`);
    definitions.add(match[1]);
  }
  for (const match of source.matchAll(/^\s*(?:Rel|BiRel|Rel_U|Rel_D|Rel_L|Rel_R)\s*\(\s*([A-Za-z_][\w-]*)\s*,\s*([A-Za-z_][\w-]*)/gmu)) {
    for (const identifier of [match[1], match[2]]) {
      if (!definitions.has(identifier)) failures.push(`${label}: relation to unknown C4 identifier "${identifier}"`);
    }
  }
}

function extractMermaid(source) {
  return [...source.matchAll(/```mermaid\s*\n([\s\S]*?)```/giu)].map(match => match[1]);
}

function readSource(file) {
  if (indexMode) {
    try { return execFileSync('git', ['show', `:${file}`], { encoding: 'utf8' }); }
    catch { return ''; }
  }
  return readFileSync(file, 'utf8');
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

function stagedFiles() {
  try {
    return execFileSync('git', ['diff', '--cached', '--name-only', '--diff-filter=ACMR', '-z'], { encoding: 'utf8' }).split('\0').filter(Boolean);
  } catch {
    return [];
  }
}

function indexFiles() {
  try { return execFileSync('git', ['ls-files', '-z'], { encoding: 'utf8' }).split('\0').filter(Boolean); }
  catch { return []; }
}

function repositoryDocs(directory = '.', prefix = '') {
  const values = [];
  for (const entry of readdirSync(directory, { withFileTypes: true })) {
    if (!prefix && ['.git', 'node_modules'].includes(entry.name)) continue;
    const path = prefix ? `${prefix}/${entry.name}` : entry.name;
    if (isIgnoredPath(path)) continue;
    if (entry.isDirectory()) values.push(...repositoryDocs(join(directory, entry.name), path));
    else if (/\.(?:md|mmd)$/iu.test(path)) values.push(relative('.', path).replace(/\\/gu, '/'));
  }
  return values;
}
