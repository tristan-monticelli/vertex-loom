import { spawnSync } from 'node:child_process';
import { loadProjectConfig } from './project-policy.mjs';

const preCommit = process.argv.includes('--pre-commit');
const { config, failures } = loadProjectConfig();

if (failures.length) {
  console.error(failures.join('\n'));
  process.exit(1);
}

const commands = [];
if (preCommit) {
  if (config.quality.mutation.preCommit) commands.push(['mutation', config.commands.mutation]);
} else {
  const names = config.status === 'template' ? ['test'] : ['lint', 'typecheck', 'test', 'integration', 'build'];
  for (const name of names) {
    if (config.commands[name]) commands.push([name, config.commands[name]]);
  }
  if (config.quality.mutation.prePush) commands.push(['mutation', config.commands.mutation]);
}

for (const [name, command] of commands) {
  console.error(`Running ${name}: ${command}`);
  const result = spawnSync(command, { cwd: process.cwd(), shell: true, stdio: 'inherit' });
  if (result.status !== 0) process.exit(result.status ?? 1);
}
