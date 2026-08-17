# SameBoy Link — Technical Implementation Plan

This document translates the product roadmap into concrete implementation work against the current SameBoy codebase.

## Core architectural decisions

- Keep SameBoy `Core/` as close to upstream as practical.
- Remote Play is the primary Internet multiplayer mode: both linked Game Boys execute on the host.
- Native NetLink (one emulator on each PC with serial traffic over the Internet) is a later optional backend.
- The remote video source is Player 2's native completed SameBoy framebuffer, not a capture of the host window.
- The network stream remains at native Game Boy resolution whenever practical. Scaling and presentation filters belong on the client.

## Codebase findings

SameBoy exposes the serial primitives needed for a two-core link through `Core/gb.h`, `Core/gb.c`, `Core/timing.c` and `Core/memory.c`. `libretro/libretro.c` already provides the working reference for local multiplayer: two `GB_gameboy_t` instances, separate framebuffers/input/audio, cross-connected serial callbacks, and a cycle-delta scheduler.

The Windows SDL frontend (`SDL/main.c`) is currently centered on one global `GB_gameboy_t gb`, so the main frontend refactor is to introduce per-emulator slots and a session manager rather than modify serial emulation in the core.

## Remote Play architecture

```text
HOST                                      CLIENT
SameBoy Core A                            Controller P2
      |                                        |
 local serial cable                      input packets
      |                                        |
SameBoy Core B <-------------------------------+
      |
native P2 framebuffer
      |
encoder / packetizer
      |
network ---------------------------------> decoder
                                               |
                                      native framebuffer
                                               |
                                      SameBoy/SDL renderer
                                               |
                                      local scaling/filter
                                               |
                                            display
```

The Game Boy link itself never crosses the Internet in Remote Play mode. Only P2 input travels toward the host; P2 video/audio travels back to the client.

## Native-resolution video rule

Do not stream an already enlarged desktop/window image. The source should normally remain the native Game Boy framebuffer, typically 160x144 for DMG/CGB gameplay. A 32-bit 160x144 frame is 92,160 bytes uncompressed, roughly 5.25 MiB/s / 44 Mbit/s at ~59.7 fps before compression.

The client reconstructs the native framebuffer and performs presentation locally. This preserves:

- nearest-neighbour scaling
- integer scaling (2x, 3x, 4x, etc.)
- SameBoy color correction/palette handling where reusable
- SameBoy SDL/OpenGL shaders and display filters where reusable
- LCD/CRT/ghosting effects
- client-specific visual preferences independent of the host

A useful correctness test for a lossless mode is that the host P2 native framebuffer and reconstructed client framebuffer are bit-identical before client-side scaling/filtering.

## Video quality modes

Do not over-engineer the first release, but expose a codec/transport abstraction capable of quality presets.

### Balanced — default

Native-resolution stream with fast lightweight compression chosen for low latency. Prefer lossless when measured bandwidth is reasonable; near-lossless is acceptable if it materially improves constrained links without visibly damaging pixel art.

Goals: native 160x144 source, ~59.7 fps, client-side scaling/filtering, bounded queue depth, latency prioritized over maximum compression ratio.

### Pixel Perfect

Lossless native-resolution transport. The reconstructed client framebuffer must match the host framebuffer exactly before rendering filters. It may use more bandwidth but should produce the same nearest-neighbour/integer-scaled pixels as local rendering.

### Low Bandwidth

More aggressive compression for weak or metered connections. It may use a conventional low-latency video codec or another lossy representation, but decoded output is still treated as a native-resolution source by the client renderer rather than streaming a pre-scaled desktop image.

Quality modes affect only transport/encoding, never Game Boy emulation or link timing.

## Codec strategy

Do not hard-wire the architecture to H.264/H.265/AV1 initially. Traditional codecs may add RGB/YUV conversion, chroma subsampling, buffering and frame dependencies undesirable for pixel art.

Benchmark behind a common encoder interface:

1. raw framebuffer transport for LAN/reference testing
2. fast lossless frame compression
3. changed-region/tile or XOR-delta plus fast compression
4. conventional low-latency codec for Low Bandwidth mode

For delta modes, periodically send/recover a complete reference frame and immediately recover after packet loss. Dropping obsolete video frames is preferable to allowing latency queues to grow.

## Local link and scheduler

Introduce `EmulatorSlot` objects and a `GameSession`. Port libretro's proven serial callback bridge into `LocalLink`. Both cores initially execute on one scheduler thread using the libretro cycle-delta strategy; do not use independent emulator threads for local serial execution.

## Remote input

Send full current button state rather than key-down/key-up deltas. Each packet contains protocol/session ID, sequence number, button mask and client timestamp. Newer state supersedes older state. UDP is appropriate for the realtime prototype.

## Remote Play save-file handling

Remote Play runs both Game Boy instances on the host. Therefore the host is the authoritative owner of active emulator state and battery-backed save data during a session. The client does not need a ROM or active emulator save in the initial Remote Play implementation.

### V1: host-owned P1 and P2 saves

Each `EmulatorSlot` must have its own save path. Never allow both linked cores to read/write the same `.sav` file.

Example:

```text
Pokemon Crystal [P1].sav
Pokemon Crystal [P2].sav
```

or an equivalent session/profile directory layout.

This is required for games such as Pokémon where both linked systems represent independent cartridges/trainers and may save independently.

Rules:

- P1 and P2 always use distinct battery-save files.
- The same ROM may be loaded into both cores, but save paths remain separate.
- Writes must use SameBoy's normal battery-save semantics per core.
- A crash or disconnect must not cause P1 and P2 save paths to alias or overwrite one another.
- The host should preserve P2's save for future sessions unless the user explicitly deletes/replaces it.

### Future: client-owned portable P2 save

A later feature may allow the remote player to bring a personal `.sav` without requiring a ROM or local emulation.

Session start:

```text
CLIENT P2.sav
      |
secure/reliable file transfer
      v
HOST temporary P2 save
      |
SameBoy Core B
```

Session end:

```text
SameBoy Core B saves
      |
HOST finalized P2.sav
      |
verified transfer
      v
CLIENT replaces local copy only after validation
```

The client save is therefore a portable data file, not a second live emulator state.

### Save-transfer safety

Client-owned save transfer must be transactional:

1. Keep the client's previous save as a backup.
2. Transfer into a temporary file on the host/client rather than overwriting the destination directly.
3. Record file size and a cryptographic hash (for example SHA-256) before/after transfer.
4. Only promote the temporary file to the active `.sav` after the transfer and hash verify successfully.
5. If the session or network fails, retain the last known-good save.
6. Never accept arbitrary paths from the peer; save files must remain inside a controlled profile/session directory.
7. Save transfer uses a reliable channel and is separate from realtime UDP input/video traffic.

### Disconnect policy

For V1, a remote disconnect does not delete P2's host save. The host continues to own the last safely flushed P2 save.

For future client-owned saves, an unexpected disconnect should preserve the updated host-side temporary/backup state and allow explicit recovery/resume rather than silently replacing either side's last known-good copy.

### RTC and auxiliary cartridge data

Some games may persist more than a single plain `.sav` payload (for example RTC-related cartridge state). The save manager should therefore be designed around a per-slot persistent-data bundle rather than assuming every game is exactly one `.sav` forever. V1 may expose `.sav` first, but the API should leave room for SameBoy-supported RTC/auxiliary persistent files.

### Native NetLink distinction

This save policy applies to Remote Play. In future Native NetLink mode, each PC runs its own emulator and therefore normally owns its own local ROM and save data independently. Save files should not be synchronized automatically in Native NetLink mode.

## Audio

Keep per-core audio separate. Host local output may select P1/P2/mix; Remote Play normally sends P2 audio. Use small blocks and deliberately small jitter buffers.

## Latency telemetry

Instrument timestamps for client controller event, input send/host receive/application, P2 frame completion, encode, video send/receive, decode, texture upload and presentation request.

Initial engineering targets on typical modern PCs are under ~1 ms host emulator processing per produced dual-core frame where hardware permits, under ~1 ms encode, under ~1 ms decode, and roughly 1–3 ms local streaming-pipeline processing excluding network/frame/display waiting. These are measurement targets, not correctness assumptions.

## Implementation order

1. **T0 Windows baseline** — reproducible SDL build and timing instrumentation.
2. **T1 EmulatorSlot** — remove one-core frontend assumption while preserving single-player behavior.
3. **T2 LocalLink** — create Core B and port libretro serial bridge.
4. **T3 Dual scheduler** — cycle-delta scheduling and long-running link stability.
5. **T4 Multiplayer input** — separate P1/P2 controller ownership.
6. **T5 Dual presentation** — side-by-side local rendering and separate audio/framebuffer access.
7. **T6 Save isolation** — guaranteed independent P1/P2 persistent save paths before Internet sessions.
8. **T7 Remote input** — direct-IP/LAN P2 controller packets; both cores still run on host.
9. **T8 Raw/native video reference** — stream native P2 framebuffer on LAN and measure latency.
10. **T9 Quality/codec layer** — Balanced, Pixel Perfect and Low Bandwidth; client-side SameBoy rendering/filter paths where practical.
11. **T10 Remote audio** — bounded low-latency P2 audio.
12. **T11 Internet sessions** — room codes, endpoint negotiation, NAT traversal/P2P and relay fallback.
13. **T12 Client save transfer (optional)** — portable client-owned P2 saves with transactional transfer/recovery.
14. **T13 Latency optimization** — tune scheduling, queues, transport and presentation from telemetry.
15. **T14 Native NetLink** — optional serial-over-Internet backend after Remote Play is stable.

## First coding task

Introduce `EmulatorSlot` around the existing SDL `GB_gameboy_t` while keeping one active slot and preserving normal SameBoy behavior. Only after that builds and behaves identically should Core B and LocalLink be enabled.
