# SameBoy Link — Technical Overview

This document is the high-level technical introduction for contributors. For the detailed implementation sequence, see `SAMEBOY_LINK_TECHNICAL_PLAN.md`.

## Current snapshot — 2026-08-18

The fork now has a reproducible Windows build, two-core Local Link, isolated input/save/audio state, explicit four-mode session lifecycle and a protocol-v4 Remote Play implementation. The normal SDL menu supports Local Link, direct-IP Host/Join and clean Disconnect; P1/P2 have persistent independent keyboard/controller mappings with two-controller hotplug support. LAN and direct public-IPv4 play have been physically verified with lossless native-framebuffer video and adaptive 48 kHz stereo PCM. Remote Client has a dedicated network thread, latency telemetry, aspect-correct resizing, the full SameBoy OpenGL shader/filter pipeline and an Escape menu for local video/audio/P2-control settings. Video pacing/compression, authentication, encryption and production-grade Internet connectivity remain planned work.

## 1. Why SameBoy

SameBoy already provides highly accurate Game Boy / Game Boy Color emulation and exposes the serial primitives required for link-cable emulation. Its libretro frontend already demonstrates a working two-core local link implementation.

The fork should therefore avoid changing the emulator core unless absolutely necessary. Most work belongs in the SDL/Windows frontend plus new multiplayer/network modules.

## 2. Primary architecture

The project has two multiplayer models.

### Remote Play — primary Internet mode

Both linked Game Boys run on the host:

```text
HOST PC

+------------------+       local emulated       +------------------+
| SameBoy Core P1  |<------ link cable -------->| SameBoy Core P2  |
+------------------+                            +------------------+
        ^                                                   ^
        |                                                   |
    local input                                      remote P2 input
                                                            |
                                                            |
CLIENT PC <-------------------------------------------------+

CLIENT PC <---------------- P2 video/audio ----------------- HOST PC
```

Consequences:

- only the host requires the ROM;
- no ROM region/revision/hash matching is required for Remote Play;
- Game Boy serial timing remains entirely local to the host;
- the client needs no running emulator core for gameplay;
- the client can still reuse SameBoy rendering/scaling/filter code.

### Native NetLink — later optional mode

```text
PC A SameBoy Core <---- serial/link data over Internet ----> PC B SameBoy Core
```

This is harder because both emulators must remain synchronized across network latency and jitter. It is deliberately deferred until Remote Play is stable.

## 3. Existing SameBoy primitives we reuse

Relevant core APIs include:

```c
GB_set_serial_transfer_bit_start_callback()
GB_set_serial_transfer_bit_end_callback()
GB_serial_get_data_bit()
GB_serial_set_data_bit()
GB_disconnect_serial()
```

The existing `libretro/libretro.c` implementation is the reference for:

- two `GB_gameboy_t` instances;
- cross-connected serial callbacks;
- separate framebuffers;
- separate input/audio paths;
- cycle-delta scheduling between cores.

The initial local-link implementation should adapt that proven path rather than invent a new serial protocol.

## 4. SDL frontend refactor

Upstream SDL SameBoy is largely organized around one global emulator instance. This fork now implements:

```text
GameSession
   |
   +-- EmulatorSlot[0]
   |       +-- GB_gameboy_t
   |       +-- framebuffer
   |       +-- save/persistent data
   |       +-- input state
   |
   +-- EmulatorSlot[1]
           +-- GB_gameboy_t
           +-- framebuffer
           +-- save/persistent data
           +-- input state
```

Normal one-player SameBoy behavior was regression-tested before slot 1 and Local Link were activated.

## 5. Local link scheduling

Do not initially run the two Game Boy cores on unrelated OS threads. Use the same basic signed cycle-delta scheduler already proven in the libretro frontend:

```c
signed delta = 0;

while (!p1_vblank || !p2_vblank) {
    if (delta >= 0) {
        delta -= GB_run(&p1);
    }
    else {
        delta += GB_run(&p2);
    }
}
```

This keeps link timing deterministic and avoids injecting host thread-scheduling jitter into Game Boy serial timing.

Encoding/network work can use worker threads later.

## 6. Remote input

The client sends complete current P2 button state rather than individual key-up/key-down events.

Conceptual packet:

```c
struct RemoteInputPacket {
    uint32_t protocol_version;
    uint32_t session_id;
    uint32_t sequence;
    uint16_t buttons;
    uint64_t client_timestamp_us;
};
```

Advantages:

- newest packet supersedes old packets;
- packet loss does not leave a button permanently stuck;
- latency can be measured precisely;
- realtime input can use UDP.

## 7. Video path

The host streams Player 2's completed **native framebuffer**, not a screenshot of the Windows window.

```text
P2 SameBoy framebuffer
        |
        v
encoder / packetizer
        |
        v
network
        |
        v
client decoder
        |
        v
native framebuffer
        |
        v
SameBoy/SDL rendering path
        |
        +-- nearest neighbour
        +-- integer scaling
        +-- color correction
        +-- shaders/filters
```

For ordinary DMG/CGB gameplay the native image is typically 160x144. At 32 bits per pixel a raw frame is only 92,160 bytes, so we can prioritize latency and image integrity rather than extreme compression ratio.

Current and planned quality modes:

- **Lossless reference** — implemented using native-framebuffer pixel RLE;
- **Balanced** — planned default after video-pacing and codec measurements;
- **Low Bandwidth** — planned more aggressive compression.

GPU hardware encode/decode may be used where benchmarks show a real end-to-end benefit, but it is not assumed to be faster for such a small source.

## 8. Audio path

Keep audio identifiable per emulator core.

Host local audio can select/mix P1 and P2. Remote Play normally sends the P2 stream to the client using small blocks and a deliberately bounded jitter buffer.

Protocol v4 currently sends uncompressed 48 kHz stereo PCM with an adaptive jitter buffer. Physical LAN and Internet testing found this path more stable than the experimental Opus option, so Opus is deferred rather than used by default.

A large audio buffer must never silently become the dominant latency source.

## 9. Save architecture

Remote Play has two virtual cartridges on the host, so each `EmulatorSlot` requires independent persistent storage.

Never do this:

```text
P1 ----+
       +--> game.sav
P2 ----+
```

Instead:

```text
P1 --> profile/session P1 save
P2 --> profile/session P2 save
```

V1 keeps both saves on the host. Later, the remote player may upload a portable P2 save before a session and receive the updated save afterward.

Portable transfer must be transactional: backup, temporary file, cryptographic hash verification, atomic promotion and recovery after disconnect. The persistence API should allow `.sav`, RTC and auxiliary cartridge data.

## 10. Internet connectivity

The user-facing target is zero manual network configuration.

The current prototype has proven direct UDP play over the public Internet using manual public-IP entry and UDP port forwarding. That is a development test path only; it is not the intended user experience and does not yet provide session authentication or encryption.

Connection establishment may combine:

```text
coordination server
       |
       +-- direct IPv6
       +-- UDP hole punching
       +-- UPnP IGD
       +-- NAT-PMP
       +-- PCP
       +-- relay fallback
```

Direct P2P is preferred because video traffic is much larger than controller input. Relay is required for restrictive NAT/CGNAT cases.

The coordination server does **not** run SameBoy. It manages rooms, tokens, endpoint candidates and relay information.

## 11. Invite architecture

Users can join using either a short room code or an invite URL.

Custom application URL:

```text
sameboylink://join/<opaque-token>
```

Shareable web URL:

```text
https://<project-domain>/join/<opaque-token>
```

The token is opaque, random, short-lived and resolved by the coordination service. Raw IP addresses, ports, ROM paths and save paths should not be embedded in the invite.

Normal flow:

```text
Host Game
   |
Copy Invite Link
   |
friend clicks
   |
SameBoy Link opens
   |
coordination service resolves token
   |
connection candidates negotiated
   |
direct path or relay
   |
Connected
```

## 12. Latency strategy

The relevant measurement is controller-to-display latency, not just encoder execution time.

Instrument at least:

```text
client controller event
input packet sent
host packet received
input applied to P2
P2 frame completed
encode begin/end
video sent/received
decode begin/end
texture upload
presentation request
```

Design rules:

- newest input wins;
- queues stay bounded;
- obsolete video frames are dropped rather than queued;
- no B-frame/large lookahead pipeline in low-latency modes;
- optimize based on measured end-to-end behavior.

Initial processing targets on typical modern hardware are roughly under 1 ms each for emulation-side processing, encode and decode where practical, with the understanding that network propagation and frame/display timing will usually dominate.

## 13. Security boundaries

Before public Internet release:

- treat every packet and URL as untrusted;
- validate packet lengths, protocol versions and session IDs;
- use TLS for coordination APIs;
- authenticate and encrypt realtime P2P/relay sessions;
- never accept arbitrary peer-provided filesystem paths;
- never let an invite URL execute arbitrary commands;
- do not deserialize arbitrary remote SameBoy save-state blobs in Remote Play mode.

## 14. Current source layout

```text
SDL/
  session/
    emulator_slot.c/.h
    game_session.c/.h
    local_link.c/.h
    multiplayer_input.c/.h

  remote_play/
    remote_play_host.c/.h
    remote_play_client.c/.h
    protocol.c/.h
    transport_udp.c/.h
    video_codec.c/.h
  link_diagnostics.c/.h

  native_netlink/
    ... later experimental backend ...
```

## 15. Documentation map

- `README.md` — project introduction for visitors.
- `ROADMAP.md` — phase-by-phase development plan.
- `TODO.md` — actionable checklist.
- `LINK_UI_ARCHITECTURE.md` — target Link menu, session modes and control ownership.
- `TECHNICAL_OVERVIEW.md` — this contributor-oriented architecture summary.
- `SAMEBOY_LINK_TECHNICAL_PLAN.md` — detailed implementation notes and decisions.
- `SAMEBOY_LINK_PLAN.md` — original project/product planning notes.
