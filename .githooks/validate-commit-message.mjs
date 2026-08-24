import { readFileSync } from 'node:fs';

const message = readFileSync(process.argv[2], 'utf8').split(/\r?\n/u)[0].trim();
const valid = /^(?:feat|fix|refactor|test|docs|chore|perf)(?:\([^)]+\))?!?: .{1,72}$/u;
if (!valid.test(message)) {
  console.error('Invalid commit. Expected format: type(scope): short description');
  process.exit(1);
}
