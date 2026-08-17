# Host-side rules harness

Compiles the **real firmware rules engine** (`HeishaMon/src/rules/`) as a Linux binary and drives it against simulated heat-pump state, so a ruleset can be validated and behavior-tested without flashing a device. Rulesets normally have no lint/test tooling at all — validation happens only on-device at upload time; this harness runs the exact same `rule_initialize()` parse/validation on your PC and then goes further by executing scripted scenarios with assertions.

## Build

```
./build.sh
```

Needs `g++` and `python3` (no Arduino toolchain). `build.sh` first regenerates `valid_names.h` from the firmware's topic/command tables (`decode.h`, `commands.h`), so `@Name` typos in a ruleset fail the parse just like real unknown names would.

## Usage

```
./harness <rules.txt> parse            # parse + on-device-style validation only
./harness <rules.txt> <scenario.txt>   # run a scenario / test
../run_tests.sh                        # build + run every ruleset's tests/
```

Exit code `0` means the scenario ran and every expectation held (`2` parse failure, `3` failed expectation). Each example ruleset keeps its scenarios in a `tests/` directory next to its `rules.txt`.

## Scenario language

One op per line, `#` starts a comment:

| op | effect |
|---|---|
| `boot` | fire `System#Boot` |
| `silent <Topic> <val>` | set a topic value without firing its event (initial conditions) |
| `set <Topic> <val>` | set a topic value and fire `on @Topic` if it changed |
| `adv <seconds>` | advance the virtual clock, firing due timers in order |
| `time <hour> <minute>` | set the virtual wall clock for `%hour`/`%minute` (it advances with `adv`) |
| `pump` | simulated pump accepts pending `@Set…` commands: mapped read-back topics update and fire their events |
| `expectcmd <Cmd> <val>` | assert the oldest unchecked emitted command is exactly this |
| `expectnone` | assert there are no unchecked emitted commands |
| `vars` | dump persistent `#` globals |
| `note <text>` | print a marker line |

A scenario that uses `expect…` ops must account for every emitted command — ending with unchecked commands fails the run.

## Fidelity notes

- **Timers behave like `src/common/timerqueue.cpp`:** re-arming a pending timer replaces its delay, and `setTimer(id, 0)` **cancels** it (it never fires) — arming a non-pending timer with `0` is a no-op.
- **`@Set…` writes are recorded, not fed back** — like the real device, where a command's effect only appears on the read topic later. Use `pump` (for identity-valued commands like `SetMaxPumpDuty`) or explicit `set` lines to simulate the pump responding; commands with pump-side semantics (e.g. `SetOperationMode`) must always be simulated with `set`.
- **Set every topic your ruleset reads** in the scenario preamble. A topic left unset reads as `NULL`, which the engine treats inconsistently inside `&&` chains (see the `heishamon-rules` skill) — on a real pump these topics are always populated, so an unset topic in a scenario is almost always a test bug. The harness prints `!!! read of unset topic` warnings to stderr to catch this.
- `%hour`/`%minute` come from the scenario wall clock; timers run on a virtual clock, so a simulated day takes milliseconds. Local `$` variables are freed after every firing, persistent `#` globals survive until the end of the run — both mirror the device glue in `HeishaMon/rules.cpp`.

## Verifying a refactor is behavior-preserving

Run the ruleset's `tests/` against both the old and the new version:

```
./harness old-rules.txt ../<example>/tests/04-dhw-valve-target-curve.txt
./harness new-rules.txt ../<example>/tests/04-dhw-valve-target-curve.txt
```

Since the tests assert the exact command stream, a refactor that passes the same tests emits the same commands with the same values at the same virtual times. For ad-hoc comparison beyond the tests, both runs print `CMD`/`TIMER`/`TOPIC` trace lines to stdout that can be diffed directly.
