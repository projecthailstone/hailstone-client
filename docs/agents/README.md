# Hailstone agent briefing

Hailstone is a C++20 and Vulkan client for EQEmu. It speaks the RoF2 protocol,
reads the player's own EverQuest installation and ships no game assets. The
client has its own game model, renderers, Lua interfaces and add-on host. The
same change often crosses a boundary between two of those systems. Identify
the owner of the behavior before choosing where to edit.

Read [repository instructions](../../AGENTS.md) and the
[contributing guide](../../CONTRIBUTING.md). The handbook adds source routes,
examples and verification steps to those rules. Its Markdown lives with the
code so a fresh checkout can be used by a person, a local coding tool or a
managed runner with the same starting context.

## Find the right page

| Task | Start here | Then check |
| --- | --- | --- |
| Start from an issue, resume a branch or review another contributor's work | [Shared workflow](workflow.md) | [Plan](../plan.md), [to-do list](../todo.md) and [known gaps](../unknowns.md) |
| Review and fix a PR on a Hailstone organization branch | [Organization PR review](workflow.md#review-and-fix-an-organization-pr) | [Repository Git rules](../../AGENTS.md#git-workflow) and [validation](validation.md) |
| Trace a symptom to its owning layer | [Codebase map](codebase.md) | [Code overview](../code-overview.md) and the linked implementation |
| Change a packet, command, timer or gameplay rule | [Compatibility contracts](contracts.md) | [Mechanics](../mechanics.md), [protocol](../protocol/session.md) and [worked recipes](recipes.md) |
| Update a window, key binding or persisted option | [Worked recipes](recipes.md) | [Developer UI](../dev-ui.md), [UI API](../ui-api.md) and [folder layout](../layout.md) |
| Extend the host API, write an add-on or support a server | [Compatibility contracts](contracts.md) | [Plugins and scripts](../plugins.md), [add-ons](../addons.md) and [server support](../../server/README.md) |
| Fix assets, rendering, audio or platform behavior | [Codebase map](codebase.md) | [Graphics](../graphics.md), [file formats](../formats/) and [validation](validation.md) |
| Configure a build, choose tests or report a skipped fixture | [Validation](validation.md) | [CI and local builds](../ci.md), [parser hardening](../hardening.md) and [mock server](../../tests/mock_server/README.md) |
| Prepare a draft PR or pick up work in a different tool | [Shared workflow](workflow.md#handoff) | [Validation](validation.md) and the exact branch, revision and diff |

Use the [glossary](glossary.md) for terms shared across protocol, UI, add-ons
and test reports.

## Decisions that matter early

1. **Identify the baseline.** Use original RoF2 behavior for game rules and
   keep the stock EQEmu path working. A server-offered extension needs a
   stock fallback. The renderer has its own modern design.
2. **Anchor each claim.** Trace the current checkout to establish what
   Hailstone does. Use reference observations to establish what RoF2 does.
   Record uncertain behavior and the evidence needed to settle it in
   [known gaps](../unknowns.md) and the [to-do list](../todo.md).
3. **Keep ownership clear.** Server-specific client behavior belongs in
   its own add-on. A reusable host API belongs in the client with its tests
   and documentation. Use the repository's drop-in Lua scripts for
   Hailstone support. [Server support](../../server/README.md)
4. **Keep the project clean-room.** Cite behavioral findings, use
   independently written code and honor license boundaries. Keep player
   assets, session data and signing material outside the repository.
   [Contributor rules](../../CONTRIBUTING.md#clean-room-rule)
5. **Protect existing work.** Check repository, branch, base and pending
   changes. Use an isolated worktree and build directory. Keep the
   player's game installation read-only. [Folder layout](../layout.md)
6. **Match checks to the claim.** A build, a test with local fixtures, a
   mock session and a live check answer different questions. Record the
   exact commands and which prerequisites were available.
7. **Follow through on the interfaces.** Check both shipped UI packs for
   shared UI changes, legacy callers for persistent state and extension
   points, and other compilers for portable code. Generate derived tables
   and references from their owning inputs.

## Finish checklist

- The diff belongs to the requested repository and intended base, and
  preserves work from other contributors.
- The implementation, public contract and docs agree. Source and test
  additions appear in the build lists, and generated changes come from
  the appropriate tool.
- Regression checks go through the normal affected entry point. The
  report names stock, extended, platform and fixture coverage where
  they apply.
- A reviewer can start from the PR or handoff, identify what ran on which
  revision, and reproduce or pick up the outstanding checks.

## Add to this handbook

Use the [maintenance guide](workflow.md#maintaining-the-handbook) to add a
workflow, example or observed tool difference. Add lasting project rules to
[AGENTS.md](../../AGENTS.md) and link to the relevant handbook section.
