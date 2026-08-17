# SameBoy Link — Technical Implementation Plan

This document translates the product roadmap into concrete implementation work against the current SameBoy codebase.

## 1. Codebase findings

### Core serial/link API

SameBoy already exposes the primitives needed for a two-core Game Boy link:

- `GB_set_serial_transfer_bit_start_callback()`
- `GB_set_serial_transfer_bit_end_callback()`
- `GB_serial_get_data_bit()`
- `GB_serial_set_data_bit()`
- `GB_disconnect_serial()`

Relevant files:

- `Core/gb.h`
- `Core/gb.c`
- `Core/timing.c`
- `Core/memory.c`

The serial transfer state is maintained per `GB_gameboy_t`, including `serial_master_clock`, `serial_mask` and `serial_count`. `Core/memory.c` starts transfers when SC is written, and `Core/timing.c` advances the serial master clock and invokes the callbacks at bit boundaries.

### Existing working dual-core reference

`libretro/libretro.c` already implements local two-player link cable emulation with:

- `GB_gameboy_t gameboy[2]`
- two independent frame buffers
- two vblank callbacks
- two independent input paths
- per-core audio handling
- serial bit callbacks that cross-connect the two cores
- a scheduler that alternates `GB_run()` between the two cores based on returned cycle deltas until both reach vblank

The serial bridge is effectively:

```text
Core A internal clock
  serial_start1(bit)
        |
        v
  cached outgoing bit
        |
  serial_end1()
        |
        +-- GB_serial_get_data_bit(Core B)
        +-- GB_serial_set_data_bit(Core B, A_bit)

and symmetrically for Core B.
```

This is the implementation we should adapt rather than designing a new local-link protocol.

### SDL/Windows frontend

`SDL/main.c` is currently architected around a single global:

```c
GB_gameboy_t gb;
```

It also owns singleton state for:

- pixel buffers
- filename/save path
- battery state
- rewind/turbo state
- callbacks
- input handling
- rendering
- menus/commands

The Windows build uses the SDL frontend and is produced through the root `Makefile`. On Windows the project already uses SDL2 and defaults to XAudio2/SDL audio backends.

The main architectural work is therefore not in the SameBoy core. It is converting the SDL frontend from a single-emulator singleton into a session containing one or two emulator instances.

---

## 2. Core design rule

Do not put networking, room codes, streaming, controller ownership or multiplayer policy into `Core/`.

`Core/` should remain upstream-compatible.

All new logic belongs in the SDL/Windows frontend and new fork-specific modules.

Target layering:

```text
SDL UI / Windows frontend
        |
GameSession
        |
+-------+---------------------------+
|                                   |
EmulatorSlot[0]                 EmulatorSlot[1]
|                                   |
GB_gameboy_t                       GB_gameboy_t
|                                   |
+----------- LocalLink ------------+
        |
RemotePlayHost / RemotePlayClient
        |
Transport
```

---

## 3. Phase T0 — build and instrumentation baseline

### Goal

Produce a repeatable Windows debug build before changing runtime behavior.

### Work

1. Document Windows toolchain used by the current Makefile.
2. Add a fork-specific compile-time marker such as `SAMEBOY_LINK_BUILD` only in frontend code.
3. Add a simple debug log category for session/link/stream diagnostics.
4. Record current frame rate, audio sample rate and framebuffer format at runtime.

### Files

- `Makefile`
- new `SDL/link_debug.h`
- new `SDL/link_debug.c`

### Exit criterion

Unmodified one-player behavior works and a debug build prints useful frontend timing diagnostics.

---

## 4. Phase T1 — introduce EmulatorSlot

### Goal

Remove the assumption that the SDL frontend owns exactly one Game Boy instance.

### New type

Create:

```c
typedef struct EmulatorSlot {
    GB_gameboy_t gb;

    uint32_t pixel_buffer[2][256 * 224];
    uint32_t *active_pixels;
    uint32_t *previous_pixels;

    char *rom_path;
    char *battery_path;

    bool battery_dirty;
    unsigned battery_timer;

    unsigned player_index;
    bool vblank_occurred;
} EmulatorSlot;
```

Do not immediately move every SDL global into this object. Migrate only state that is genuinely per-emulator.

### New files

- `SDL/emulator_slot.h`
- `SDL/emulator_slot.c`

### Initial API

```c
bool emulator_slot_init(EmulatorSlot *slot, GB_model_t model, unsigned player_index);
void emulator_slot_deinit(EmulatorSlot *slot);
bool emulator_slot_load_rom(EmulatorSlot *slot, const char *path);
void emulator_slot_reset(EmulatorSlot *slot);
```

### Migration strategy

First replace the single global `GB_gameboy_t gb` with:

```c
static EmulatorSlot slots[2];
static unsigned active_slot_count = 1;
```

Preserve one-player behavior before enabling slot 1.

### Exit criterion

Normal SDL SameBoy still behaves identically with `active_slot_count == 1`.

---

## 5. Phase T2 — port the proven libretro local-link bridge

### Goal

Run two SameBoy cores in the SDL frontend and connect their serial ports locally.

### New module

- `SDL/local_link.h`
- `SDL/local_link.c`

### Structure

```c
typedef struct LocalLink {
    EmulatorSlot *a;
    EmulatorSlot *b;
    bool outgoing_bit[2];
    bool connected;
} LocalLink;
```

### Callback behavior

Adapt the proven callbacks from `libretro/libretro.c`.

For A:

```c
start(A, bit):
    link->outgoing_bit[A] = bit;

end(A):
    incoming = GB_serial_get_data_bit(&B->gb);
    GB_serial_set_data_bit(&B->gb, link->outgoing_bit[A]);
    return incoming;
```

Mirror for B.

No network abstraction should be introduced at this stage. We want the shortest path to a known-correct local implementation.

### Important constraint

Callbacks need a reliable way to resolve `GB_gameboy_t *` back to an `EmulatorSlot` and `LocalLink`. Prefer frontend-owned lookup/context rather than modifying `GB_gameboy_t`.

For two fixed slots a simple pointer comparison is sufficient initially.

### Exit criterion

A known link game successfully performs transfers between both cores.

---

## 6. Phase T3 — dual-core scheduler

### Goal

Keep both emulators on the same emulated timeline.

### Reference behavior

The libretro frontend already uses the correct basic approach: keep a signed cycle delta and alternate `GB_run()` between cores depending on which side is ahead.

Conceptually:

```c
signed delta = 0;

while (!slot0.vblank_occurred || !slot1.vblank_occurred) {
    if (delta >= 0) {
        delta -= GB_run(&slot0.gb);
    }
    else {
        delta += GB_run(&slot1.gb);
    }
}
```

We should port this concept instead of running each core on an independent OS thread.

### Why one scheduler thread first

Two independent emulator threads would introduce host scheduling jitter into serial timing and make correctness harder to prove.

For the first implementation:

- both cores run on the main emulation thread
- networking/encoding may later use worker threads
- serial link remains deterministic and synchronous

### New module

- `SDL/game_session.h`
- `SDL/game_session.c`

### Session modes

```c
typedef enum {
    SESSION_SINGLE,
    SESSION_LOCAL_LINK,
    SESSION_REMOTE_PLAY_HOST,
    SESSION_REMOTE_PLAY_CLIENT,
    SESSION_NATIVE_NETLINK,
} GameSessionMode;
```

### Exit criterion

Both cores run for long sessions without accumulating timing drift and both reach vblank correctly.

---

## 7. Phase T4 — two-player input ownership

### Goal

Separate local controls for P1 and P2.

### Current issue

`SDL/main.c` currently routes SDL controller/keyboard events directly to a single `GB_gameboy_t`.

### New model

Introduce an intermediate input state:

```c
typedef struct PlayerInputState {
    bool keys[GB_KEY_MAX];
    uint32_t sequence;
    uint64_t timestamp_us;
} PlayerInputState;
```

SDL events update `PlayerInputState`, not the core directly.

At deterministic update points:

```text
InputState P1 -> slot[0]
InputState P2 -> slot[1]
```

This is mandatory for Remote Play because P2 input will later come from the network instead of SDL.

### New files

- `SDL/multiplayer_input.h`
- `SDL/multiplayer_input.c`

### Exit criterion

Two local controllers independently control the two linked Game Boys.

---

## 8. Phase T5 — dual video/audio presentation

### Local video

Initially implement side-by-side rendering only:

```text
+----------------+----------------+
|    Player 1    |    Player 2    |
|     160x144    |     160x144    |
+----------------+----------------+
```

Do not create two native Windows windows in the first prototype. One SDL window with two textures is simpler and is already conceptually aligned with libretro's dual-screen path.

### Remote Play requirement

Each slot must expose its raw completed frame independently before composition/scaling.

The streamer should consume P2's native framebuffer, not a screenshot of the composed SDL window.

This avoids:

- GPU readback
- scaling latency
- window compositor latency
- accidentally streaming P1's view

### Audio

The host should maintain separate audio paths:

- local output can be P1, P2 or mixed
- remote stream normally sends P2 audio

Use the same per-core concept already present in `libretro/libretro.c` rather than mixing first and trying to split later.

### Exit criterion

Local dual link has independent video and identifiable per-core audio data.

---

## 9. Phase T6 — Remote Play transport skeleton

### Goal

Prove network input before adding video compression.

Architecture:

```text
CLIENT                        HOST

Controller
   |
Input packet ----------------> RemoteInputQueue
                                   |
                                   v
                               Player 2
                               SameBoy core
```

### Protocol split

Use separate logical channels:

1. control/session
2. realtime input
3. video
4. audio
5. statistics/keepalive

The implementation may share a socket/connection later, but the semantics must remain separate.

### Input packet

First version:

```c
struct RemoteInputPacket {
    uint32_t protocol_version;
    uint32_t session_id;
    uint32_t sequence;
    uint16_t buttons;
    uint64_t client_timestamp_us;
};
```

Send current full button state, not key-down/key-up deltas. This makes packet loss recovery trivial: a newer state supersedes an older one.

### Transport choice for prototype

Use UDP for realtime input.

Control/session setup may use a reliable channel later.

### Exit criterion

A second PC can control Player 2 while both Game Boys still execute locally on the host.

---

## 10. Phase T7 — low-latency video path

### Design principle

The source is only 160x144 at Game Boy frame rate. Optimize for latency, not compression ratio.

Pipeline:

```text
SameBoy P2 framebuffer
      |
completed-frame callback
      |
encoder worker
      |
packetizer
      |
UDP transport
      |
client jitter queue (very small)
      |
decoder
      |
SDL texture upload
      |
present
```

### First codec strategy

Implement the streaming interface before committing the session code to one codec:

```c
typedef struct VideoEncoder VideoEncoder;

typedef struct {
    bool (*init)(VideoEncoder *, unsigned width, unsigned height, unsigned fps);
    bool (*encode)(VideoEncoder *, const uint32_t *pixels, uint64_t frame_id);
    void (*shutdown)(VideoEncoder *);
} VideoEncoderOps;
```

For the first network proof, support an intentionally simple/raw or lightweight intra-frame transport on LAN if necessary. After end-to-end timing instrumentation is in place, choose the production low-latency codec based on measured encode/decode delay on Windows hardware.

Do not scale the source to 720p/1080p before encoding. Encode native or a small integer-scaled frame only if required by the selected encoder.

### Frame policy

- no B-frames
- no long decode dependency chain
- latest-frame-first behavior under congestion
- dropping an obsolete video frame is preferable to growing latency

### Exit criterion

Remote client displays P2 video with bounded latency and no steadily growing queue.

---

## 11. Phase T8 — remote audio

### Goal

Add P2 audio after input+video latency is measurable.

Pipeline:

```text
P2 SameBoy samples
   -> small audio block
   -> low-latency encoder/packetizer
   -> network
   -> tiny client jitter buffer
   -> audio backend
```

Audio clock should become the long-term presentation clock only after we measure whether that improves stability. Do not let a large audio buffer silently add 100+ ms to perceived latency.

### Exit criterion

Video and audio remain acceptably synchronized without increasing controller latency.

---

## 12. Phase T9 — latency telemetry and control

Instrumentation is mandatory before optimization.

### Capture timestamps for

1. client controller event
2. input packet send
3. host packet receive
4. host input applied to P2
5. host P2 vblank/frame produced
6. encode begin/end
7. first/last video packet sent
8. client first/last packet received
9. decode begin/end
10. SDL texture upload
11. presentation request

### Display

Developer overlay:

```text
RTT            24.1 ms
Input uplink   12.8 ms
Encode          1.4 ms
Video downlink 12.5 ms
Decode          0.8 ms
Video queue     1 frame
Estimated E2E  43 ms
```

The exact end-to-end display latency cannot always be known without hardware measurement, so label estimated values correctly.

### Adaptive behavior

Remote Play should prefer:

- latest input state
- latest video frame
- bounded queues
- frame drop over queue growth

The application should never preserve every video frame at the cost of continuously rising latency.

---

## 13. Phase T10 — session and matchmaking layer

Only after direct-IP/LAN Remote Play works.

### Components

`SessionService`:

- create room
- join room
- protocol/version negotiation
- ROM hash/version metadata
- endpoint exchange
- authentication token/session token

`PeerTransport`:

- direct UDP when available
- NAT traversal
- relay fallback

The coordination server does not run SameBoy.

### Security boundary

Treat all client packets as untrusted:

- validate sizes and versions
- reject invalid session IDs
- rate limit malformed traffic
- never deserialize arbitrary SameBoy save-state data from a remote peer in Remote Play mode

---

## 14. Phase T11 — Native NetLink as optional second backend

Only after Remote Play is stable.

Reuse the frontend/session architecture but replace remote video/input semantics with a `NetworkLinkTransport` that exchanges serial/link timing information between a SameBoy core on each PC.

This is intentionally a separate experimental mode because the compatibility and synchronization problem is substantially harder.

```text
Remote Play:
Host Core A <local cable> Host Core B
                       ^
                       |
                 remote input

Native NetLink:
PC A Core <serial events over Internet> PC B Core
```

Do not let Native NetLink requirements complicate the first Remote Play release.

---

## 15. Proposed new source layout

```text
SDL/
  main.c

  session/
    game_session.c
    game_session.h
    emulator_slot.c
    emulator_slot.h
    local_link.c
    local_link.h
    multiplayer_input.c
    multiplayer_input.h

  remote_play/
    remote_play_host.c
    remote_play_host.h
    remote_play_client.c
    remote_play_client.h
    protocol.c
    protocol.h
    transport_udp.c
    transport_udp.h
    video_encoder.c
    video_encoder.h
    video_decoder.c
    video_decoder.h
    audio_stream.c
    audio_stream.h
    latency_stats.c
    latency_stats.h

  native_netlink/        # later
    netlink_transport.c
    netlink_transport.h
```

Keep these frontend modules independent from `Core/` wherever possible.

---

## 16. First implementation sequence

The first coding branch should contain only these steps:

### Commit A — EmulatorSlot scaffolding

- add `EmulatorSlot`
- make single-player SDL run through `slots[0]`
- no visible feature change

### Commit B — second core lifecycle

- initialize/deinitialize slot 1
- load same ROM into both cores
- allocate independent framebuffer/audio state
- still no link

### Commit C — LocalLink callbacks

- port libretro serial callbacks
- connect slot 0 and slot 1
- add transfer counters/logging

### Commit D — dual-core scheduler

- port delta-based `GB_run()` scheduler
- verify both cores reach vblank

### Commit E — P1/P2 input split

- separate local controller state
- apply P1 to slot 0 and P2 to slot 1

### Commit F — side-by-side rendering

- one SDL window
- two native Game Boy views

At that point we have the local-link foundation required by Remote Play.

The first Remote Play branch should then implement, in order:

1. UDP P2 input only
2. raw/native framebuffer streaming on LAN for latency measurement
3. bounded frame queue
4. production video encoder backend
5. P2 audio
6. session handshake
7. room-code service
8. P2P/NAT traversal
9. relay fallback

---

## 17. Technical risks

### Risk: SDL frontend singleton assumptions

This is currently the largest code-structure risk. Solve it incrementally; do not attempt a complete frontend rewrite in one commit.

### Risk: per-core timing sync

Do not allow each `GB_gameboy_t` to independently sleep against wall-clock time while being scheduled as a linked pair. The `GameSession` must own pacing for dual-core modes.

### Risk: audio buffering

The existing desktop audio backend is designed for smooth local playback, not minimum remote latency. Remote P2 audio must have its own measured buffer path.

### Risk: video encoder adding hidden buffering

Many general-purpose encoders buffer frames unless configured for low delay. The encoder interface and telemetry must expose queue depth and encode latency.

### Risk: network queue growth

Never use an unbounded reliable queue for realtime video or input. Stale frames and stale input are worse than dropped packets.

### Risk: upstream maintenance

Avoid modifying `Core/gb.h`, `Core/gb.c`, `Core/timing.c` or `Core/memory.c` unless a measured blocker requires it. Existing public serial APIs are already sufficient for the local-link design.

---

## 18. Definition of the first usable release

The first useful Windows release does not require Native NetLink.

It is complete when:

- host opens a ROM
- host selects Remote Link
- two linked SameBoy cores run locally on host
- host controls P1
- client controls P2
- client receives P2 video and audio
- connection uses a simple host/join flow
- queues remain bounded
- latency stats are visible in developer mode
- disconnect returns both programs to a safe menu state

Native NetLink remains an optional later feature.
