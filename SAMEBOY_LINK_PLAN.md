# SameBoy Link

A Windows-focused SameBoy fork for simple local and Internet Game Boy / Game Boy Color Link Cable multiplayer.

## Implementation checkpoint — 2026-08-18

The reproducible Windows build, multi-instance session layer, Local Link MVP and LAN Remote Play MVP are complete. The normal SDL frontend now exposes explicit single-player, Local Link, Remote Host and Remote Client modes, menu-driven direct-IP Host/Join/Disconnect, persistent independent P1/P2 keyboard/controller mappings and a client Escape menu for local settings. Protocol v4 provides full-state Player 2 input, lossless native-framebuffer streaming, adaptive 48 kHz stereo PCM, a dedicated client network thread and latency telemetry. Tetris for Game Boy and Tetris DX for Game Boy Color have exercised the local and remote paths, including a successful five-minute direct public-IPv4 session.

PCM is the stable audio path. Opus remains experimental and is deferred after producing worse real-world behavior in the current prototype. The UI/session boundary defined in `LINK_UI_ARCHITECTURE.md` is implemented and awaits final physical four-mode regression. Full client shader/filter reuse and video pacing are next, before authenticated/encrypted sessions and zero-configuration Internet connectivity.

## Product direction

Internet multiplayer will be implemented in two modes, in this order:

1. **Remote Play (primary Internet mode):** the host runs both linked SameBoy instances locally. The guest sends controller input to the host and receives only the guest Game Boy video/audio stream.
2. **Native NetLink (advanced optional mode):** each PC runs its own SameBoy instance and Link Cable serial events are synchronized over the network.

Remote Play is the first Internet target because the emulated Link Cable remains entirely local and therefore retains normal SameBoy timing and compatibility. Native NetLink remains a later experimental/advanced option for users who prefer local rendering and can tolerate stricter synchronization requirements.

## Design principles

- Keep the SameBoy emulation core as close to upstream as practical.
- Build Local Link first; it is the correctness reference for all multiplayer modes.
- Preserve physical Link Cable semantics by keeping both emulated consoles on the host for the first Internet implementation.
- Keep streaming, networking and session management outside the emulator core.
- Optimize end-to-end input latency aggressively, but never at the expense of deterministic Link Cable behavior.
- Keep the Windows UI deliberately simple.
- Preserve the upstream license and make upstream merges practical.

## Target user flow

### Local Link
1. Open ROM.
2. Select **Local Link**.
3. Configure Player 1 and Player 2 controllers.
4. Run two linked Game Boy instances in one process.

### Internet — Remote Play
1. Host opens a ROM and selects **Host Online Game**.
2. Host receives a short room code.
3. Guest selects **Join Online Game** and enters the room code.
4. Host runs both linked Game Boy instances locally.
5. Host controls Player 1 locally.
6. Guest controller input is sent to the host with the lowest practical latency.
7. Host streams Player 2 video and audio back to the guest.
8. The Game Boy Link Cable itself never crosses the Internet.

### Internet — Native NetLink (later option)
1. Each PC runs one SameBoy instance.
2. Clients verify protocol version and ROM identity/hash.
3. Establish direct P2P where possible with relay fallback.
4. Synchronize Link Cable serial events using a transport designed around emulated timing.

## Primary architecture — Remote Play

```text
HOST PC

 SameBoy P1  <--- local Link Cable --->  SameBoy P2
     |                                  |      |
 local input                         video/audio encode
                                            |
                                      Internet stream
                                            |
                                            v
                                        GUEST PC
                                            |
                                      video/audio decode
                                            |
                                      remote display
                                            |
                                     controller input
                                            |
                                            +-----> HOST
```

The host is authoritative for both emulated Game Boys. There is no Game Boy state synchronization across the Internet in Remote Play mode.

## Component architecture

```text
Windows Frontend
      |
Multiplayer Manager
      |
 +----+-----------------------------+
 |                                  |
Local Session                 Online Session
 |                                  |
Dual SameBoy Core            RemotePlay Host/Client
 |                           |              |
Local Link                   Input      Video/Audio
                             Network       Stream
                                  \        /
                                  Session Service
                              (rooms / traversal / relay)
```

Later, Native NetLink adds a separate transport path without replacing Remote Play.

## Phase 0 — Baseline

- Build the existing SameBoy Windows/SDL frontend without functional modifications.
- Document compiler/toolchain requirements.
- Establish a repeatable debug build.
- Add a minimal test ROM/link test procedure.

**Exit criterion:** upstream-derived Windows build runs known GB/GBC ROMs correctly.

## Phase 1 — Local Link MVP

- Create two `GB_gameboy_t` instances in one process.
- Connect their serial/link interfaces locally.
- Support loading the same ROM into both instances.
- Support separate P1/P2 controller mappings.
- Initially favor split-screen or two-view presentation.
- Add basic link diagnostics.

Diagnostics should expose at least:

- transfer direction
- serial byte/value
- relevant serial control state
- emulated timestamp/cycle
- transfer count

**Exit criterion:** a known two-player Link Cable game can establish a session and exchange data reliably for an extended play session.

## Phase 2 — Dual-instance session layer

Turn the Local Link experiment into a reusable session object that owns:

- both SameBoy instances
- ROM/session configuration
- controller routing
- local Link Cable connection
- P1/P2 framebuffers
- audio sources
- pause/reset/session lifecycle

This layer is the foundation for both local multiplayer and Remote Play hosting.

**Exit criterion:** the frontend can start and stop a dual-console session through one clean API without special-case emulator code spread across the UI.

## Phase 3 — Remote input prototype

Implement the guest-to-host control path first, before video streaming.

Requirements:

- low-overhead input packets
- sequence numbers
- timestamps
- controller state rather than fragile one-shot key events where appropriate
- stale/reordered packet handling
- disconnect detection
- measured RTT and jitter
- optional LAN direct-connect test mode

The host remains authoritative. Guest input is applied only to Player 2.

**Exit criterion:** a second PC on the LAN can control Player 2 reliably while the host displays both Game Boy screens locally.

## Phase 4 — Low-latency video/audio streaming

Stream only the guest view required for normal play.

Initial goals:

- capture Player 2 framebuffer directly from SameBoy rather than screen-grabbing the Windows desktop
- preserve the native 160x144 image internally
- scale only for presentation/encoding where beneficial
- use a low-latency encoder configuration
- avoid unnecessary frame queues
- transmit audio with a small resilient buffer
- decode and present immediately on the guest
- expose dropped frames, encode/decode time and stream latency in debug mode

Codec/transport choice should be based on measured end-to-end latency on Windows, not codec compression efficiency alone. Hardware encoding may be used where it materially reduces latency, but a broadly compatible fallback is required.

**Exit criterion:** two PCs on a LAN can play a Link Cable title with the guest seeing/hearing Player 2 remotely.

## Phase 5 — Internet sessions

Add a small coordination service for:

- room creation
- short room codes
- peer discovery
- session metadata
- NAT traversal coordination
- relay fallback
- connection authentication/token handoff

Prefer direct P2P for input and media when possible. Relay should be a fallback, not the required normal path.

The service does not emulate the Game Boy and does not need ROM contents.

**Exit criterion:** two users on separate Internet connections can host/join without manually entering IP addresses.

## Phase 6 — Remote Play latency optimization

Measure the complete guest input-to-photon path:

```text
Guest controller
 -> input sampling
 -> network uplink
 -> host receive
 -> SameBoy P2 input/emulation
 -> framebuffer produced
 -> encode
 -> network downlink
 -> decode
 -> presentation
```

Optimize each stage independently.

Priorities:

1. Send input immediately; do not wait for video frames.
2. Minimize buffering and queued frames.
3. Prefer latest-frame behavior over accumulating stale video.
4. Tune encoder for latency rather than quality/bitrate efficiency.
5. Keep decoding and presentation asynchronous from network receive.
6. Adapt bitrate/resolution only when network conditions require it.
7. Keep audio buffering as small as reliability permits.
8. Instrument every stage so latency regressions are measurable.

**Exit criterion:** Remote Play remains responsive on realistic Internet connections and degrades gracefully under jitter or bandwidth pressure.

## Phase 7 — Native NetLink (optional advanced mode)

Only after Remote Play is stable, add direct emulated Link Cable networking.

Conceptually:

```text
PC A                               PC B
SameBoy A                          SameBoy B
   |                                  |
Serial endpoint <--- Internet ---> Serial endpoint
```

Requirements may include:

- ordered serial events
- cycle/timestamp information
- protocol versioning
- sequence numbers
- RTT/jitter measurement
- adaptive receive buffering
- clock-drift correction
- packet batching without changing serial semantics
- ROM hash verification
- deterministic save-state/restore tests before any rollback work

Possible later latency techniques:

- speculative execution
- save-state snapshots
- rollback/re-simulation

Remote Play remains available even if a particular game is incompatible with Native NetLink.

**Exit criterion:** selected compatible games can run one SameBoy instance per PC without requiring host-side video streaming.

## User-facing Windows UI

Target top-level UI:

```text
Play
Local Link
Host Online Game
Join Online Game
Settings
```

Host/join should default to **Remote Play**. Native NetLink can later appear as an advanced connection mode.

Online session status should show useful information such as:

- Connected / Waiting / Reconnecting
- Ping
- Direct or Relay
- stream bitrate
- dropped frames

Advanced diagnostics belong behind a developer/debug option.

## Remote Play debug mode

Expose:

- input send/receive rate
- RTT and jitter
- P2 input age at application
- emulation/frame production timestamp
- encode time
- network media delay
- decode time
- presentation queue depth
- dropped/skipped frames
- audio buffer depth
- estimated input-to-photon latency
- direct/relay state

## Testing strategy

Maintain four test levels.

### Core/link tests
Deterministic tests for local serial transfer and dual-instance behavior.

### Remote input tests
Inject latency, jitter, packet loss, duplication and reordering into guest controller traffic.

### Media network simulation
Test video/audio behavior under controlled bandwidth, latency, jitter and packet loss.

Suggested profiles:

- LAN / near-zero impairment
- 20 ms RTT / low jitter
- 50 ms RTT / moderate jitter
- 100 ms RTT / moderate jitter
- 150+ ms RTT / adverse case
- controlled packet loss and bandwidth reduction

### Real games
Maintain a compatibility matrix containing game, GB/GBC mode, local-link result, Remote Play result, latency profile and known issues. Native NetLink results are added later as a separate column.

## Milestones

### M0 — Build baseline
Unmodified Windows build and documented toolchain.

### M1 — Local Link
Two SameBoy instances communicate reliably in one Windows process.

### M2 — Dual-instance session layer
Reusable host/local multiplayer session architecture.

### M3 — Remote Input
A second PC can control Player 2 over LAN.

### M4 — Remote Video/Audio
Guest receives a low-latency Player 2 stream over LAN.

### M5 — Internet Remote Play MVP
Room-code host/join, P2P where possible, relay fallback.

### M6 — Remote Play optimization
Instrument and reduce input-to-photon latency; improve network adaptation and resilience.

### M7 — Polished Windows release
Simple multiplayer UI, controller setup, packaging and compatibility documentation.

### M8 — Native NetLink prototype
Optional one-emulator-per-PC Link Cable networking after the Remote Play path is stable.

## Historical first implementation task — complete

The original first coding task was deliberately narrow:

> Identify SameBoy's existing serial/link API and the current local/libretro link implementation, then create the smallest Windows-side experiment that runs two cores and connects them locally.

After Local Link works, the next Internet-specific task is **remote Player 2 input**, not Link Cable networking and not video streaming. This lets us validate the control path before adding media complexity.

## Next implementation tasks

1. Complete physical regression of single-player, Local Link, Remote Host and Remote Client UI flows.
2. Reuse SameBoy's full shader/filter pipeline in the Remote Play client.
3. Improve video pacing and evaluate low-latency compression without regressing the lossless reference path.
4. Add session authentication and encryption before treating direct Internet play as a public feature.
5. Replace manual IP/port forwarding with coordination, NAT traversal and relay fallback.
