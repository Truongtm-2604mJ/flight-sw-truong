# Lab 6.2 - Code generation (Path B)

`modelgen.py` reads `../lab-6.1-model/mode_machine.yaml` and writes three files:

- `generated/mode_sm.h`: enums, types, API and action hook prototypes;
- `generated/mode_sm.c`: the state machine;
- `generated/mode_sm_trace.json`: which generated lines each model element produced, and the requirement IDs.

The files in `generated/` are never edited by hand. Each starts with a banner carrying the SHA-256 of the model and of the generator, and `make check-generated` fails if they differ from a fresh generation. To change behaviour, change the model (or the generator) and run `make generate`.

```
make generate          # regenerate (needs Python 3 and PyYAML: sudo apt install python3-yaml)
make check-generated   # generated/ matches the model: no hand edit
make test              # generator rejects bad models, flight warning set, clang-tidy
```

The C build does not need Python: the generated files are committed.

## What the generator checks before it writes anything

`tests/test_modelgen.py` mutates the real model one way at a time and checks that each mutation is rejected (11 cases):

- every (state, event) pair is handled or explicitly ignored;
- for a handled pair, the guards cover every combination of inputs, so a latched event can never stay pending forever;
- priorities are unique within each state;
- no event is both handled and ignored in the same state;
- guards use only declared inputs and parse correctly;
- no self transitions (internal transitions instead);
- every state is reachable from a boot state;
- every `semantics` key is stated, with a value the generator implements.

## Generator configuration

These are the equivalent of the code generator settings in a commercial tool. They are fixed in `modelgen.py`, so changing one is a change to a controlled file.

| Setting | Value | Why |
|---|---|---|
| Memory | Static only. The caller allocates `mode_sm_t`; no `malloc`, no static mutable state in the module | Flight rule; several instances possible |
| Loops | One, bounded by the event count (6). No recursion | Bounded execution time |
| Transitions per step | 1 | Model semantics |
| Arithmetic | Unsigned 32-bit bit masks; no floating point; `U` literal suffix | No FPU dependency; MISRA C style |
| Switches | On `uint32_t`, every case listed, `default` used for invalid state recovery | `-Wswitch-enum`; no unreachable `default` |
| Policy | None in generated code: every action is an extern hook | Logic and policy separated |
| Ground command bookkeeping (v1.1) | `GROUND` mask; an ignored ground command is reported when an external transition fired | MM-140, closes D-05 |
| Unused constructs | Not emitted: no empty exit functions, no guard code on unguarded transitions | No dead code to justify |
| Diagnostics | Name strings only under `MODE_SM_WITH_NAMES` (test builds) | Not in flight builds |

## How model elements map to generated code

| Model element | Generated artefact |
|---|---|
| `states[]` | `mode_sm_state_t`: `MODE_SM_NOMINAL`, `MODE_SM_DEGRADED`, `MODE_SM_SAFE` |
| `events[]` | `mode_sm_event_t`: `MODE_SM_EV_<NAME>`; `class` becomes the `ONE_SHOT` and `LATCHED` masks |
| `inputs[]` | fields of `mode_sm_inputs_t` |
| state `on_entry` | `enter_<STATE>()`: sets the state, then calls `mode_sm_action_<a>()` for each action in order |
| state `on_exit` | `exit_<STATE>()`; emitted only for SAFE, the only state with exit actions |
| state `ignore` | `IGNORED[<STATE>]` mask |
| transition `Txx` | `fire_Txx()`: exit, transition actions, entry, report, result |
| transition priority and guard | one `if / else if` arm in `select_<FROM>()`, in priority order; the guard is compiled to C (`!in->fdir_inhibited`) |
| internal transition | `fire_Txx()` without exit or entry, result kind `MODE_SM_STEP_INTERNAL` |
| `semantics.input_sampling` | `mode_sm_step()` copies the pending set and the inputs into locals first |
| `semantics.unconsumed_*` | the consumption block at the end of `mode_sm_step()` |
| `boot_states` | `mode_sm_init()` switch |
| `invalid_state_recovery` | `default:` of the `mode_sm_step()` switch |

Each `fire_Txx()` starts with a comment repeating the model line and its requirement IDs, for example `/* T02: NOMINAL -> SAFE on fault_critical [!fdir_inhibited], priority 2  MM-040 */`. `mode_sm_trace.json` records the exact line range of every element; Lab 6.4 uses it.

## Static checks and their findings

| Check | First generated version | After the generator fix |
|---|---|---|
| GCC 13, `-std=c99 -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wswitch-enum -Wshadow -Wcast-qual -Wstrict-prototypes -Wmissing-prototypes -Werror` | **failed**: `-Wswitch-enum` on the `switch` over the state enum (the `_COUNT` member is not a case) | 0 warnings: switches on `uint32_t` |
| clang-tidy 18 (`bugprone`, `cert`, `clang-analyzer`, `misc`, `readability`, `hicpp` switch/bitwise checks) | **36 findings** (`static-analysis/clang-tidy-before.txt`): 34 lower case `u` suffixes, 1 missing direct `<stdint.h>` include, 1 cognitive complexity of 30 in `mode_sm_step()` | **0** (`static-analysis/clang-tidy-after.txt`): upper case suffix, explicit include, one `select_<STATE>()` per state |

Each finding was fixed in the generator, never in the output. That is the discipline the session asks for, and it also fixes every future model at once. No MISRA checker was available (cppcheck's MISRA add-on and commercial tools were not installed), so MISRA conformance is claimed only for the rules the table above covers.

## Integration into the RTEMS application (`../app/`)

`app/fdir6.c` is Session 5's `fdir.c` with the hand written mode manager replaced:

- **Requests become events.** `fdir_mode_request()` posts an event to the machine, mapping causes fail closed: only GROUND becomes a `tc_*` event.
- **One step per supervisor cycle.** The supervisor task calls `mode_sm_step()` once per 2 s cycle, after the liveness check and with the mode lock held.
- **Hooks drive the outputs.** Payload, sun pointing, schedule suspension and housekeeping and monitoring rates, which Session 5 derived from the mode, are now set by the action hooks.
- **The inhibit is now in the application.** It is a timer, not a mode, so it lives in `fdir6.c`.

`make check-no-hand` confirms that `modes.c` is not linked: no `mode_request` symbol, and `mode_sm_step` present.

The Session 5 system scenarios run unchanged against this build (`logs/`). At boot the build announces `Mode manager: GENERATED from vehicle_mode v3 (model sha256 46d8922ca14ac291...)`.

| Scenario | Host simulation | Note |
|---|---|---|
| `check_flight.py` (flight build) | 11/11 | |
| `campaign.py`: six injections | all six detected and recovered | `logs/campaign-host-sim-generated.md` |
| `campaign.py`: false positive run, 300 s | 0 detections | |
| `gs_modes.py` (Lab 5.3 scenario) | 49/51 | The 2 failures are divergence D-07: the transition now happens at the next supervisor cycle, up to 2 s after the TC, and these two checks assume it is immediate |
| MM-150 latency, TM(1,7) to mode report | max 1.999 s (limit 3.0 s), PASS | `latency.py` on the `gs_modes` log, `logs/latency-host-sim.md` |

**On the target (RTEMS 6 / LEON3, SIS).** The session text says QEMU; as in Sessions 3 to 5 the target here is SIS. `make rtems` builds `lab6-test.exe` and `lab6-flight.exe` with the same flags as Session 5.

`app/run-sis.sh` builds both images, runs the same three scenarios under SIS and measures MM-150 on the SIS log. It writes `logs/sis-*.log`, `logs/campaign-sis-generated.md`, `logs/latency-sis.md` and the summary `logs/sis-summary.md`:

```
cd ../app && RTEMS_PREFIX=$HOME/Work/01.Code/rtems/rtems/6 ./run-sis.sh
```

The campaign runs with `--nv-volatile` on SIS, because the NV record is RAM there and a reset ends the simulation (Session 5 design note, Section 6).
