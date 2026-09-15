# Module Lifecycle System

**Status**: In progress
**Priority**: High
**Created**: February 12, 2026
**Updated**: September 15, 2026

## Purpose

Provide standard commands that let another module, a supervisor, or a GUI turn a
`Module2` instance on and off and query its state without terminating its process.

The central rule is:

> Operational off is not runtime shutdown. The control plane remains available
> while data processing is disabled.

`Module2::start()` and `Module2::stop()` remain runtime activation and final
teardown operations. They are not the remote on/off API.

## Implementation Status

Implemented:

- Built-in `LifecycleOn`, `LifecycleOff`, and `GetLifecycleStatus` messages.
- One dedicated lifecycle mailbox and command thread per module, including
  modules with no outputs.
- Deferred completion replies, idempotent on/off commands, and busy rejection.
- Data-thread-owned transitions with `on_enable()` and `on_disable()` hooks.
- Input unsubscribe/resubscribe and output-subscriber retention across off/on.
- Typed `Remote<T>` and input-handle `on()`, `off()`, and `status()` RPCs;
  raw system/instance helpers remain available for compatibility.
- Generated lifecycle endpoint metadata and ProcessLauncher resolution of the
  producer's primary-output lifecycle address.
- State-gated processing and idempotent final `stop()`.

Not yet implemented:

- Reset command and framework-state reset policy.
- Configurable startup-off and deterministic retry policy.
- Automatic launcher lifecycle orchestration and GUI controls.
- Event-based wakeup; disabled modules currently check transitions every 10 ms.

## RACK Reference Behavior

The legacy RACK implementation separates process lifetime from operational
state.

### Runtime structure

`RackModule::run()` starts two long-lived tasks:

- `cmd_task_proc()` receives commands until process termination.
- `data_task_proc()` owns operational transitions and calls `moduleLoop()` only
  while enabled.

The command task remains alive while the module is disabled. RACK tracks both
the actual `status` and requested `targetStatus`; the data task reconciles them.

### Command semantics

`RackModule::moduleCommand()` implements:

- `MSG_ON`: sets the target to on and stores one pending request. The data task
  replies after `moduleOn()` succeeds or fails. Repeated `MSG_ON` returns OK only
  when already enabled.
- `MSG_OFF`: sets the target to off and immediately acknowledges acceptance.
  The data task performs `moduleOff()` asynchronously, so status must be queried
  to observe completion.
- `MSG_GET_STATUS`: returns enabled, disabled, or error. Internal retry states
  are exposed as error.

`RackDataModule::moduleOn()` clears data history and listeners, then calls
`moduleLoop()` until the first output exists before completing the on transition.
`moduleOff()` removes all listeners and sends an error notification to each one.

When `moduleLoop()` fails, RACK enters an error state and, while the target is
still on, waits before retrying `moduleOn()`.

### Lessons to retain

- Keep command handling alive while operationally off.
- Separate requested state from actual state.
- Execute lifecycle hooks in the data-task context, serialized with processing.
- Do not report an on transition as complete before activation succeeds.
- Define subscription and buffered-data behavior for every transition.

### Behavior not to copy directly

- Random retry delays are unsuitable for deterministic systems.
- A second request must not silently overwrite the single pending request.
- On and off replies should have the same unambiguous completion semantics.
- Failures need structured error codes rather than one generic error message.
- First-data readiness should be an explicit policy, not an unconditional rule.

## Baseline Before Implementation

`Module2` currently has only runtime lifecycle operations:

```cpp
MyModule module(config);
module.start();
module.stop();
```

`start()` activates mailboxes, subscribes inputs, starts one command thread per
output, starts the data thread, and finally calls `on_start()`.

`stop()` calls `on_stop()`, sets `should_stop_`, unsubscribes inputs, stops output
mailboxes, and joins the data and command threads. It is terminal teardown in
practice:

- No command path remains available after `stop()`.
- `should_stop_` is not reset by `start()`.
- The destructor calls `stop()` again, so stop is not currently idempotent.
- The data thread can enter `process()` before `on_start()` runs.

Current system commands are output-scoped. Subscribe, unsubscribe, GetData,
GetNextData, parameter, and user commands arrive through each output's CMD
mailbox. Therefore:

- A module with no outputs has no command thread.
- A multi-output module has multiple command endpoints but one lifecycle.
- Using an output CMD mailbox would expose duplicate lifecycle endpoints.

The WORK mailbox is module-level in practice, but it carries outbound RPC and
replies. Adding another receiver there would create reply-routing races.

The in-process launcher only calls `start()` and `stop()`. `ProcessLauncher`
uses `SIGTERM`, then `SIGKILL` after a timeout. Neither exposes operational
on/off control.

## Gap Summary

| Capability | RACK | CommRaT today | Required |
|---|---|---|---|
| Command plane while off | Always alive | Stopped by `stop()` | Persistent control plane |
| Requested and actual state | Separate | None | Atomic target and actual states |
| Module command address | One | One per output | One lifecycle address per module |
| Transition completion | Mixed reply semantics | Not available | Stable-state replies |
| Status query | Enabled/disabled/error | None | Bounded status reply |
| Error recovery | Automatic retries | None | Deterministic policy |
| Input handling on off/on | Module-specific | Runtime only | Unsubscribe and resubscribe |
| Producer subscribers | Removed on off | Exist until teardown | Explicit retention policy |
| No-output module control | Supported | No command thread | Output-independent endpoint |
| GUI discovery | Proxy knows address | Descriptor lists I/O | Identity and capability metadata |

## Lifecycle Model

CommRaT needs two distinct state machines.

### Runtime lifecycle

Runtime lifecycle controls resource ownership:

```text
Constructed -> Started -> Stopping -> Stopped
```

- `start()` performs one-time mailbox and thread activation.
- `stop()` performs final teardown and must become idempotent.
- A stopped runtime is not remotely reachable or restartable in the first
  implementation.
- Process termination remains a launcher and `module_main` responsibility.

### Operational lifecycle

Operational lifecycle controls whether `process()` runs:

```text
Disabled --On--> Enabling --success--> Enabled
Enabled --Off--> Disabling --success--> Disabled
Enabling/Enabled/Disabling --failure--> Error
Error --Off--> Disabled
Error --On or retry--> Enabling
```

Proposed states and target:

```cpp
enum class LifecycleState : uint8_t {
    Disabled,
    Enabling,
    Enabled,
    Disabling,
    Error
};

enum class LifecycleTarget : uint8_t {
    Off,
    On
};
```

Actual and target state are distinct atomics. Transitional states are observable.
For backward compatibility, `start()` initially targets on unless configuration
explicitly requests startup in the off state.

## Module Identity and Addressing

Lifecycle is module-scoped. Add one dedicated lifecycle CMD mailbox and command
thread to every module, including modules with no outputs.

The address uses the module's primary output type, system ID, and instance ID,
plus a reserved lifecycle mailbox index:

```text
[primary output type:8][system:8][instance:8][lifecycle index 0xFF:8]
```

This preserves the existing CommRaT rule that modules with different output
types may share a system/instance pair. A no-output module uses type ID 0.

- `MultiOutputConfig` currently derives module-level resources from output 0.
  Its first output is therefore the lifecycle identity.
- A caller targeting an output-bearing module supplies its primary output type,
  matching the existing typed command API. A no-output target uses `void`.
- Validation must reject duplicate lifecycle addresses.

Do not receive lifecycle requests on the WORK mailbox. It is used for outbound
RPC and replies and must not have a competing receiver.

## Command Protocol

Lifecycle messages use `MessagePrefix::System` and currently unused IDs after
the parameter commands in `SystemSubPrefix::Control`. A dedicated CoreRaT
sub-prefix can replace this later without changing behavior.

Minimum command set:

```cpp
struct LifecycleOnPayload {};
struct LifecycleOffPayload {};
struct LifecycleResetPayload {};
struct GetLifecycleStatusPayload {};

enum class LifecycleResult : uint8_t {
    Success,
    AlreadyInState,
    Busy,
    Rejected,
    Failed,
    ShuttingDown
};

struct LifecycleCommandReplyPayload {
    LifecycleResult result;
    LifecycleState state;
    LifecycleTarget target;
    uint32_t error_code;
};

struct LifecycleStatusReplyPayload {
    LifecycleState state;
    LifecycleTarget target;
    uint32_t error_code;
    uint32_t retry_count;
    uint64_t state_since_ns;
};
```

Payloads are fixed-size and allocation-free. Human-readable diagnostics belong
in bounded logging or an explicitly bounded field.

### Reply contract

- `On` replies after state reaches `Enabled` or `Error`.
- `Off` replies after state reaches `Disabled` or `Error`.
- `Reset` replies after reset completes and reports the resulting state.
- `GetLifecycleStatus` replies immediately from atomic state.
- Repeated on/off requests for the stable current state are idempotent and
  return `AlreadyInState`.
- A conflicting request during transition returns `Busy`; it cannot replace the
  pending request.
- Sender-side RPC timeout controls waiting only. It cannot force a data-thread
  transition to complete safely.

The first implementation supports one pending transition in fixed storage.
Additional conflicting requests are rejected rather than queued dynamically.

## Transition Ownership and Concurrency

The lifecycle command thread validates requests and records the requested target.
It does not call user hooks directly. The data thread owns transitions so these
operations are serialized:

1. Stop invoking `process()`.
2. Run the lifecycle hook.
3. Update subscriptions and state.
4. Publish the final state atomically.
5. Send the correlated deferred reply.

The pending request uses fixed storage protected by CommRaT/CoreRaT primitives.
No allocation, exception, standard mutex, or unbounded wait is allowed in the
real-time path.

The data thread must be wakeable for transition requests. Timer waits should use
an interruptible platform event. Input-driven loops are currently bounded by
their mailbox poll timeout; until CoreRaT supports cancellation, that timeout is
also the upper bound on transition pickup latency.

## Hooks and Transition Semantics

Keep runtime hooks separate from repeatable operational hooks:

```cpp
protected:
    virtual LifecycleResult on_enable() { return LifecycleResult::Success; }
    virtual void on_disable() {}
    virtual LifecycleResult on_reset() { return LifecycleResult::Success; }
```

- `on_start()` remains a one-time runtime hook and runs before processing.
- `on_stop()` remains a one-time final teardown hook.
- Operational hooks may run repeatedly during process lifetime.
- Hooks must be bounded and allocation-free when running out of band. They
  report failure explicitly instead of throwing.

### On

1. Set state to `Enabling`.
2. Run `on_enable()`.
3. Subscribe continuous inputs; failure moves to `Error`.
4. Set state to `Enabled` and allow `process()`.
5. Send the command reply.

`Enabled` means activation succeeded and required subscriptions are established.
It does not guarantee first output. A separate readiness policy can provide that
RACK-like behavior when needed.

### Off

1. Set state to `Disabling` and prevent another `process()` call.
2. Unsubscribe continuous inputs.
3. Run `on_disable()`.
4. Set state to `Disabled`.
5. Send the command reply.

Mailboxes, command threads, and the data thread remain alive.

Existing output subscribers are retained while operationally off. They receive
no data and resume after re-enable. This differs from RACK because CommRaT has no
typed subscriber-revocation notification; silently removing a subscriber would
leave `ContinuousInput::is_subscribed()` stale.

Subscribe, GetData, and GetNextData requests received while disabled return an
explicit unavailable result. Existing reply payloads may need a bounded status
field to distinguish disabled from no matching data.

### Reset

Reset is not process restart.

- If enabled, first perform off.
- Run `on_reset()` and clear framework-owned output history, sequence counters,
  stale synced-input state, and lifecycle error state.
- Restore the target active before reset unless requested to remain off.
- Do not recreate mailboxes, threads, or the C++ object.

The exact framework-owned reset list must be tested before public exposure.

## Error and Retry Policy

The first implementation enters `Error` after activation failure or an explicit
`report_error(error_code)` from module code. It does not attempt to recover from
arbitrary C++ exceptions in an out-of-band path.

```cpp
struct LifecyclePolicy {
    bool auto_retry{false};
    uint32_t retry_delay_ms{1000};
    uint32_t max_retries{0};
};
```

- Default: remain in error until off, reset, or a new on request.
- Optional retries use a fixed delay and bounded count.
- Off cancels pending retry and transitions toward disabled.
- Status exposes the last bounded error code and retry count.

## Launcher and GUI Integration

Operational lifecycle and process supervision remain separate:

- The in-process Launcher starts runtimes, then may request operational state.
- ProcessLauncher retains SIGTERM and SIGKILL for final process shutdown. It may
  request off first for cleanup but cannot depend on an IPC reply to reap a
  failed process.
- A GUI sends lifecycle RPCs to the lifecycle address and queries status after a
  reconnect or timeout.
- Generated descriptors expose lifecycle capability and canonical identity.
  Runtime state is queried, not stored in descriptors.

Dependency-ordered orchestration is a later layer consuming module identities
and input dependencies from `AppDescription`, not part of the state machine.

## Implementation Plan

### 1. Runtime correctness

- Make `start()` and `stop()` state-checked and stop idempotent.
- Run `on_start()` before enabling processing.
- Preserve startup-on behavior by default.
- Test repeated stop and destructor-after-stop.

### 2. Identity, messages, and mailbox

- Define and register lifecycle messages.
- Add a fixed lifecycle mailbox and persistent command thread.
- Add address collision validation.

### 3. Operational state machine

- Add atomic actual and target states and one bounded pending request.
- Gate `process()` on enabled state.
- Add operational hooks.
- Implement deferred, correlated replies.

### 4. I/O transitions

- Unsubscribe inputs on off and resubscribe on on.
- Retain producer subscribers across off/on.
- Define reset behavior for history, sequence counters, and synced state.
- Return unavailable for data commands while disabled.

### 5. Tooling and supervision

- Export lifecycle identity and capability in descriptors.
- Add GUI controls and status.
- Let launchers request graceful off before final shutdown.
- Add dependency ordering separately.

## Required Tests

- On, off, status, and reset RPCs from another module and a raw client.
- Startup enabled by default and explicitly disabled.
- Idempotent requests and conflicting-transition rejection.
- Replies only after stable state is reached.
- Command and status access while disabled.
- No-output, single-output, and multi-output modules.
- Input unsubscribe on off and resubscribe on on.
- Existing output subscriber resumes after re-enable.
- Activation failure and bounded retry policy.
- Reset while enabled, disabled, and in error.
- Final `stop()` during every operational state.
- Duplicate lifecycle-address rejection.
- No allocation in transition and steady-state real-time paths.

## Open Decisions

- Optional readiness policy that waits for first output.
- A typed subscriber-revocation event for future remove-on-off behavior.
- Control sub-prefix now versus a dedicated CoreRaT lifecycle sub-prefix.
- CoreRaT wake primitive for bounded EVL transition latency.

## Non-Goals

- Off does not terminate or unload a process.
- On does not recreate a stopped runtime.
- Reset does not reconstruct the C++ object.
- Lifecycle control does not replace SIGTERM/SIGKILL supervision.
- No dynamic request queue or unbounded diagnostic string is introduced.

## Reference Implementation Files

CommRaT:

- `include/commrat/module2.hpp`
- `include/commrat/module/module_config.hpp`
- `include/commrat/module/helpers/address_helpers.hpp`
- `include/commrat/module/services/command_handler.hpp`
- `include/commrat/module/io/output/module_output.hpp`
- `include/commrat/messaging/system/system_registry.hpp`
- `include/commrat/launcher/launcher.hpp`
- `include/commrat/launcher/process_launcher.hpp`

Legacy RACK:

- `RackModule::run`, `cmd_task_proc`, `data_task_proc`, and
  `RackModule::moduleCommand` in `rack_module.cpp`
- `RackDataModule::moduleOn`, `RackDataModule::moduleOff`, and
  `RackDataModule::moduleCommand` in `rack_data_module.cpp`
- `RackProxy::getStatus` and RPC helpers in `rack_proxy.cpp`