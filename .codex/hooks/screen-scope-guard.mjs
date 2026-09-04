import process from 'node:process';

const input = JSON.parse(await stdin());
const toolName = String(input.tool_name ?? '');
if (!/node_repl__js$/u.test(toolName)) process.exit(0);

const code = String(input.tool_input?.code ?? '');
const globalCapture = /(?:CGDisplayCreateImage|SCStream|ScreenCaptureKit|capture(?:Full|Entire)?Screen|desktopScreenshot|wholeScreen|fullScreenCapture)/iu;
if (globalCapture.test(code)) {
  process.stdout.write(JSON.stringify({
    decision: 'block',
    reason: 'Computer Use bloqué : seules les captures limitées à une application sont autorisées. Utilisez sky.get_app_state({ app: "..." }) et sa capture associée.',
  }));
  process.exit(0);
}

if (/sky\.get_app_state\s*\(/u.test(code) && !/\bapp\s*:/u.test(code)) {
  process.stdout.write(JSON.stringify({
    decision: 'block',
    reason: 'Computer Use bloqué : get_app_state doit cibler explicitement une application via { app: "..." }.',
  }));
  process.exit(0);
}

process.stdout.write(JSON.stringify({
  hookSpecificOutput: {
    hookEventName: 'PreToolUse',
    additionalContext: 'Screen scope: app-only. Ne pas capturer le bureau ou l’écran global.',
  },
}));

function stdin() {
  return new Promise(resolve => {
    let value = '';
    process.stdin.setEncoding('utf8');
    process.stdin.on('data', chunk => { value += chunk; });
    process.stdin.on('end', () => resolve(value || '{}'));
  });
}
