---
name: wavecrate-develop
description: Develop and verify a requested WaveCrate feature or fix, using agents only when independent implementation work warrants it.
disable-model-invocation: true
---

# WaveCrate development

Act as the lead for the work requested by the user. This is a reusable development
process, not a milestone definition. Use ordinary built-in agents where separate
context and genuine parallel work are useful. No custom agent profiles are needed.
Prioritise a working result and fast feedback. Do not turn a small feature into
a lengthy testing, review or automation project.

## Requested work

$ARGUMENTS

If invoked by name from a fleet prompt, take the requested work from that prompt.
If no concrete task was supplied, ask for it before editing or delegating. Do not
select the next milestone automatically. A request to plan or explain is not a
request to implement.

## Establish scope

1. Read AGENTS.md, README.md, the relevant parts of PLAN.md and the code affected
   by the request. Keep project rules in those files rather than duplicating them
   here.
2. Identify the requested outcome, exclusions, dependencies and acceptance checks.
   Ask about decisions that materially affect behaviour or scope. Do not expand
   the request to an entire milestone or later features.
3. Inspect existing changes and preserve unrelated work. If Git is unavailable,
   report that limitation; do not initialise a repository or create commits unless
   asked.
4. For multi-step work, track tasks and actual prerequisites in the session todos.
   Keep feature scope and status in PLAN.md. Do not create another planning file
   unless the user requests one.
5. Establish the minimum shared interfaces needed before dividing implementation.
   The lead owns shared interfaces, application wiring, CMake and documentation.

## Delegate bounded work

- First decide whether the task has substantial, independent implementation slices.
  If it is a small change to one component, a focused fix, or a continuous
  investigation, do the work directly without starting a fleet. In particular,
  avoid parallel workers when they would edit or repeatedly inspect the same
  waveform, browser or transport component.
- Use agents only when separate work can proceed in parallel with clear ownership,
  such as independent components or a substantial test/documentation task that
  does not depend on unfinished implementation. Include the coordination and
  integration cost in the decision; agent count is not a speed target.
- Use at most two active workers at once, excluding the lead. This includes any
  reviewer or command-running worker. Honour stricter session limits. Do not alter
  model preferences, permissions or global/repository settings to run the task.
- Use ordinary general-purpose implementation workers. The lead assigns roles
  such as UI or audio for this task; roles are not permanent profiles.
- Delegate only substantial, self-contained work with clear ownership. Handle
  small changes or one continuous investigation directly instead of manufacturing
  parallel tasks.
- Give each worker the requested outcome, relevant context, exact allowed files,
  interfaces, exclusions, acceptance criteria and a clear stopping point. Tell it
  to read AGENTS.md and the relevant project documentation.
- Each writable file has one owner. Workers must request reassignment before
  editing outside their scope. They do not edit the lead's files, revert another
  worker's changes or run builds without an explicit hand-off.
- Workers must not spawn other agents. The lead alone coordinates dependencies.
  Do not create agent profiles, start another coordinator session or use dynamic
  workflows to implement this skill.
- Keep the current model selection and normal subagent preferences unless the user
  requests an override. Reuse a worker only for follow-ups on its existing task.
- While workers run, do independent lead work. Wait for completion notifications
  rather than repeatedly polling or duplicating their implementation.
- Require a concise hand-off: changed files, behaviour delivered, checks actually
  run and any remaining blockers. A worker's completion claim is not acceptance.

## Integrate and verify

1. Integrate only after prerequisites are complete. Resolve shared wiring and
   ownership issues. The lead reviews the change and owns the single build/test
   runner; relevant source edits must finish before it starts.
2. Default to one incremental Debug build and the smallest tests covering the
   changed behaviour. Reuse out and existing tools. Add only focused regression
   cases, not a new testing framework. Documentation-only changes need no build.
3. Do not automatically add Release builds, GUI automation, hardware test suites,
   extra reviewer agents or repeated full checks. Use them only when explicitly
   requested or necessary for a specific unresolved risk or acceptance criterion.
   Ask before optional validation would significantly delay a working result.
4. Fix failures within scope and rerun only affected checks. Once the necessary
   checks pass, stop testing. Report any remaining verification limits accurately;
   never claim unobserved UI behaviour or audible playback. Do not install SDKs
   or system tools to bypass a blocker.
5. Update README.md and PLAN.md to match what was delivered and verified. Clean up
   task-owned temporary helpers, preserve unrelated processes and hand over the
   working result without starting another review or test cycle.

## Report

Lead with the delivered outcome. State material limitations, failed or blocked
verification, and any necessary next step. Do not present placeholders, untested
controls or unobserved audio playback as working features. Stop at the requested
scope rather than starting the next item in the roadmap.
