import process from 'node:process';

const input = JSON.parse(await stdin());
const toolInput = input.tool_input ?? {};
const paths = extractPaths(toolInput);
const codePaths = paths.filter(path => /\.(?:c|cc|cpp|cs|css|gd|go|h|hpp|java|js|jsx|mjs|py|php|rs|sass|scss|shader|sql|swift|ts|tsx|vue)$/iu.test(path));
const policyPaths = paths.filter(path => /(?:^|\/)(?:AGENTS\.md|agents\.md|CLAUDE\.md|\.project\/project-config\.json|\.codex\/(?:hooks\.json|architecture-policy\.json|hooks\/[^/]+)|\.githooks\/[^/]+)/iu.test(path));
const docPaths = paths.filter(path => /\.(?:md|mmd)$/iu.test(path));

if (codePaths.length || policyPaths.length || docPaths.length) {
  const lines = ['Audit required before continuing.'];
  if (codePaths.length) {
    lines.push(`Code : ${codePaths.join(', ')}`);
    lines.push('Read relevant documentation and diagrams before changing code.');
    lines.push('Check placement, architecture, structure, reuse, duplication, regressions, deletions, and side effects.');
    lines.push('Update documentation when architecture, contracts, flows, state, or dependencies change.');
  }
  if (docPaths.length) {
    lines.push(`Documentation : ${docPaths.join(', ')}`);
    lines.push('Check consistency, links, Mermaid, and related documents.');
  }
  if (policyPaths.length) {
    lines.push(`Instructions/hooks : ${policyPaths.join(', ')}`);
    lines.push('Check scope, consistency, format, security, fail-open behavior, and actual behavior.');
  }
  lines.push('Review the diff and run the relevant validation.');
  process.stdout.write(JSON.stringify({
    hookSpecificOutput: { hookEventName: 'PostToolUse', additionalContext: lines.join('\n') },
  }));
}

function extractPaths(toolInput) {
  const paths = new Set();
  visit(toolInput, '');
  return [...paths];

  function visit(value, key) {
    if (typeof value === 'string') {
      if (/^(?:file_?path|path|filename)$/iu.test(key) && !value.includes('\n')) paths.add(value.trim());
      for (const match of value.matchAll(/\*\*\*\s+(?:Add|Update|Delete)\s+File:\s*([^\n]+)/giu)) paths.add(match[1].trim().replace(/^['"]|['"]$/gu, ''));
      return;
    }
    if (Array.isArray(value)) return value.forEach(item => visit(item, key));
    if (value && typeof value === 'object') {
      for (const [childKey, child] of Object.entries(value)) visit(child, childKey);
    }
  }
}

function stdin() {
  return new Promise(resolve => {
    let value = '';
    process.stdin.setEncoding('utf8');
    process.stdin.on('data', chunk => { value += chunk; });
    process.stdin.on('end', () => resolve(value || '{}'));
  });
}
