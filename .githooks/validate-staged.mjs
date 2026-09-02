import { execFileSync } from 'node:child_process';
import { mkdtempSync, rmSync, writeFileSync } from 'node:fs';
import { extname, join } from 'node:path';
import { tmpdir } from 'node:os';

const files = execFileSync('git', ['diff', '--cached', '--name-only', '--diff-filter=ACMR', '-z'], { encoding: 'utf8' }).split('\0').filter(Boolean);
const failures = [];
const temporary = mkdtempSync(join(tmpdir(), 'staged-validation-'));

try {
  for (const [index, file] of files.entries()) {
    if (!/\.(?:js|mjs|cjs|json)$/iu.test(file)) continue;
    const source = stagedSource(file);
    if (/\.(?:js|mjs|cjs)$/iu.test(file)) validateJavaScript(file, source, index);
    if (/\.json$/iu.test(file)) {
      try { JSON.parse(source); }
      catch { failures.push(`${file}: invalid JSON`); }
    }
  }
} finally {
  rmSync(temporary, { recursive: true, force: true });
}

if (failures.length) {
  console.error(failures.join('\n'));
  process.exit(1);
}

function stagedSource(file) {
  return execFileSync('git', ['show', `:${file}`], { encoding: 'utf8' });
}

function validateJavaScript(file, source, index) {
  const path = join(temporary, `${index}${extname(file)}`);
  writeFileSync(path, source);
  try { execFileSync('node', ['--check', path], { stdio: 'pipe' }); }
  catch { failures.push(`${file}: invalid JavaScript`); }
}
