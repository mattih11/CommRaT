# Typed Remote Module Handles

**Status**: Implemented
**Created**: 2026-09-15

## Goal

Give a module a typed handle to every configured remote dependency without
manually repeating system IDs, instance IDs, mailbox addresses, message IDs, or
buffer sizes.

The intended user experience is the useful part of RACK proxies without a
hand-written proxy class for every module type:

```cpp
class Pilot : public App::Module2<
    Output<PilotData>,
    Input<Scan2dData>,
    SyncedInput<GPSData>,
    Remote<SensorData>> {
protected:
    LifecycleResult on_enable() override {
        auto& scan2d = input<Scan2dData>();
        auto& gps = input<GPSData>();
        auto& sensor = remote<SensorData>();

        if (!scan2d.on() || !gps.on()) {
            return LifecycleResult::Failed;
        }

        auto calibration = sensor.send_command(CalibrateSensor{.offset = 0.2F});
        return calibration ? LifecycleResult::Success : LifecycleResult::Failed;
    }

    void process(const Scan2dData& scan, Synced<GPSData> gps,
                 PilotData& output) override {
        auto response = input<0>().send_command(SetScanRange{.range_mm = 8000});
        // Normal processing uses scan, gps, and output.
    }
};
```

`Input<T>` and `SyncedInput<T>` remain data-acquisition specifications.
`Remote<T>` is the command/control-only dependency specification; it replaces
the misleading idea of a `CmdInput<T>`. All three expose the same remote control
capability.

## Current Foundation

Most of the required information already exists:

- `Input<T>` and `SyncedInput<T>` are initialized from `ModuleConfig` with the
  producer system and instance IDs.
- Both runtime input classes inherit `CmdInput<Registry, T>`, which already
  stores that identity and implements type-checked `send_command()`.
- `DataWithCommands<Data, Commands...>` is the compile-time contract that
  determines which commands a remote output accepts.
- `Module2::get_input<N>()` and `send_command_to_input<N>()` already prove the
  indexed path, but the command capability is not presented as a coherent
  user-facing handle.
- WORK mailboxes use `Registry::max_message_size`, so they can safely send and
  receive every registered request and reply. This is conservative in memory,
  but it does not under-size command buffers.
- Output CMD mailboxes are already specialized from the commands associated
  with their output type.

The implementation should refactor and expose these pieces rather than create
per-module proxy classes.

## Terminology and Types

### RemoteHandle

Rename the runtime concept from `CmdInput<Registry, T>` to
`RemoteHandle<Registry, T>`. Keep `CmdInput` as a temporary deprecated alias
while internal users migrate.

`RemoteHandle` owns no mailbox. It contains only:

- a non-owning reference to the module's shared RPC client;
- the target output CMD address;
- the target module lifecycle address;
- target system and instance IDs for diagnostics;
- the default timeout.

It exposes:

```cpp
template<typename Command>
RpcResult<typename Command::Reply> send_command(
    const Command& command,
    Duration timeout = Duration::zero());

RpcResult<LifecycleOnReplyPayload> on(Duration timeout = Duration::zero());
RpcResult<LifecycleOffReplyPayload> off(Duration timeout = Duration::zero());
RpcResult<LifecycleStatusReplyPayload> status(Duration timeout = Duration::zero());
```

`send_command()` is constrained at compile time to the command tuple associated
with `T`. The handle computes request and reply IDs from the registry; users do
not provide them.

`RpcResult<T>` should distinguish transport timeout, send failure, protocol
mismatch, and a received application reply. Returning only `std::optional`
loses information needed by lifecycle orchestration and diagnostics.

### Remote Specification

Add a command/control-only specification:

```cpp
template<typename T>
struct Remote {
    using Type = T;
    static constexpr bool is_remote = true;
};
```

`CreateIOInstance<Registry, Remote<T>>` creates a `RemoteHandle<Registry, T>`.
It is not counted as a process input, does not affect execution-mode inference,
does not subscribe, and does not contribute a process argument.

The application description gains a separate `remotes` collection. Its source
identity is configured once in the same way as an input connection. `Remote<T>`
entries must not be hidden in `inputs`, because they do not carry data.

## Accessors

Use template access because a heterogeneous collection cannot provide a
statically typed C++ result through ordinary runtime syntax such as `input[0]`.

```cpp
auto& scan2d_by_index = input<0>();
auto& scan2d_by_type = input<Scan2dData>();
auto& sensor_by_index = remote<0>();
auto& sensor_by_type = remote<SensorData>();
```

Type-based access is enabled only when the type occurs exactly once. Duplicate
types require index-based access. Keep `get_input<N>()` as an implementation
and compatibility API; make `input<N>()` the concise public spelling.

Named reflected access can remain available through `inputs()` and a future
`remotes()`, but it should not be the only API because generated field names can
collide when a type appears more than once.

`Input<T>` and `SyncedInput<T>` runtime objects publicly expose the
`RemoteHandle` operations, so these are equivalent:

```cpp
input<0>().send_command(command);
input<Scan2dData>().send_command(command);
```

## Address Resolution

### Output Commands

The output command address is fully determined by:

- the referenced output type ID from the registry;
- the source system and instance IDs from configuration;
- CMD mailbox index `0`.

This is already computed by `CmdInput` and requires no user input.

### Module Lifecycle

Lifecycle needs additional care. The current lifecycle address uses the target
module's primary output type plus mailbox index `0xFF`. An input may reference a
secondary output, so its data type is not always a valid lifecycle anchor.

The launcher must resolve each input/remote connection against the source
module descriptor and inject the source module's lifecycle address into the
runtime configuration. Users still specify the source only once. Direct
`ModuleConfig` construction may provide an explicit resolved lifecycle address;
it must not silently assume that the referenced data output is primary.

Longer term, a type-independent module identity would remove this asymmetry, but
that requires changing the existing rule that different output types may share
the same system/instance pair. It is outside this handle refactor.

## RPC Ownership and Concurrency

The current synchronous helpers call `receive()` directly on the shared WORK
mailbox. Two concurrent callers can consume each other's replies; source
filtering does not help after the wrong caller has already removed a message.
Lifecycle RPC and command RPC currently duplicate this pattern.

Introduce one module-owned `RpcClient<Registry>` as the only code allowed to
perform request/reply transactions on WORK.

The first implementation should use one CommRaT `Mutex` around the complete
send/wait transaction:

1. Lock the RPC client.
2. Assign a non-zero sequence number.
3. Send the request.
4. Receive until the expected source, reply message ID, and sequence number
   match, or the deadline expires.
5. Unlock and return `RpcResult<T>`.

All subscription, get-data, user-command, parameter, and lifecycle RPC paths
must use this client. Serializing RPC is bounded, allocation-free, and prevents
reply theft without adding another thread. Nested or cyclic synchronous RPC is
not supported and must be documented.

If measurements later require concurrent outstanding calls, replace the lock
with one dispatcher thread and a fixed-capacity pending-request table keyed by
sequence number. Do not let multiple callers receive directly from WORK.

## Mailbox Sizing

Mailbox receive storage is derived from compile-time message sets:

- WORK can send any registered request for compatibility, but its receive slots
  use `Registry::max_reply_message_size`; request and data payload sizes do not
  affect its allocation.
- Per-output CMD mailboxes derive their allowed request/reply set from
  `DataWithCommands` and size themselves with `TypedMailbox::max_message_size`.
- The lifecycle mailbox is already restricted to lifecycle payloads.

Narrowing WORK below all registered replies would require removing or further
constraining the compatibility helper that can target any command-bearing output
in the application registry.

## Introspection Contract

Current introspection is split and is not sufficient as a standalone command
contract for another project:

- the registry schema can emit numeric IDs and wire layouts for registered
  request/reply payloads;
- `*.module.json` associates command type names with an output index;
- deployment configuration supplies system and instance IDs;
- mailbox indices and final command/lifecycle addresses are only conventions
  derived in C++ code.

Extend the module descriptor with optional structured endpoint metadata while
preserving the existing fields for compatibility:

```json
{
  "command_endpoints": [
    {
      "output_index": 0,
      "output_type": "Scan2dData",
      "output_type_id": 7,
      "mailbox_index": 0,
      "commands": [
        {
          "request_type": "SetScanRange",
          "request_id": 196609,
          "reply_type": "SetScanRange::Reply",
          "reply_id": 262143
        }
      ]
    }
  ],
  "lifecycle_endpoint": {
    "anchor_output_index": 0,
    "anchor_type_id": 7,
    "mailbox_index": 255
  }
}
```

The numeric values above are illustrative. IDs must always come from the
compiled registry, never from hand-maintained documentation or arithmetic in an
external tool. Explicitly emit reply IDs even though CommRaT can derive them.

The deployment validation step should combine module descriptors with
`app.json` and produce a resolved manifest containing concrete CMD and lifecycle
addresses for every module instance and connection. This gives GUIs and other
projects one runtime-oriented artifact and lets the launcher reject registry,
type, or address mismatches before starting processes.

Add a registry fingerprint to both schema and descriptor metadata. Two projects
may communicate only when their relevant type names, message IDs, and layouts
match; a type name alone is not a wire contract.

## Implementation Plan

### 1. Stabilize RPC Transport

- Add `RpcClient<Registry>` around the WORK mailbox.
- Match replies by source address, reply ID, and sequence number.
- Route existing subscription, synchronized get-data, parameter, command, and
  lifecycle helpers through it.
- Add tests with concurrent callers and deliberately reordered replies.

### 2. Introduce the Handle

- Extract `RemoteHandle` from `CmdInput` and retain a compatibility alias.
- Move lifecycle `on()`, `off()`, and `status()` onto the handle.
- Return `RpcResult<T>` with explicit transport status.
- Verify invalid commands fail at compile time and valid commands need no
  address or message-ID arguments.

### 3. Expose Configured Dependencies

- Add `input<N>()` and unique-type `input<T>()` accessors.
- Add the `Remote<T>` specification and `remote<N>()` / `remote<T>()` accessors.
- Extend I/O tuple metadata so command-only remotes are initialized but excluded
  from process signatures, input counts, and execution-mode selection.
- Extend `AppDescription`, `ModuleConfig`, and descriptor inspection with remote
  dependencies.

### 4. Resolve Lifecycle Endpoints

- Match each configured dependency to its source module/output during launcher
  validation.
- Inject the source primary-output lifecycle address into each handle.
- Reject ambiguous, missing, or type-incompatible source connections before
  module startup.
- Test lifecycle control through both primary and secondary output references.

### 5. Publish Complete Introspection

- Add command request/reply IDs and endpoint mailbox indices to module
  descriptors.
- Ensure generated registry schemas include all command and lifecycle payloads
  used by the application.
- Generate a resolved deployment manifest with concrete addresses.
- Add compatibility tests that parse these artifacts without linking CommRaT.

### 6. Documentation and Migration

- Replace direct `send_command_to_input<N>()` examples with handle examples.
- Document synchronous RPC restrictions and real-time blocking behavior.
- Deprecate `CmdInput` only after all internal call sites migrate.
- Add a RACK pilot-style example that controls chassis, joystick, and scan
  dependencies without manually constructing proxies or repeating addresses.

## Acceptance Criteria

- A configured data input can call `on()`, `off()`, `status()`, and any command
  associated with its output type without specifying an address or ID.
- A `Remote<T>` dependency provides the same control API without subscribing to
  or fetching data.
- Sending a command not associated with `T` fails at compile time.
- Multi-output sources use the correct output CMD endpoint and primary-output
  lifecycle endpoint.
- Concurrent API calls cannot steal or discard another RPC's reply.
- Every receiving mailbox is compile-time sized for all payloads it accepts.
- External tools can discover request/reply IDs and resolved endpoint addresses
  from generated artifacts without reproducing CommRaT C++ address logic.
- Existing `Input<T>`, `SyncedInput<T>`, and launch configurations remain
  source-compatible during migration.
