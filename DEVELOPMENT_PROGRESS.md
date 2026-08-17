# SameBoy Link — Development Progress

This document records completed implementation work and the evidence used to
advance between roadmap phases. The detailed backlog remains in `TODO.md`.

## 2026-08-17 — Phase 0 complete

The Windows SDL baseline is reproducible and ordinary single-player behavior has
been verified before beginning the multi-instance frontend refactor.

### Build environment

- Windows 64-bit SDL frontend;
- Visual Studio 2022 C++ Build Tools and Windows SDK;
- LLVM/Clang 22.1.8 with LLD;
- SDL2 2.32.10;
- RGBDS and GNU Make/MSYS;
- one-command debug build through `build-windows.ps1`.

A clean debug build produces `build\bin\SDL\sameboy.exe`. The build script also
supports clean builds, release builds, alternate targets, parallel jobs and a
custom SDL2 root. The Windows tester target was corrected so it links its tester
entry point and can be built through the same script.

### Runtime instrumentation

The SDL frontend now emits fork-specific diagnostic categories for frontend,
video, audio and timing. At the start of each ROM session it records:

- active renderer;
- native framebuffer dimensions, pitch and SDL pixel format;
- audio driver, channel/sample format and frontend/core sample rates;
- nominal frame rate;
- measured cadence over the first 120 normal frames.

On the reference development machine the two test sessions used a 160×144,
32-bit `SDL_PIXELFORMAT_ABGR8888` framebuffer and XAudio2 at 48 kHz. Measured
cadence was approximately 59.75 Hz against SameBoy's nominal 59.727501 Hz.

### Regression baseline

Two user-provided test ROMs were exercised, one DMG title and one CGB title. The
automated tester completed both runs and generated valid framebuffer captures.
Manual SDL testing confirmed:

- controls;
- rendering;
- audio;
- battery saves;
- save states.

The ROM files are external test inputs and are not part of this repository.

## 2026-08-17 — Phase 1 started

Phase 1 moves the SDL frontend's per-emulator state into `EmulatorSlot` objects
owned by a `GameSession`. Slot 1 remains inactive until the one-slot refactor has
passed the same build and regression baseline. No link emulation is enabled in
this phase.

### Implemented foundation

- added `SDL/session/emulator_slot.c/.h`;
- added `SDL/session/game_session.c/.h`;
- removed the standalone global `GB_gameboy_t` from the SDL frontend;
- moved each core's two native framebuffer buffers and active/previous pointers
  into its slot;
- moved ROM ownership, battery-save path ownership and battery dirty/timer state
  into its slot;
- routed SDL rendering, menus, debugger reload, save states and battery handling
  through slot 0;
- allocated capacity for two slots while keeping `active_slot_count == 1`;
- left `Core/` unchanged.

### Automated verification after the refactor

- clean Windows SDL debug build passed with warnings treated as errors;
- the Windows tester target built successfully;
- 15-second automated DMG and CGB runs completed and produced valid framebuffer
  captures;
- both ROMs ran in the real SDL frontend with `active_slots=1` reported by the
  diagnostic stream;
- OpenGL presentation, XAudio2 at 48 kHz and approximately 59.76 Hz cadence were
  observed;
- the isolated CGB test copy produced its battery save beside the copied ROM,
  leaving the original test directory untouched.

### Manual sign-off and phase completion

Manual SDL testing confirmed controls, audio, save-state save/load and battery
save persistence after the refactor. Phase 1 is therefore complete. Slot 1
remains inactive in the shipped execution path; activating and linking it belongs
to the Local Link MVP in phase 2.

## 2026-08-17 — Phase 2 runnable developer slice

The SDL frontend can now start two SameBoy cores in one process with the
temporary `--local-link` command-line option. This developer mode loads the same
ROM into both slots so the serial implementation, scheduler and frontend can be
validated before a finished Local Link menu is added.

### Implemented

- slot 1 owns its own core, framebuffer pair, ROM path, input state, vblank
  state, audio callback and battery-save path;
- `LocalLink` connects SameBoy's serial bit callbacks and infrared state between
  the two cores, following the existing libretro implementation;
- a cycle-delta scheduler advances whichever core is behind until both have
  produced a frame;
- both native framebuffers are composed side by side and presented by either the
  SDL renderer or the OpenGL path;
- Player 1 continues to use the configured SameBoy keyboard bindings, while
  Player 2 has an independent temporary keyboard map;
- Player 2 battery data uses `<rom name>.p2.sav`, so it cannot alias Player 1's
  `<rom name>.sav`;
- link diagnostics report synchronized frames, final cycle delta, serial bits
  transferred and Player 2 audio samples.

Player 2 audio uses a dedicated callback and sample counter. Local Link keeps it
separate from Player 1 output; Remote Play later routes it to the P2 client.

### Run the developer mode

From the repository root:

```powershell
.\build\bin\SDL\sameboy.exe --local-link "C:\path\to\game.gb"
```

Temporary keyboard controls:

| Player | Directions | A | B | Start | Select |
| --- | --- | --- | --- | --- | --- |
| P1 | Arrow keys | X | Z | Enter | Backspace |
| P2 | W/A/S/D | K | J | I | U |

Both screens must enter the game's two-player/link mode. Closing the window
flushes each active slot's battery save.

### Verification completed

- a clean Windows debug build passed with warnings treated as errors;
- ordinary one-core mode still started both the DMG and CGB test ROMs;
- two-core mode started both ROMs without the earlier missing-APU-callback
  assertion;
- both cores reached 120 synchronized frames with a bounded cycle delta;
- side-by-side output was produced by both OpenGL and `--nogl` presentation;
- Tetris DX created and flushed separate `.sav` and `.p2.sav` files.

Phase 2 is not complete yet. The next exit-criterion work is to enter an actual
two-player session in the DMG and CGB titles, confirm non-zero serial activity,
exercise both control maps, and then run an extended stability/save test.

### Manual DMG link sign-off

The user completed a real two-player session with Tetris (DMG) and confirmed
that link gameplay, both keyboard control maps and side-by-side presentation
worked without faults. The remaining compatibility gate is the equivalent CGB
test followed by an extended stability run.

### Manual CGB link sign-off

The user also completed a linked Tetris DX (CGB) match and confirmed correct
two-player behavior. The shutdown diagnostic recorded 30,112 serial bits from
Player 1 and 30,128 from Player 2. Both the 8 KiB `.sav` and `.p2.sav` files
were flushed at the same shutdown time, confirming separate persistence for the
two active cartridges. A screenshot also confirmed two distinct live playfields
in the 320×144 side-by-side presentation.

Both required link-game compatibility checks now pass. The remaining Phase 2
exit work is the extended drift/crash/save-integrity session.

### P2 frame-latching correction

The first extended run exposed a rare partial-frame tear on the right-hand
screen. Local Link had been composing from each core's active write buffer. The
vblank path now swaps each slot to its other buffer and presents only the
completed, immutable frame while the core renders the next frame. The fix builds
cleanly and passes the initial 120-frame scheduler smoke test; an extended manual
run remains required before Phase 2 sign-off.

### Phase 2 completion

The repeated manual session after frame latching showed no further partial-frame
tearing, and the two screens appeared more tightly synchronized. The application
closed normally, link diagnostics recorded serial activity, and both independent
8 KiB save files remained readable with distinct SHA-256 hashes.

Phase 2 is complete. The Windows SDL frontend now meets the Local Link MVP exit
criterion with working DMG and CGB multiplayer, synchronized dual-core execution,
independent controls, side-by-side presentation, separate persistence and basic
diagnostics. Phase 3 can begin with a LAN Remote Play reference path while both
emulator cores remain on the host.

## 2026-08-17 — Phase 3 remote-input prototype

The first LAN Remote Play slice keeps both linked emulator cores on the host and
moves only Player 2 input across UDP. It is intentionally a direct-IP developer
mode; discovery, authentication, encryption and Internet traversal come later.

### Implemented

- protocol version 2 (original input prototype used version 1) with a fixed,
  explicitly encoded 28-byte input packet;
- non-zero session ID, sequence number, complete eight-button P2 mask and client
  monotonic timestamp in every packet;
- portable nonblocking UDP transport with Winsock support on Windows;
- host-side validation of exact length, magic, version, type, reserved fields,
  session ID and button range;
- wrap-safe newest-sequence-wins handling for stale/reordered packets;
- a 500 ms disconnect timeout that releases every P2 button;
- a standalone remote P2 client window in the same executable, sending changes
  immediately and a full-state keepalive every 50 ms;
- an explicit `GameSessionFrame` view of each latched completed native frame,
  including dimensions, pitch and monotonically increasing frame sequence.

The scheduler integration also removed an unintended extra Player 1 instruction
after each dual-core frame. In Local Link mode, the cycle-delta scheduler now has
exclusive ownership of core advancement.

### Verification

- a clean Windows debug build passed with warnings treated as errors;
- a real loopback client connected to a loopback host and delivered keepalives;
- crafted packets proved that the host applied a valid button mask, ignored an
  older sequence, rejected a different session ID and released input on timeout;
- the two-core cadence remained approximately 59.77 Hz with a zero-cycle delta
  in the initial smoke run.

### Developer commands

Host, which automatically enables Local Link:

```powershell
.\build\bin\SDL\sameboy.exe --remote-input-host 45900 --remote-session 424242 "C:\path\to\game.gb"
```

Client on another LAN PC, using the host's LAN address:

```powershell
.\build\bin\SDL\sameboy.exe --remote-input-client 192.168.1.10:45900 --remote-session 424242
```

Use `127.0.0.1` instead of the LAN address to test both processes on one PC.
The client uses the same UDP session for P2 input and the native P2 video stream.

### Raw P2 video reference

The same UDP session now returns Player 2's completed native framebuffer to the
client. This is a deliberately uncompressed correctness/latency baseline before
the quality/codec layer:

- the host converts the latched P2 frame to canonical RGBA8 before packetizing;
- each video datagram has a validated 48-byte header and at most 1,024 payload
  bytes, keeping packets below a typical Ethernet MTU;
- a 160×144 frame is 92,160 bytes split across 90 datagrams;
- frame sequence, dimensions, chunk index/count, payload offset/size, session ID
  and host capture timestamp are carried and validated;
- the client keeps only one in-progress frame and one completed presentation
  frame; a newer frame replaces incomplete obsolete work;
- only complete frames reach the SDL texture, which is displayed at native
  aspect ratio with local scaling.

An automated loopback run reconstructed 480 consecutive 160×144 frames with
zero dropped or rejected frames. Host cadence remained approximately 59.74 Hz
and the two-core scheduler reported a zero-cycle delta. The raw stream is roughly
44 Mbit/s before UDP/IP overhead and is a measurement reference, not the final
Balanced transport.

Manual testing with Tetris DX verified that the remote window displays the P2
framebuffer correctly and that remote P2 controls work during linked play. In a
roughly 143-second run, the host sent 8,582 frames (772,380 video chunks) and the
client completed all 8,582 frames with zero dropped or rejected frames. Observed
input packet age was normally 0–18 ms.

The client receive loop is bounded to 256 datagrams per iteration so a continuous
video stream cannot starve input keepalives. A follow-up loopback run completed
more than 600 frames with zero dropped/rejected frames, no input timeout, an
approximately 59.82 Hz host cadence and a zero-cycle scheduler delta.

### Raw P2 audio reference

The Remote Play session now also transports Player 2 audio as uncompressed
48 kHz stereo signed 16-bit little-endian PCM. Each packet contains 240 sample
frames (5 ms at 48 kHz), a 32-byte validated header, sequence number, session ID,
sample rate and host timestamp. Its payload bitrate is approximately 1.54 Mbit/s
before UDP/IP overhead.

The host collects complete audio blocks in a bounded eight-packet queue and drops
old work rather than allowing latency to grow. The client starts playback after
a 20 ms prebuffer, enforces an 80 ms absolute queue ceiling and re-prebuffers
after an underflow. Sequence diagnostics count lost, stale and reordered audio
packets independently of video.

An automated loopback run received 3,000 audio packets with zero loss, stale
packets or queue resets; the observed playback queue was normally 15–25 ms. A
subsequent manual Tetris DX session completed 3,976 remote video frames and
13,309 audio packets with zero video/audio packet loss or stale audio. The steady
audio queue was approximately 30–50 ms, and listening confirmed clean P2 sound.

### Physical LAN baseline and Balanced transport

The first two-PC test used an Ethernet host and a Wi-Fi P2 client. The raw
reference remained playable for roughly 131 seconds and the host reported 7,856
video frames, 26,299 audio packets and no local send failures. The player saw one
approximately 200 ms video/network glitch and possibly one short audio crackle.
This confirmed the LAN architecture while also showing that bursts of roughly 90
UDP packets per raw frame (about 44 Mbit/s video) were unnecessarily hostile to
Wi-Fi.

Protocol version 2 adds the first Balanced video path:

- a fast lossless RGBA8 pixel-RLE codec with strict bounded decoding;
- per-frame format tagging and automatic raw fallback when compression would be
  larger than the original frame;
- a dedicated stream sequence so intentional gaps while disconnected are not
  counted as packet loss;
- codec round-trip checks over flat, patterned and incompressible maximum-size
  frames, including rejection of truncated input;
- event diagnostics for missing/incomplete video frames, audio sequence gaps,
  queue resets and underflows.

Tetris DX used 22.2% of the raw framebuffer bytes in the final Wi-Fi run and
averaged about 20.5 video datagrams per frame instead of 90. The client completed
6,101 consecutive frames with zero dropped/rejected frames and received 20,422
audio packets with zero packet loss or stale packets. Audio initialization was
moved before network registration, with a bounded 40 ms prebuffer and 120 ms
ceiling. Three underflows were recorded (startup, one mid-session and shutdown),
down from nine in the preceding run; the user described final image, controls and
sound as essentially flawless. A measured 234 ms maximum input-arrival gap shows
that the Wi-Fi link still produced real jitter without destabilizing gameplay.

### Protocol v3 full-pipeline latency telemetry

Protocol version 3 adds NTP-style ping/pong clock synchronization and carries
the timestamps needed to follow a changed P2 input through the complete Remote
Play pipeline. The measured stages are client event/send, host receive/apply,
P2 frame completion, encode begin/end, host send, client receive span, decode,
texture upload and the SDL presentation call. Total input-event-to-presentation
time stays entirely on the client clock.

The client calculates smoothed RTT, network jitter and host-minus-client clock
offset. Cross-machine input/video network splits also report a conservative
clock uncertainty of half the lowest observed RTT; this makes sub-millisecond
negative values caused by synchronization error explicit instead of hiding
them. Delayed, duplicated or malformed clock replies are rejected. The live
window title shows RTT, uncertainty, latest total input latency, jitter, dropped
video frames and audio queue depth, while the stop summary records averages and
maxima.

A local loopback smoke run completed 659 video frames and 2,204 audio packets
with zero video drops/rejections, audio loss, stale packets, queue resets or
underflows. The lowest RTT was 2.04 ms, giving a stated cross-clock uncertainty
of approximately ±1.02 ms.

The subsequent 125-second physical Ethernet-host/Wi-Fi-client run completed
7,405 video frames and 24,784 PCM packets with zero video drops/rejections and
zero audio packet loss/stale packets. Across 333 changed inputs, total
input-event-to-presentation latency was 48.0 ms median, 50.0 ms average, 64.4 ms
p95 and 65.6 ms maximum. Measured RTT was 17.6 ms median and 21.0 ms p95. The
client intentionally superseded 125 already completed frames rather than
presenting obsolete work. One audio underflow occurred near startup and one
underflow plus the old queue reset occurred at shutdown. This run completes the
Phase 3 LAN and latency exit criteria.

### Protocol v4 adaptive audio and Opus Low Delay

Protocol version 4 makes the audio codec explicit per packet. The host accepts
`--remote-audio pcm` (the default reference) or `--remote-audio opus`; the client
selects its decoder from the stream. Both use 48 kHz stereo and 240-frame/5 ms
packets so codec comparisons do not hide extra packetization delay.

The fixed SDL queued-audio path was replaced by a callback-driven adaptive ring
buffer. It starts at 40 ms, can protectively grow to 100 ms after network gaps,
returns gradually toward 40 ms, re-buffers without clearing the entire queue and
uses bounded ±2,000 ppm linear resampling to correct host/client audio-clock
drift. Queue depth, target, underflows, trims, rate correction and arrival gaps
are reported. Opus uses `OPUS_APPLICATION_RESTRICTED_LOWDELAY`, 96 kbit/s target
VBR, 5 ms frames, complexity 5, stereo music tuning and decoder packet-loss
concealment.

In a 20-second Opus loopback run, all 3,951 packets decoded with zero loss,
decode errors, underflows or trims. The buffer ended at 42 ms with a 54 ms
maximum. Encoder lookahead was 120 frames (2.5 ms); average encode and decode
costs were approximately 29–32 µs and 14 µs per packet. The observed payload
averaged 36.9 bytes (about 59 kbit/s for that game-audio segment), compared with
PCM's fixed 960 bytes/1,536 kbit/s. A separate protocol-v4 PCM loopback also
completed without loss, decode errors, underflows or trims.

Physical Wi-Fi comparison exposed repeated 168-214 ms Opus arrival gaps,
96 missing packets, 13 underflows and aggressive buffer recovery. Opus itself
reported zero decode errors, but the resulting play session was substantially
less stable. The follow-up PCM run had zero audio/video packet loss and felt much
better, so PCM remains the stable baseline and Opus is deferred until transport
recovery and bandwidth pacing are mature.

### Remote presentation controls

The protocol-v4 PCM path is the current stable LAN baseline. In the final
physical keyboard test, the client completed 4,549 video frames and received
15,223 audio packets with zero packet loss, rejected frames, audio trims or
decode errors. The only audio underflow occurred 1.44 seconds after startup.
Average input-event-to-present latency was 52.3 ms and final RTT was 17.3 ms.

The remote P2 window is now resizable. Rendering preserves the native aspect
ratio and centers the image with letterboxing or pillarboxing instead of
stretching it. A resolution change no longer overwrites a size the user selected.

The host presentation is independent of the two active emulation slots.
`--remote-host-view p1` starts with only P1 visible, while
`--remote-host-view both` keeps the original side-by-side layout. `F9` switches
the view during play. P2 emulation, input, audio, saves and Remote Play streaming
continue in either view. A local automated test switched 160x144 -> 320x144 ->
160x144 while the client stream stayed active with zero audio/video drops.

The subsequent physical presentation test was accepted by the user. The host
switched between P1-only and side-by-side four times while sending 4,109 frames
and 13,750 PCM packets with zero host-side drops. Free P2 window resizing,
aspect preservation, remote input and continued streaming all worked correctly.

### First manual public-IPv4 Remote Play proof

The unchanged protocol-v4 PCM build was tested across two genuinely separate
Internet connections using temporary manual UDP forwarding. The host accepted
6,388 input packets, answered 623 clock-sync pings and sent 18,590 P2 video
frames plus 62,188 PCM packets over roughly five minutes. It reported zero
host-side video, audio or socket-send drops. The other player described the
session as working very well.

Across 571 logged changed-input events, host receive-gap median was 33.0 ms,
p95 was 50.7 ms and none exceeded 75 ms. The copied client diagnostic belonged
to a short retry after the host had already stopped, so client-side RTT,
input-to-present latency and receive-drop statistics remain unmeasured for this
first Internet proof. The result validates public reachability and playability,
not production readiness: authentication, encryption, automatic NAT traversal,
relay fallback and adaptive Internet bandwidth are still required.
