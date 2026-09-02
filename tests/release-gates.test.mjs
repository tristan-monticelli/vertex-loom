import test from 'node:test';
import assert from 'node:assert/strict';
import { skippedTests } from '../.github/scripts/require-no-skipped-tests.mjs';

test('release gate distinguishes passed and skipped CTest cases', () => {
  const report = `<testsuite>
    <testcase name="pass" status="run"></testcase>
    <testcase name="skip-status" status="notrun"></testcase>
    <testcase name="skip-element" status="run"><skipped message="display"/></testcase>
  </testsuite>`;
  assert.deepEqual(skippedTests(report), ['skip-status', 'skip-element']);
});
