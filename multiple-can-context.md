# Multiple CAN contexts

The context types are defined in
[Structs.h](Src/Open_SAE_J1939/Structs.h). CAN operations select their backend
through the originating `J1939` instance; hardware callbacks and backend state
belong to its `CAN_Context`.

Related discussion:
[Open-SAE-J1939 issue #92](https://github.com/DanielMartensson/Open-SAE-J1939/issues/92).
The [maintainer's response](https://github.com/DanielMartensson/Open-SAE-J1939/issues/92#issuecomment-6348105438)
requests a direct API change and validation of the `.txt` examples.

## Goal

Support independent CAN interfaces with separate hardware bindings and protocol
instances. Devices on different buses may use identical CAN identifiers and ECU
addresses. Frames and transport sessions must remain associated with their bus.

Applications own the context structures and can allocate them statically. This
also supports an AVR32/FreeRTOS application with a separate actuator on each bus.

## State ownership

| Structure | Scope | State |
|---|---|---|
| `CAN_Context` | One CAN interface | Hardware callbacks, application-defined `uint32_t Tag`, internal feedback buffers and indices, SocketCAN handle and initialization state |
| `J1939` | One protocol/ECU instance | ECU addresses, identification and diagnostic data, transport state, application callback and a pointer to its `CAN_Context` |

Separate CAN interfaces use separate `CAN_Context` instances. Multiple `J1939`
instances can reference the same context when they use the same interface.

For example:

| Protocol instance | CAN context | Tag | Controller |
|---|---|---|---|
| `first_ecu` | `first_can_context` | `1U` | CAN controller 1 |
| `second_ecu` | `second_can_context` | `2U` | CAN controller 2 |

`Tag` is a freely assignable integer identifying an application resource, such as
a controller or channel. Applications can use different callbacks for each
context, or shared callback functions that select a controller through `Tag`.

`CAN_Context` contains the storage for both backends. On platforms with one-byte
`bool`, its feedback arrays occupy 3,584 bytes, plus callbacks, handles, indices
and alignment. Budget this storage per interface, including on embedded targets.

## Callback contract

Every callback receives the originating `J1939 *j1939` as its first argument.
This is an explicit instance pointer, similar to `this`. The callback can access
the protocol state and the CAN interface through `j1939->can_context`.

Forward declarations allow the two structures to reference each other:

```c
typedef struct J1939 J1939;
typedef struct CAN_Context CAN_Context;
```

The four callback members of `CAN_Context` are:

```c
void (*Callback_Function_Send)(J1939 *j1939, uint32_t ID,
                               uint8_t DLC, uint8_t data[]);
void (*Callback_Function_Read)(J1939 *j1939, uint32_t *ID,
                               uint8_t data[], bool *is_new_message);
void (*Callback_Function_Traffic)(J1939 *j1939, uint32_t ID,
                                  uint8_t DLC, uint8_t data[], bool is_transmit);
void (*Callback_Function_Delay_ms)(J1939 *j1939, uint8_t milliseconds);
```

The application callback is a member of `J1939`:

```c
void (*Callback_Function_Application)(J1939 *j1939, SAE_Application_Info info);
```

A send operation invokes the callback using the actual caller instance:

```c
j1939->can_context->Callback_Function_Send(j1939, ID, 8, data);
```

The callback can read `j1939->can_context->Tag`. Passing the instance on every
call also identifies the originating ECU when several instances share a CAN
context.

## Public API changes

Use the following API migration. Existing applications must update the affected calls
and callback implementations. The implementation uses explicit instance
arguments throughout. Existing source and binary interfaces are not compatible.

| API | Change |
|---|---|
| `CAN_Context_Init` | Initialize a caller-owned `CAN_Context *context` before use |
| `CAN_Send_Message` | Add `J1939 *j1939` as the first argument |
| `CAN_Send_Request` | Add `J1939 *j1939` as the first argument |
| `CAN_Read_Message` | Add `J1939 *j1939` as the first argument |
| `CAN_Delay` | Add `J1939 *j1939` as the first argument |
| `CAN_Set_Callback_Functions` | Add the target `CAN_Context *context` and use the new callback signatures |
| `SAE_J1939_Set_Application_Callback_Function` | Add `J1939 *j1939` and use the new application callback signature |
| `socketcan_setup` | Add `CAN_Context *context` as the first argument |
| `socketcan_transmit` | Add `CAN_Context *context` as the first argument |
| `socketcan_receive` | Add `CAN_Context *context` as the first argument |
| `socketcan_close` | Release the socket belonging to one context |
| `STM32_PLC_CAN_Transmit`, `STM32_PLC_CAN_Get_ID_Data` | Add `J1939 *j1939` to the platform integration |
| `QT_USB_Transmit`, `QT_USB_Get_ID_Data` | Add `J1939 *j1939` to the platform integration |
| `STM32_PLC_Start_CAN` | Add the owning `STM32_CAN_Context *context` |
| `QT_USB_set_serial_handler` | Add the owning `QT_USB_Context *context` |

The public global `Callback_Function_Application` is replaced by the member of the
corresponding `J1939` instance. Applications that assign this global directly must
migrate to the instance member or the updated setter.

The protocol entry points that already receive `J1939 *` can retain their
signatures. Their internal hardware calls pass that same instance, including
transport acknowledgements, aborts, traffic notifications and delays.

Internal helpers use explicit state: `Internal_Transmit`, `Internal_Receive`
and `socketcan_is_initialized` operate on the selected `CAN_Context`.

Adding members to the public `J1939` structure changes its binary layout. All
applications must be rebuilt against the updated headers and library.

## Initialization and lifetime

- Call `CAN_Context_Init(&context)` before attaching protocol instances and
  starting communication. It clears callbacks, feedback buffers, indices and
  `Tag`, sets `can_initialized` to `false` and `can_socket` to `-1`.
- Assign each instance's `can_context` before any operation that accesses CAN.
  Register the callbacks required by the selected backend and protocol
  operations before starting traffic.
- Keep the context and its backend resources alive while any attached instance
  uses them. Reinitializing an ECU must respect other users of a shared context.
- The interface owner calls `socketcan_close(&context)` after all users stop.
  Closing one ECU through `Open_SAE_J1939_Closedown_ECU` only saves its ECU
  information; closing a shared CAN interface is a separate operation. Close an
  open socket before reinitializing its context.
- Serialize concurrent operations on the same protocol instance or shared
  backend state, for example through a single owner task or application locking.
  Independent contexts keep bus selection separate; they do not by themselves
  provide synchronization.

For example, two independent interfaces can be initialized as follows:

```c
CAN_Context first_can_context;
CAN_Context second_can_context;
J1939 first_ecu = {0};
J1939 second_ecu = {0};

CAN_Context_Init(&first_can_context);
CAN_Context_Init(&second_can_context);
first_can_context.Tag = 1;
second_can_context.Tag = 2;
first_ecu.can_context = &first_can_context;
second_ecu.can_context = &second_can_context;
```

Register hardware callbacks with
`CAN_Set_Callback_Functions(&first_can_context, send, read, traffic, delay)`.
Register an application callback with
`SAE_J1939_Set_Application_Callback_Function(&first_ecu, application)`.

Registered send, read and delay callbacks take precedence over the selected
built-in backend. With `INTERNAL_CALLBACK`, a missing send callback produces
`STATUS_SEND_ERROR`; a missing read callback reports no new frame. Traffic and
delay callbacks are optional. CAN functions reject a missing ECU/context or
invalid data pointer without invoking callbacks.

For SocketCAN, call `socketcan_setup(&first_can_context, "can0")` and
`socketcan_setup(&second_can_context, "can1")` to select different interfaces.
Check the return value before starting communication. Without explicit setup,
each context lazily opens `SOCKETCAN_IFNAME`. Transmit errors map to
`STATUS_SEND_ERROR`. After `socketcan_close`, explicitly set up a custom
interface again before reuse.

The STM32 and Qt examples extend `CAN_Context` by placing it as the first
member of an application-owned driver structure. Initialize an
`STM32_CAN_Context` through `STM32_PLC_Start_CAN(&context, hcan, &j1939)`.
The board integration must implement `STM32_PLC_Get_CAN_Context(hcan)` to map
each interrupt's controller handle to its owning context. Put the context type
and updated driver declarations into the board's shared header.
For Qt, initialize `QT_USB_Context` through
`QT_USB_set_serial_handler(&context, serial_port)` and attach
`j1939.can_context = &context.can_context`. Include the updated declarations
from `can_to_usb.h` in the platform integration.

A shared receive queue consumes frames when read. Delivering the same frame to
several ECU instances on a physical bus requires a suitable receive dispatcher;
sharing a context alone does not provide that dispatch mechanism.

## Migration checklist

1. Allocate and initialize a context for each interface and attach every ECU
   before its first CAN operation, including `Open_SAE_J1939_Startup_ECU`.
2. Add the originating `J1939 *` to hardware and application callbacks.
3. Pass the context to hardware callback registration and SocketCAN functions;
   pass the ECU instance to raw CAN calls and application callback registration.
4. Update STM32/Qt platform driver declarations and per-interface resource
   bindings. Use `Tag` or an application-owned extended context to identify them.
5. Rebuild the library and applications together.
6. Ensure context lifetime, receive dispatch and synchronization match the
   application's task and bus model.

## Verification plan

| Check | Expected result |
|---|---|
| Existing software identification example | Two simulated ECUs exchange identification data through their shared feedback context |
| Existing `transport_eom` regression | End-of-message acknowledgements retain the correct byte and packet totals |
| Two independent buses using identical identifiers and ECU addresses | Each instance transmits and receives only through its assigned interface |
| Interleaved multi-packet transfers on separate buses | Buffers, sequence numbers, CTS/EOM state and abort handling remain associated with the correct instance |
| Shared callback implementation with different `Tag` values | Every hardware callback receives the expected `J1939 *` and selects the intended controller |
| Per-instance application callbacks | Identification and proprietary notifications reach the callback of the receiving ECU |
| Separate feedback contexts | Reading or transmitting on one context does not consume or enqueue frames in another context |
| `.txt` examples | Updated examples compile and run in their applicable environment; hardware-dependent examples are exercised with the corresponding hardware |

Validation uses the project's existing compiler settings. Record the tested
backend and execution environment for hardware-dependent examples.

## Running the regression tests

```sh
cmake -S tests -B build/tests
cmake --build build/tests
ctest --test-dir build/tests --output-on-failure
```

The default suite runs `transport_eom`, `multiple_can_contexts` and
`feedback_contexts`. Linux also runs `socketcan_contexts`, which builds the
real SocketCAN backend with mocked OS calls to verify descriptor/interface
isolation, return codes and cleanup after setup errors.

To additionally compile the host `.txt` examples and run the finite examples:

```sh
cmake -S tests -B build/tests -DOPENSAE_J1939_TEST_EXAMPLES=ON
cmake --build build/tests
ctest --test-dir build/tests --output-on-failure
```

These example targets use the `NO_PLATFORM` feedback backend; examples with
registered callbacks override it. `Open SAE J1939/Main.txt` is compile-only
because its event loop does not terminate. VIN and application-layer callback
examples need a Linux SocketCAN build. The STM32, Qt USB and SD-card snippets
require their platform headers, drivers and hardware.

## Validation results

- Windows / MinGW GCC 8.1: all three core regression tests pass. The default
  feedback `main` example exchanges the expected software identification.
- Linux / WSL / GCC 13.3: all four core regression tests pass, including the
  SocketCAN backend with mocked socket operations.
- Twenty finite host `.txt` examples compile and run successfully.
  `Open SAE J1939/Main.txt` compiles without running its endless event loop.
- `Proprietary A.txt` currently has an existing build error: it references
  `MAX_PROPRIETARY`, while the current header defines `MAX_PROPRIETARY_A`.
  Enabling all host-example targets exposes this error. The other host tests
  can be run with `ctest --test-dir build/tests -E example_SAE_J1939_Proprietary_A_txt`.
- The VIN client/server and C++ application-callback example compile against the
  native Linux SocketCAN library. The C++ example's `--help` path runs.
- Live SocketCAN exchange could not be exercised because the local WSL kernel
  lacks the `vcan` module. STM32, Qt USB and SD-card execution still requires the
  corresponding platform environment and hardware.
