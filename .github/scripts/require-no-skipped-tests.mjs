import { readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';

export function skippedTests(junit) {
  const results = [];
  for (const match of junit.matchAll(/<testcase\b([^>]*)>([\s\S]*?)<\/testcase>|<testcase\b([^>]*)\/>/gu)) {
    const attributes = match[1] ?? match[3] ?? '';
    const body = match[2] ?? '';
    if (/\bstatus="notrun"/u.test(attributes) || /<skipped\b/u.test(body)) {
      results.push(attributes.match(/\bname="([^"]+)"/u)?.[1] ?? 'unnamed test');
    }
  }
  return results;
}

if (process.argv[1] && fileURLToPath(import.meta.url) === process.argv[1]) {
  const path = process.argv[2];
  if (!path) throw new Error('usage: require-no-skipped-tests.mjs <ctest-junit.xml>');
  const skipped = skippedTests(readFileSync(path, 'utf8'));
  if (skipped.length) {
    console.error(`Release gate rejected skipped tests:\n${skipped.join('\n')}`);
    process.exit(1);
  }
}
