# SameBoy Link

A Windows-focused SameBoy fork for simple local and Internet Game Boy / Game Boy Color Link Cable multiplayer.

## Design principles

- Keep the SameBoy emulation core as close to upstream as practical.
- Put multiplayer policy in a separate link abstraction/network layer.
- Make local link the correctness reference for online link.
- Prioritize compatibility and deterministic behavior before latency optimizations.
- Keep the Windows UI deliberately simple.
- Preserve the upstream license and make upstream merges practical.

## Target user flow

### Local
1. Open ROM.
2. Select **Local Link**.
3. Configure Player 1 and Player 2 controllers.
4. Run two linked Game Boy instances in one process.

### Internet
1. Open ROM.
2. Select **Host Online Game** or **Join Online Game**.
3. Host receives a short room code.
4. Clients verify protocol version and ROM identity/hash.
5. Establish a direct peer-to-peer connection when possible; use relay fallback when necessary.
6. Measure latency/jitter and select an appropriate link buffer.
7. Start synchronized emulation.

## Proposed architecture

```text
Windows Frontend
      |
Multiplayer Manager
      |
Link Abstraction
   /        \
Local       Network (LinkNet)
Link          |
 |          UDP/P2P
 |            |
SameBoy    Lobby / Relay
Core(s)
```

The emulator core should not know about rooms, NAT traversal, matchmaking, or UI. It should communicate through a small link interface.

## Phase 0 — Baseline

- Build the existing SameBoy Windows/SDL frontend without functional modifications.
- Document compiler/toolchain requirements.
- Establish a repeatable debug build.
- Add a minimal test ROM/link test procedure.

**Exit criterion:** upstream-derived Windows build runs known GB/GBC ROMs correctly.

## Phase 1 — Local Link MVP

- Create two `GB_gameboy_t` instances in one process.
- Connect their serial/link interfaces through a local transport.
- Support loading the same ROM into both instances.
- Support separate P1/P2 controller mappings.
- Initially favor a simple split-screen or two-view presentation.
- Add basic link diagnostics.

Diagnostics should expose at least:

- transfer direction
- serial byte/value
- relevant serial control state
- emulated timestamp/cycle
- transfer count

**Exit criterion:** a known two-player Link Cable game can establish a session and exchange data reliably for an extended play session.

## Phase 2 — Link abstraction

Introduce a transport-independent interface between SameBoy serial emulation and multiplayer transport.

Conceptually:

```text
SameBoy serial
     |
LinkEndpoint
     |
 +---+----------------+
 |                    |
LocalTransport   NetworkTransport
```

Required properties:

- ordered serial events
- emulated timestamps/cycle positions
- sequence numbers
- explicit connection/session state
- deterministic local transport
- logging hooks

**Exit criterion:** Local Link works through the abstraction with no direct P1-to-P2 special-case path.

## Phase 3 — LinkNet prototype

Implement the first Internet transport.

Initial priorities:

- UDP for latency-sensitive session traffic
- sequence numbers
- packet loss/reordering detection
- latency measurement
- jitter measurement
- adaptive receive buffer
- disconnect/reconnect handling where safe
- protocol version negotiation
- ROM identity/hash verification

Do not begin with rollback. Correct buffered synchronization is the first target.

**Exit criterion:** two machines on a LAN can play through NetworkTransport rather than LocalTransport.

## Phase 4 — Internet sessions

Add a small coordination service for:

- room creation
- short room codes
- peer discovery
- session metadata
- NAT traversal coordination
- relay fallback

Prefer direct P2P transport once peers are connected. The service should not emulate the Game Boy.

**Exit criterion:** two users on separate Internet connections can host/join without manually entering IP addresses.

## Phase 5 — Latency optimization

Start from a correctness-first adaptive jitter buffer.

Potential mechanisms:

1. Adaptive buffering based on RTT and jitter.
2. Emulator clock-drift correction to keep peers close on the emulated timeline.
3. Packet batching without changing serial semantics.
4. Save-state snapshots at carefully selected synchronization points.
5. Speculative execution where safe.
6. Rollback/re-simulation for late remote information if measurements show it is necessary and compatible.

Rollback must be introduced only after we have deterministic tests proving that restore + replay produces the same state.

**Exit criterion:** acceptable playability at realistic Internet RTT/jitter while maintaining link correctness.

## Phase 6 — User-facing Windows UI

Target top-level UI:

```text
Play
Local Link
Host Online Game
Join Online Game
Settings
```

Online session status should show only useful information, for example:

- Connected / Waiting / Reconnecting
- Ping
- Link buffer
- Direct or Relay connection

Advanced diagnostics belong behind a developer/debug option.

## Debug mode

A dedicated Link Debug view should eventually expose:

- local/remote emulated time
- serial events
- sender/receiver
- RTT
- jitter
- packet loss/reordering
- buffer depth
- clock correction
- desync detection
- rollback count (if rollback is implemented)

Logs should be exportable for reproducing compatibility problems.

## Testing strategy

Maintain three test levels:

### Core/link tests
Deterministic tests for serial transfer and transport ordering.

### Network simulation
Inject controlled latency, jitter, packet loss, duplication, and reordering without requiring a real Internet connection.

Suggested profiles:

- 0 ms / no loss — reference
- 20 ms RTT / low jitter
- 50 ms RTT / moderate jitter
- 100 ms RTT / moderate jitter
- 150+ ms RTT / adverse case
- controlled packet loss/reordering cases

### Real games
Maintain a compatibility matrix containing game, GB/GBC mode, connection result, gameplay result, RTT profile, and known issues.

## Milestones

### M0 — Build baseline
Unmodified Windows build and documented toolchain.

### M1 — Local Link
Two SameBoy instances communicate reliably in one Windows process.

### M2 — Transport abstraction
Local multiplayer runs entirely through LinkEndpoint/LocalTransport.

### M3 — LAN LinkNet
Two PCs communicate over the network with diagnostics and adaptive buffering.

### M4 — Internet MVP
Room-code host/join, P2P where possible, relay fallback.

### M5 — Latency optimization
Clock synchronization, improved buffering, then evaluate speculative/rollback techniques.

### M6 — Polished Windows release
Simple multiplayer UI, controller setup, updater/release packaging, compatibility documentation.

## First implementation task

The first coding task is deliberately narrow:

> Identify SameBoy's existing serial/link API and the current libretro/local-link implementation, then create the smallest Windows-side experiment that runs two cores and connects them locally.

No Internet code should be added until this works and can be tested deterministically.
