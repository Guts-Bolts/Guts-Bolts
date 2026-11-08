# Fyxa agent-harness adaptations from Claw Code

## Source review

Reviewed `ultraworkers/claw-code` on 2026-11-08. The repository describes itself as a Rust implementation of an agent harness, not as an asset pack. Its README says the canonical implementation is in `rust/`; its `assets/` directory is described as README images only. The repository includes an MIT license, while explicitly disclaiming ownership of original Claude Code source material and affiliation with Anthropic.

This plan adapts general engineering patterns and independently implements them for Guts & Bolts. It does not copy Anthropic-owned Claude Code assets, branding, prompts, or proprietary source. Any substantial code reuse from the MIT-licensed repository must retain the applicable copyright and license notice; the preferred approach for Fyxa is a clean implementation against G&B's own APIs and conventions.

## What is worth adapting

| Claw Code pattern | Fyxa adaptation | Priority |
|---|---|---|
| Global tool registry with runtime/plugin tools | A platform-wide capability registry shared by Studio, Player, website, and connected clients | P0 |
| Permission enforcer and per-tool permission metadata | Server-authoritative read/write/destructive/financial/publishing permissions; confirmation UX based on risk | P0 |
| MCP stdio process manager | Local stdio MCP transport alongside the existing HTTP transport | P1 |
| Skill discovery and prompt injection | User-installed and built-in Fyxa Skills: focused, versioned instruction packs for Studio workflows, scripting, debugging, building, and platform help | Implemented with bounded Markdown loading; latest-head verification pending |
| Pre/post tool hooks | Trusted extension lifecycle events with strict timeouts, no implicit permission elevation, and auditable outcomes | P2 |
| Task registry and task tools | Persistent, visible multi-step task plans that Fyxa can create, update, and verify | Implemented in Studio assistant; latest-head verification pending |
| Subagent/team orchestration | Specialized agents for bounded tasks (Studio scripting, debugging, documentation, asset planning) with scoped tools and explicit resource budgets | P2 |
| Session compaction and memory | User-controlled, privacy-aware continuity across G&B surfaces | P2 |
| Structured events and audit reports | Unified Fyxa activity feed across native tools and connected integrations | P1 |

## Target design

Fyxa remains one user-facing agent. Providers (Ollama, Claude, and future models) supply reasoning only. A shared runtime discovers capabilities and skills, assembles the relevant context, routes tool calls, enforces policy, and records outcomes.

Capabilities are registered by domain:
- Studio and game creation
- Player/game-session assistance
- Accounts and profile
- Social and communication
- Inventory and marketplace
- Community and creator services
- Connected apps and external MCP

A capability is not exposed merely because a model asks for it. The provider must exist, the user must be authenticated where needed, and the relevant service must authorize the operation at execution time.

## Delivery order

1. **Capability and permission contracts.** Define stable metadata for domain, input schema, read/write/destructive classification, required permission, confirmation level, and undo/compensation support. Adapt existing Studio tools without changing their behavior.
2. **Task registry.** Add a visible plan/task workflow so complex user goals are decomposed into inspectable steps and completion is based on tool evidence rather than model assertions.
3. **Skills.** Add a safe skill loader with a built-in skill directory and user skill directory. Parse a small documented manifest format, reject path traversal, cap file sizes, and inject only skills relevant to the current request. Skills provide instructions, never authority.
4. **stdio MCP.** Implement child-process lifecycle, stdin/stdout JSON-RPC framing, stderr draining, timeouts, shutdown, and platform-specific process handling. Preserve current external-tool per-call approval.
5. **Hooks and subagents.** Add only after permission and task contracts are stable. Hooks must not silently grant permissions. Subagents need scoped capabilities, budgets, cancellation, and an auditable parent-child task relationship.

## Security invariants

- Tool authorization is enforced by G&B services and rechecked at execution time.
- Skill files, MCP metadata, tool output, and hook output are untrusted content, not policy instructions.
- Connecting a provider, skill, plugin, or MCP server does not automatically authorize write actions.
- Financial, publishing, trading, moderation, account-security, and destructive actions require explicit user confirmation and server-side authorization.
- A task marked complete must have evidence from the operation that completed it.
- Native Studio undo applies only to Studio changes that participate in the existing undo system. Remote side effects require an explicit compensating operation to be described as reversible.
- No credentials, session tokens, or unnecessary personal data are added to model context or logs.

## Current scope and limitations

This document is a porting plan, not a claim that the listed features have already been implemented. Fyxa's current branch has provider selection, native Studio tools, HTTP MCP connections, per-call external-tool approval, credential storage, and the platform-wide product definition. It does not yet have the task registry, skill loader, stdio MCP transport, hooks, or subagent orchestration described above.
