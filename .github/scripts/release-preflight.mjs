import { readFileSync } from 'node:fs';

const packageVersion = JSON.parse(readFileSync('package.json', 'utf8')).version;
const tag = process.env.GITHUB_REF_NAME ?? '';
const releaseVersion = tag.replace(/^v/u, '').replace(/-(?:rc|beta)\..*$/u, '');
const required = ['VERTEX_LOOM_PACKAGE_CONTACT'];
if (process.env.RUNNER_OS === 'macOS') {
  required.push('MACOS_CERTIFICATE_BASE64', 'MACOS_CERTIFICATE_PASSWORD',
    'MACOS_SIGNING_IDENTITY', 'APPLE_ID', 'APPLE_TEAM_ID', 'APPLE_APP_PASSWORD');
}
if (process.env.RUNNER_OS === 'Windows') {
  required.push('WINDOWS_CERTIFICATE_BASE64', 'WINDOWS_CERTIFICATE_PASSWORD');
}
const missing = required.filter(name => !(process.env[name] ?? '').trim());
if (releaseVersion !== packageVersion) {
  console.error(`Tag ${tag} does not match package version ${packageVersion}`);
  process.exit(1);
}
if (missing.length) {
  console.error(`Release secrets or metadata missing: ${missing.join(', ')}`);
  process.exit(1);
}
if (/example\.invalid/iu.test(process.env.VERTEX_LOOM_PACKAGE_CONTACT)) {
  console.error('Release contact must be the official public project address');
  process.exit(1);
}
