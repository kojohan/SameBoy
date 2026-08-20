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

At this checkpoint, the SDL frontend could start two SameBoy cores in one
process with the temporary `--local-link` command-line option. That developer
mode loaded the same ROM into both slots so the serial implementation, scheduler
and frontend could be validated before the finished Local Link menu was added.

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

## 2026-08-18 — GUI/session architecture adopted

`LINK_UI_ARCHITECTURE.md` is now the design contract for converting the proven
CLI backends into the normal SameBoy interaction model. The immediate milestone
is explicit `GameSession` modes and lifecycle, followed by a dedicated `Link`
menu, independent persistent P1/P2 mappings and clean Disconnect behavior. The
same executable remains responsible for single-player, Local Link, Remote Host
and Remote Client roles, and the existing CLI entry points remain regression
paths while the menu flows are introduced.

At that checkpoint, this reprioritized essential UI/session integration ahead
of client shader/filter reuse, video pacing, authentication, encryption, NAT
traversal and relay support.

### First Link-menu slice

The SDL frontend now has explicit single-player, Local Link, Remote Host and
Remote Client session modes. Existing CLI entry points select those modes
through `GameSession`, and Local Link can also be started at runtime through
`Link > Local Link…`. `Link > Disconnect` tears down the local serial bridge,
saves both active batteries, releases slot 1 and returns to single-player.

The first physical menu test exposed a presentation-size bug: opening the menu
during a 320x144 side-by-side session still allocated a 160x144 GUI buffer. The
renderer then read beyond that allocation. The GUI and mouse-coordinate path
now use the complete current presentation size. Local Link, opening the menu
and Disconnect were physically retested successfully.

Keyboard P2 is now a separate persistent profile under Control Options and is
used by both Local Link and Remote Client. Controller configuration now has
separate P1/P2 layouts and device assignments; Local Link routes the second
assigned controller to slot 1, while Remote Client interprets connected
controllers using the configured P2 layout. Physical controller and preference
persistence verification is complete: keyboard remapping, assigning P2 to a
controller, two simultaneously connected controllers and unplug/replug hotplug
recovery were all accepted in the physical test.

### Direct-IP Host/Join GUI slice

The Link menu now exposes Local Link, Remote Host, Remote Client, Disconnect and
Remote Link Settings actions. Selecting Join prompts directly for `IP:port`,
prefilled with the last successfully submitted address. The settings menu also
persists that address, the host UDP port, the shared numeric session ID and the
host's P1-only/both-screens view. Remote Host starts the already tested PCM
backend inside the current ROM session. Remote Client relaunches the same
executable in its established client role, avoiding a second network
implementation.

Remote Client now participates in the frontend menu lifecycle. Escape opens a
client-specific SameBoy menu instead of terminating the process. The client can
resume, disconnect, quit, change aspect/integer/stretch scaling, select the full
SameBoy shader/filter set, resize or toggle fullscreen, change volume/mute, remap
the P2 keyboard and select/configure the P2 controller. These settings persist
on the client machine. Disconnect tears down its UDP/audio/video resources and
returns to the ordinary SameBoy start window without launching another process.

The debug build succeeds with Opus disabled. Automated protocol-v4 loopback
regressions completed with zero video drops, rejected frames, audio drops,
underflows, trims or decode errors. The client-menu regression verified that
Escape leaves the stream process alive and that Disconnect stops the network
thread and returns that process to the `SameBoy v1.0.3` start window. At this
checkpoint, physical verification of the new menu-driven Host/Join path remained
before the four-mode GUI/session milestone could be signed off.

### One-PC four-mode regression

The completed menu/session implementation was regression-tested on a fresh
Windows development setup. The headless tester ran both the `RIBBIT` CGB demo
and the purpose-built `COOP LINK` CGB test ROM for 15 emulated seconds and
produced valid 160x144 framebuffer captures. The real SDL frontend then passed
ordinary single-player and Local Link startup with OpenGL presentation, XAudio2
at 48 kHz and measured cadence near the nominal 59.727501 Hz.

The menu-driven Local Link flow displayed both cores, accepted the independent
P1/P2 keyboard mappings, opened the menu at the full 320x144 presentation size
and returned to single-player through `Link > Disconnect`. Its shutdown log
recorded 56 P1 and 8,680 P2 serial bits and an explicit
`local_link -> single_player` transition.

The menu-driven Remote Host/Join flow was also exercised through a one-PC UDP
loopback session. The host accepted 749 input packets with no rejected or stale
packets, sent 1,769 lossless video frames with no host-side drops, sent 5,917
PCM audio packets with no host-side drops and answered all 74 clock-sync pings.
The user confirmed working P2 video, audio, input, the Remote Client Escape menu
and clean Disconnect behavior. The host recorded 12,296 P1 and 56 P2 serial
bits during teardown.

This completed the local regression coverage but did not close the milestone at
that checkpoint: the same menu-driven Host/Join flow still needed to be repeated
on two physical PCs.

### Full Remote Client shader/filter reuse

Decoded native P2 frames now use the same OpenGL shader/filter implementation
as ordinary SameBoy. Upload and presentation are separate operations so network
decode/upload and buffer-swap latency remain independently measurable. The
client Video Options menu cycles the complete configured SameBoy filter list;
changing filters recreates the shader and reuploads the latest completed frame.
If OpenGL 3.2 or shader initialization is unavailable, the client recreates its
window with the established nearest/bilinear SDL renderer. `--nogl` explicitly
selects that fallback for compatibility testing.

A clean Windows debug build passed with `-Werror`. The user manually verified
the OpenGL loopback image, audio, LCD/CRT/Nearest filter switching, resizing,
P2 input and clean Disconnect. Across 8,361 decoded frames the client reported
zero video drops or rejections; the host sent 8,391 frames and 28,086 PCM audio
packets with zero send drops. Average measured input-to-present latency was
30.309 ms and maximum latency was 48.620 ms. Four audio underflows occurred
during the long menu/filter test alongside a 534.960 ms maximum arrival gap;
there were no audio drops, stale packets, trims or decode errors.

The explicit `--nogl` loopback selected `remote_client_presentation=SDL` and
received at least 480 video frames without drops or rejections plus more than
1,800 PCM packets without drops, stale packets or underflows. Post-change smoke
tests also kept ordinary single-player and Local Link running without logged
errors. At that checkpoint, the remaining four-mode milestone item was the
two-physical-PC Host/Join test.

The temporary row of eight P2 button-state indicators has been removed from
both the OpenGL and SDL Remote Client presentation paths. Input collection,
network transmission and latency telemetry remain active, but the gameplay
window now contains only the streamed framebuffer and its configured
letterbox/pillarbox background.

### Automated Windows smoke regression

`test-windows-link.ps1` now turns the established one-PC developer paths into a
repeatable regression gate. Given an external link-capable ROM, it optionally
builds SameBoy and runs ordinary single-player, Local Link, Remote Play with the
OpenGL client and Remote Play with the explicit `--nogl` SDL fallback. It polls
the structured SameBoy Link logs rather than relying on fixed startup delays,
checks frame/audio progress and zero-drop/error invariants, and closes only the
exact PIDs it launched. A private test copy prevents the source ROM's battery
save from being modified.

The first complete harness run passed all four cases. Each Remote Play client
reached at least 120 decoded frames and 600 PCM packets with zero video drops,
rejected frames, audio drops, stale packets or decode errors; both expected
presentation backends and the host input/video/audio activation paths were
confirmed. This automated the repeatable local coverage while leaving real
two-machine network, controller and display behavior to the then-pending
physical regression.

### Versioned network test builds

`publish-windows-build.ps1` now publishes complete Windows SDL runtimes to a
new versioned directory on a network disk, deliberately excludes `prefs.bin`,
records the source commit/dirty state and verifies a SHA-256 manifest after the
copy. This lets both physical test machines select one immutable build while
keeping their P1/P2 preferences local. An included launcher can verify and
cache the build locally on SMB servers that deny direct executable access.

OpenMediaVault initially created the copied files without execute permission,
so Windows correctly rejected direct startup from the `sdc` share. After the
share was configured to preserve execute bits, build
`20260818-200055-1ac9ddab8a17-dirty` gained `ReadAndExecute` and started directly
from the organized `M:\SAME-LINKTEST` root. A ROM smoke start loaded the OpenGL renderer, native 160x144
framebuffer and XAudio2 48 kHz audio with no fatal or GLSL errors, and no shared
`prefs.bin` was created.

### Physical two-PC four-mode regression

The final menu-driven Remote Host/Join regression passed on two physical
Windows PCs using the same checksummed network build. The host ran the ROM
locally and listened on UDP port 45990; the client launched the shared
`sameboy.exe` from the NAS and joined over the 192.168.0.0/24 LAN. Windows had
classified the host Wi-Fi as Public, so a narrow inbound rule was required for
UDP 45990 from the local subnet.

The first client attempt exposed a configuration mismatch: the client used a
different session ID and remained on its empty pre-frame background. Matching
the host's active session ID (`1`) connected immediately. The transport
correctly rejected the mismatched session, but the lack of waiting/session-
mismatch feedback is retained as a connection-status UI task.

The user verified P2 video, audio and controls, free resizing, LCD/CRT/Nearest
filter switching through the Remote Client Escape menu, clean client Disconnect
back to the start window and clean host Disconnect. This closes the physical
two-PC item and the complete single-player/Local Link/Remote Host/Remote Client
UI/session regression milestone.

### Instrumented physical network comparison

The double-click diagnostic launcher now stores build/machine metadata plus
separate stdout/stderr streams and a combined `sameboy-session.log`. Join
relaunches Remote Client as a child process on Windows, so the first launcher
captured only the parent. The client now receives explicit diagnostic file paths
through its inherited environment; a deliberately invalid endpoint self-test
confirmed that child-process diagnostics reach `REMOTE CLIENT STDERR` in the
combined log.

Three physical measurements now guide optimization. A direct public-IPv4 run
streamed for roughly 484.5 seconds; the host accepted 11,422 input packets,
sent 28,945 video frames and 96,884 PCM packets and answered 1,091/1,091 clock
pings with zero host-side drops, rejected input or stale input. The earlier
logger flaw means that run has no detailed client receive summary.

On LAN, the Wi-Fi laptop Host/Ethernet desktop Client combination ran for about
51.0 active seconds. The client completed 3,046 frames with zero network frame
drops/rejections but superseded 99 complete frames (3.25%) before presentation.
It received PCM without sequence loss or decode errors, yet recorded 33
underflows, 13 trims and a 100.425 ms maximum packet-arrival gap. Smoothed RTT
ended at 13.545 ms; average/maximum input-to-present latency was
41.663/74.622 ms.

Reversing the roles to Ethernet desktop Host/Wi-Fi laptop Client ran for about
98.3 active seconds. Audio underflows fell to two, while average latency
improved to 36.527 ms. One isolated disturbance around client time 28.5 seconds
lost 12 audio packets and eight video frames, raised video network time to
142.464 ms and produced the 167.422 ms maximum input latency. The stream then
recovered and remained stable. Video superseded-before-present remained nearly
identical at 192/5,873 frames (3.27%), pointing to sender/presentation pacing
rather than network direction alone.

The first paired-log summarizer is now implemented and validated against that
Ethernet Host/Wi-Fi Client capture. It reproduced the 98.350-second active
duration, 12 audio and eight video losses, two underflows, 192 superseded
frames and 36.527/167.422 ms average/maximum latency. From 164 detailed latency
samples it additionally calculated p95 49.530 ms and p99 112.700 ms, and placed
all media-loss events in the 20-30 second window. JSON and Markdown output are
schema-versioned and include build-hash comparison plus shutdown-tail handling.

The Windows diagnostic launcher automatically exports to the shared
`SAME-LINKTEST\LOGS` root when launched from a published NAS build. Every start
uses a collision-resistant timestamp/computer/launch ID and is named after exit
with the detected `host`, `client`, `local` or `idle` role. Active stdout/stderr
now remains below the local `%LOCALAPPDATA%` staging root so synchronous SMB
writes cannot perturb realtime traffic. After exit the complete directory is
copied to a hidden NAS staging name, SHA-256 verified file by file and renamed
to its final shared name; a failed export retains the local capture. Launcher
metadata also records the primary adapter, Wi-Fi/Ethernet medium, link speed,
IPv4 address and optional test label. Isolated fixture runs verified Host,
re-launched Client and idle classification plus multiple unique launches in one
shared directory, removing the previous manual log-copy step.

The first UNC client launch exposed two launcher defects: PowerShell's internal
provider-qualified UNC path was passed to the native client's diagnostic
redirect, and the menu process could finish before its relaunched Remote Client
child. The launcher now uses the native UNC provider path, detects the child PID
and waits for it before combining or renaming logs. A process-chain fixture
verified that the child remained logged through exit and was classified as
`client`.

These comparisons also swapped physical machine roles, so Ethernet/Wi-Fi and
CPU effects are not fully isolated. The complete evidence, test limitations,
repeat matrix and P0–P5 optimization gates are maintained in
`REMOTE_PLAY_PERFORMANCE_PLAN.md`. That initial evidence led to the protocol-v5
datagram reduction and zero-queue presentation work recorded below. With the
new Wi-Fi baseline accepted, the immediate engineering order is audio arrival
telemetry, correct active-adapter metadata and adaptive-PCM tuning; deeper
transport pacing is deferred until measurements show a need.

### Zero-queue adaptive presentation experiment

The 60 FPS platform side-scroll test exposed small client hitches even though
the Wi-Fi/Wi-Fi capture completed 7,949 frames with zero network drops or
rejections. The client superseded 324 complete frames before presentation
(4.08%), so the visible issue is presentation cadence rather than missing UDP
frames.

Remote Client now keeps the existing one-frame latest-complete mailbox and does
not add buffering on either machine. Its OpenGL path requests adaptive VSync;
late frames may present immediately instead of waiting for another refresh. If
the driver does not support adaptive VSync, the client falls back to VSync off
rather than adding a blocking standard-VSync delay. Startup diagnostics record
the effective VSync mode and swap interval.

Shutdown diagnostics now distinguish completed and actually presented frames
and report average/maximum presentation-call duration plus the number of calls
lasting at least 18 ms. The paired-log summarizer includes these values in
schema version 2 while remaining compatible with older logs. A warnings-as-
errors Windows build and all four automated smoke modes passed. At this
checkpoint, the physical 60 Hz side-scroll A/B test still remained before an
acceptance decision.

### Protocol v5 reduced video datagram burst experiment

The first physical zero-queue presentation retest felt unchanged. Adaptive
VSync was unavailable on the client and correctly selected `off-fallback`.
Presentation itself was not blocking: 6,796 of 6,864 completed frames were
presented, calls averaged 0.092 ms, the maximum was 0.432 ms and no call crossed
18 ms. Superseded frames improved from 4.08% to 0.99%, but the unchanged feel
shows that counter alone does not explain the side-scroll hitch.

The same Wi-Fi/Wi-Fi run exposed stronger transport evidence. The host sent
135,127 video chunks for 6,896 frames, averaging 19.60 back-to-back datagrams
per frame. The client had a 77.086 ms maximum audio arrival gap and four
underflows, including three in the 70–80 second window, despite zero missing
audio or video packets. The launcher selected a disconnected Ethernet adapter
for host metadata (`0 bps`); the physical host connection was Wi-Fi, so adapter
selection remains a separate metadata fix.

Protocol v5 raises video payload from 1,024 to 1,280 bytes. The resulting
1,388-byte UDP payload including the SameBoy Link header remains below a
1,500-byte link MTU after IPv4 or IPv6 overhead. A typical RLE frame should need
about 16 rather than 20 datagrams, reducing packet count by roughly 20% without
adding a host/client frame queue or spreading transmission over a frame period.
Host logs now report average/maximum burst duration, bursts lasting at least
5 ms and maximum chunks per frame. Client logs report average/maximum complete-
frame receive span and spans lasting at least 5 ms. At this checkpoint, the next
identical physical side-scroll run was designated as the acceptance test.

Three physical Wi-Fi/Wi-Fi protocol-v5 runs established the reduced-datagram
path as the current baseline. Normal RLE traffic remained near 15.7–16.1
datagrams per frame instead of protocol v4's 19.6. The three superseded rates
were 0.84%, 0.71% and 0.92%; average input-to-present latency was 37.855,
34.487 and 36.670 ms. The user reported that Wi-Fi play now feels good enough
to retain this implementation while deeper transport work is deferred.

The final repetition captured a rare horizontal seam in the host's P1-only
window while the Remote Client remained intact. This was not a UDP fault: both
streams had zero video drops, and the host's maximum measured send burst was
0.943 ms. P1-only link presentation returned slot 0's active pixel buffer,
which the core may already be drawing for the next frame; side-by-side and the
remote P2 stream instead used latched complete buffers. Local Link and Remote
Host P1-only presentation now select slot 0's immutable previous buffer after
the first completed frame. This changes only the local host display and adds no
queue or network/input latency.

After the P1 presentation correction, the warnings-as-errors Windows build and
all four automated smoke modes (single-player, Local Link, Remote OpenGL and
Remote SDL) passed. The corrected build was also published to the shared
`SAME-LINKTEST` build directory for physical verification.

### Bounded audio-arrival telemetry and active adapter metadata

Remote Client now measures every post-start PCM packet arrival gap in fixed
1 ms histograms. The cumulative histogram and the current ten-second histogram
use bounded in-memory arrays; the client writes only its existing periodic
checkpoint, one compact record per completed time window and the final session
summary. This provides average, p50, p95, p99 and exact maximum arrival gaps
without restoring per-packet or NAS writes that could disturb realtime play.

The paired-log summarizer advances to schema version 3 and includes both the
session distribution and individual ten-second audio windows in JSON and
Markdown. It remains compatible with older logs that contain only the maximum
arrival gap. The Windows launcher now evaluates active default routes in metric
order and rejects adapters that are disconnected, report zero link speed or
lack a usable IPv4 address. An isolated check on the Wi-Fi test PC correctly
selected its active 1.2 Gbit/s Wi-Fi interface instead of the disconnected
`0 bps` Ethernet route.

The warnings-as-errors Windows build and all four automated smoke modes passed.
The schema-v3 summarizer reproduced the new automated arrival distribution and
also processed the latest protocol-v5 physical log as a legacy-compatible
input. The verified dirty development runtime was published as the versioned
NAS build `audio-telemetry`. Physical verification of the new metadata/windows
was designated as the next test before any adaptive-PCM parameter change.

The subsequent Wi-Fi/Wi-Fi physical run covered approximately 111.3 active
seconds. Both machines recorded the correct active 1.2 Gbit/s Wi-Fi adapter and
the same executable hash. Audio arrival averaged 5.170 ms with p95/p99 of
18/20 ms; normal ten-second windows peaked between 30.164 and 34.256 ms with no
lost, stale or undecodable audio packets and no trims. One steady-state
underflow at 90.8 seconds was inaudible and did not recur after the target rose
from 45 to 55 ms. A second counter followed a 3.795-second gap immediately
before network-thread shutdown and is classified as tail behavior.

The same run had zero video drops, 0.77% completed frames superseded before
presentation, 37.701/52.662/54.106 ms average/p95/maximum input-to-present
latency and a 0.729 ms maximum host video burst. The user heard no audio fault.
Because increasing the minimum PCM target would add latency without an observed
benefit, the existing PCM parameters remain unchanged and deeper pacing work is
again deferred until a repeatable audible or visible failure appears.

### Schema-v4 timing windows

Clock-sync checkpoints and detailed input-to-present records now include client
elapsed milliseconds. The paired-log summarizer groups smoothed RTT/jitter and
input-to-present/video-network samples into comparable ten-second windows while
retaining whole-session percentiles. Reports advance to schema version 4 and
continue to process schema-v3 and older logs without timestamped timing windows.

No sender queue was introduced: queue-age telemetry is explicitly deferred
until such a queue exists, avoiding instrumentation for nonexistent state. The
automated Remote Play regression now requires timestamped clock telemetry; the
next physical five-minute run was designated to supply input-change samples for
the first full schema-v4 windowed report.

The warnings-as-errors Windows build and all four automated smoke modes passed.
The automated OpenGL run produced a schema-v4 clock window, a synthetic
three-sample fixture verified latency grouping across the 0-10 and 10-20 second
boundaries, and the latest physical schema-v3 log remained compatible with
empty timing-window arrays. The verified dirty development runtime was
published to the NAS as `timing-windows-v4` for the five-minute physical run.

The physical schema-v4 Wi-Fi/Wi-Fi run then exceeded the requested duration at
approximately 453.6 active seconds. It produced 46 clock windows and seven
input-active latency windows. RTT averaged 15.340 ms with 18.490 ms p95;
input-to-present latency averaged 37.890 ms with 51.476 ms p95 and 61.886 ms
maximum. Video-network time averaged 4.578 ms, zero frames were lost and 0.61%
of completed frames were superseded before presentation. Presentation calls
remained below 0.973 ms.

Audio had zero missing/stale packets, decode errors or trims. Three technical
underflows occurred as the adaptive target decayed and recovered, but the user
judged audio acceptable for now and did not listen continuously throughout the
run. The result therefore closes physical schema-v4 verification without
claiming that every callback was perceptually inspected. No PCM, video or queue
behavior is changed; future tuning requires a repeatable audible or visible
fault.

### Protocol v6 connection handshake and pre-frame status

The earlier two-PC test had shown that a wrong session ID produced only the
Remote Client's empty background because the host silently discarded every
packet. Protocol v6 adds fixed-size bootstrap `Hello` and response packets. The
host can now answer `Accepted`, `Session mismatch` or `Protocol mismatch` even
before ordinary version/session-gated media decoding. Input and clock packets
are accepted only after a successful handshake and only from the accepted UDP
endpoint. Mismatch replies use an endpoint-specific send and therefore do not
replace an already selected media peer.

Remote Client now renders `Connecting`, `Waiting for host`, `Session mismatch`,
`Protocol mismatch` or `Connected` before the first media frame and mirrors the
state in its window title and bounded transition logs. A lightweight 500 ms
handshake repeat also supplies connection liveness; two seconds without a host
response returns a connected client to `Waiting`. The media formats, 1,280-byte
video payload, no-frame-queue policy and adaptive PCM behavior are unchanged
from the physically accepted protocol-v5 baseline. The bootstrap request ID
and host ID correlate replies but do not authenticate or encrypt the session.

The warnings-as-errors Windows build passed. The one-PC suite now covers five
paths: single-player, Local Link, OpenGL Remote Play, SDL Remote Play and
handshake failures. It verified clean `Connecting` to `Connected` and
`Connecting` to `Waiting` transitions, a real wrong-session client that never
became active on the host, a fake v5 endpoint that made the v6 client display
`Protocol mismatch`, and a raw incompatible-version probe that received the v6
host's protocol-mismatch response. Logs are in
`build/regression/automated-20260819-201532-19056`. The verified dirty runtime
was published as `M:\SAME-LINKTEST\builds\handshake-v6`. Physical two-PC v6
verification remains the next gate before calling the new wire version
physically accepted.

The subsequent UI pass adds transient in-game peer notices. On a new or
re-established connection, the host consumes a one-shot event and shows
`Player 2 connected` on P1 for roughly two seconds. The client independently
draws `Player 1 connected` into its copied presentation frame for the same
period after the handshake reaches `Connected`. Remote Host OSD rendering is
now primary-slot-only; this prevents P1's local notice from being embedded in
the P2 framebuffer transmitted to the client. No protocol packet, frame queue
or media timing changed.

The warnings-as-errors build and all five automated paths passed again. Both
OpenGL and SDL loopbacks emitted the P1/P2 notice markers. Logs are in
`build/regression/automated-20260819-203002-14968`; the verified runtime was
published as `M:\SAME-LINKTEST\builds\handshake-v6-player-notices`.

The first physical notice check did not show either gameplay message, although
the full-window disconnect/waiting state worked. The host log
`20260819-203148-134-LAPTOP-IUM1NOQG-host-e4aae1d0` proved that the correct
notice build ran and that the host generated the event twice, including after
an early input timeout/reconnect. The renderer still gated the text behind the
ordinary `configuration.osd` preference, so the marker could be logged without
being drawn. No new paired client log from that attempt reached the shared log
root, which also left the client executable version unverified.

Connection notices are now dedicated UI and ignore the optional general OSD
preference on both sides. P1 uses its own primary-framebuffer countdown instead
of the global OSD state; P2 retains its local copied-frame overlay without the
configuration gate. The warnings-as-errors build and all five automated paths
passed after the correction, with logs in
`build/regression/automated-20260819-203639-17212`. The corrected runtime was
published with a cache-distinct ID at
`M:\SAME-LINKTEST\builds\handshake-v6-peer-notices-v2`.

### Symmetric disconnect notices

Connection loss now mirrors the successful connection feedback. The host sets
a one-shot disconnect event when the existing 500 ms remote-input timeout
releases P2's buttons; P1 displays `Player 2 disconnected` using the same
dedicated primary-framebuffer notice path. On the client, a transition from
`Connected` to `Waiting` preserves the last locally copied gameplay frame long
enough to overlay `Player 1 disconnected` for two seconds. It then switches to
the full-window `Waiting for host` state. Reconnection replaces the stale frame
only after a fresh video frame and displays `Player 1 connected` again.

The Windows build passed with warnings treated as errors. The expanded five-
path regression stops the client first in the OpenGL case and the host first in
the SDL case, proving both disconnect directions in addition to both connect
notices. The OpenGL host recorded its input timeout followed by
`player_2_disconnected`; the SDL client recorded `waiting` followed by
`player_1_disconnected`. All tests passed; logs are in
`build/regression/automated-20260819-210959-2820`. The verified complete
connection-lifecycle runtime was published as
`M:\SAME-LINKTEST\builds\handshake-v6-peer-lifecycle`.

### Bidirectional peer reconnect without restarting the remaining side

A physical menu test exposed a generation-lifetime bug: after P2 selected
Disconnect and joined the still-running host again, the new client reached the
accepted handshake and displayed `Player 1 connected`, but received no gameplay
frames. The old client input sequence remained on the host while the newly
launched P2 began again at sequence 1, so every new input packet was rejected as
stale and media activation never resumed.

The host now tracks the accepted handshake request ID as well as its UDP
endpoint. Repeated handshakes from the same client preserve sequence continuity
across a temporary outage. A new request ID or endpoint starts a fresh client
generation: it releases all P2 buttons, resets input timing and sequence state,
and clears queued client audio before accepting new input. This also handles an
OS-reused source port and does not weaken newest-sequence-wins checks within one
generation.

The regression suite now has a sixth path, `remote-reconnect`. It keeps one host
alive, launches P2, waits for video, terminates P2, verifies the host disconnect
notice, launches a second P2 and requires both a new first video frame and a
second host activation at input sequence 1. The warnings-as-errors build and all
six paths passed; logs are in
`build/regression/automated-20260820-171127-5984`. The matching two-PC test
runtime was published separately as
`M:\SAME-LINKTEST\builds\protocol-v6-reconnect`.

The reciprocal physical menu test then exposed the same lifetime assumption on
P2. When P1 selected Disconnect and hosted again, the continuing client accepted
the new handshake but retained the previous host's completed video and audio
sequence numbers. Because the restarted host began both streams again at low
sequence values, P2 rejected them and remained on the text-only connected frame.

P2 now treats a changed handshake host ID as a new host generation. It discards
incomplete video assembly, clears the completed/highest video sequence gates,
empties the audio ring and resets audio and clock sequence state. The ordinary
client process, controls and input sequence remain alive. Presentation waits for
the first complete frame from the new generation, then shows `Player 1
connected` over gameplay rather than leaving the black status frame visible.

A seventh regression path, `remote-host-reconnect`, keeps one P2 running while
it stops the first host and starts a second host on the same UDP port and session
ID. It verifies `Waiting`, the disconnect notice, host generation 2, accepted
continuing input and an actual generation-2 video frame at video sequence 1.
All seven paths and the warnings-as-errors build passed; logs are in
`build/regression/automated-20260820-172103-19520`. The bidirectional reconnect
runtime was published as
`M:\SAME-LINKTEST\builds\protocol-v6-bidirectional-reconnect`.

The subsequent physical two-PC verification passed with that runtime. P2 could
disconnect and join the still-running P1 again, and P1 could disconnect and host
again while the same P2 client remained open. In both directions the expected
disconnect/connected notices appeared and gameplay resumed instead of remaining
on the black text-only connected frame. This closes the physical protocol-v6
peer-lifecycle and bidirectional reconnect gate.

### Fixed-role performance-matrix launcher

With physical protocol-v6 lifecycle verification complete, the next evidence
gate is three five-minute repetitions for each fixed-role Ethernet/Ethernet and
Ethernet/Wi-Fi setup. `start-performance-baseline.cmd` now provides one
double-click entry point on both PCs. It selects one of the three documented
machine/network role assignments plus repetition 1-3, derives the same bounded
test label on both machines and delegates to the existing local-first diagnostic
launcher. Actual role, active adapter, executable hash and unique launch ID are
still detected independently, and completed logs are copied to the NAS only
after shutdown. Published builds now include both baseline-launcher files.
The clean matrix runtime and tooling bundle is published as
`M:\SAME-LINKTEST\builds\protocol-v6-baseline-matrix-v2`.
