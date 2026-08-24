# Template governance

`.project/project-config.json` is the single source for project decisions,
source directories, code extensions, contracts, and commands.

While the project status is `template`, its `starter` paths are also a
completeness manifest: every declared infrastructure root and root file must
exist. Derived projects may revise that manifest during approved initialization
and cleanup.

`.codex/architecture-policy.json` only locates that configuration and declares
allowed states. CTXRoute loads relevant rules from `.claude/hooks/docs/` through
the portable `.codex/hooks/ctxroute.mjs` wrapper.

```mermaid
flowchart TD
    Request[Requested write] --> Route[CTXRoute context injection]
    Route --> PreTool[PreToolUse policy]
    PreTool --> Config[Fail-closed configuration]
    Config --> Edit[Authorized write]
    Edit --> PostTool[PostToolUse audit]
    PostTool --> Index[Git index]
    Index --> PreCommit[Authoritative pre-commit]
    PreCommit --> Architecture[Architecture and ADR checks]
    PreCommit --> Documentation[Links, placeholders, and Mermaid]
    PreCommit --> Quality[Targeted mutation testing when configured]
    Architecture --> PrePush[Pre-push]
    Documentation --> PrePush
    Quality --> PrePush
    PrePush --> Commands[Complete project commands]
    Commands --> Stop[Stop and final audit]
```

PreToolUse provides immediate feedback. Git hooks remain authoritative because
they inspect the index and capture files produced by commands or external tools.
