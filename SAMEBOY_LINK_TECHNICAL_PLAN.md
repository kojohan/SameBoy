# SameBoy Link — Technical Implementation Plan

This document translates the product roadmap into concrete implementation work against the current SameBoy codebase.

## Current implementation status — 2026-08-20

T0–T8 and the first T10 audio transport are implemented in the Windows SDL frontend. Explicit `GameSession` modes now route CLI and menu entry points through controlled lifecycle transitions. The normal `Link` menu starts Local Link or direct-IP Remote Host/Join sessions, persistent P1/P2 mappings support two controllers with hotplug, and Remote Client has an Escape menu for local presentation, audio and P2 control settings. Protocol v8 adds automatic first-client pairing and a session-lifetime client lock to the host-owned linked cores, full-state Player 2 UDP input, reduced-burst lossless native framebuffers and adaptive-jitter-buffered 48 kHz stereo PCM. Local, LAN and direct public-IPv4 media tests have passed; v8 pairing, second-client rejection and bidirectional reconnect regressions pass.

Opus support is retained only as an experimental build option and is deferred because PCM was more stable in physical testing. The session/UI milestone described in `LINK_UI_ARCHITECTURE.md` and its physical two-PC four-mode regression are complete. Full Remote Client shader/filter reuse, pre-frame connection status and symmetric per-player connected/disconnected notices are complete. Protocol v8 preserves the zero-queue media baseline. Stronger transport security, coordination, NAT traversal and relay support are still required before public Internet release.

## Core architectural decisions

- Keep SameBoy `Core/` as close to upstream as practical.
- Remote Play is the primary Internet multiplayer mode: both linked Game Boys execute on the host.
- Native NetLink is a later optional backend.
- The remote video source is Player 2's native completed SameBoy framebuffer, not host-window capture.
- Scaling and presentation filters belong on the client.
- Internet multiplayer should require no manual IP/port entry or router configuration in the normal user flow.

## Codebase findings

SameBoy exposes the serial primitives needed for a two-core link through `Core/gb.h`, `Core/gb.c`, `Core/timing.c` and `Core/memory.c`. `libretro/libretro.c` is the working local-link reference with two `GB_gameboy_t` instances, separate framebuffers/input/audio, cross-connected serial callbacks, and a cycle-delta scheduler.

The upstream Windows SDL frontend was centered on one global core. This fork now implements per-emulator slots and a session manager without changing the serial emulation model in `Core/`.

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
```

Only the host requires the ROM in Remote Play mode. The client is a controller plus video/audio receiver and does not need to match ROM region/revision/hash.

## Native-resolution video and quality

Normally stream the native Game Boy framebuffer (typically 160x144), not an enlarged desktop image. A 32-bit 160x144 frame is 92,160 bytes uncompressed, roughly 5.25 MiB/s / 44 Mbit/s at ~59.7 fps before compression.

Client-side rendering preserves nearest-neighbour/integer scaling, SameBoy color correction/palettes and reusable SDL/OpenGL shaders/filters.

Quality presets:

- **Balanced (default):** fast lightweight native-resolution compression, lossless where practical.
- **Pixel Perfect:** lossless; reconstructed client framebuffer should be bit-identical to host P2 before filtering/scaling.
- **Low Bandwidth:** more aggressive compression but still decoded/presented as native-resolution source.

Benchmark raw framebuffer, fast lossless, changed-region/tile or XOR-delta compression, and conventional hardware/software low-latency codecs behind a common encoder interface. GPU hardware encode/decode may be an optional backend where measurement shows lower total latency or CPU use; do not assume GPU is faster for such a small source.

## Local link and scheduler

Introduce `EmulatorSlot` and `GameSession`. Port libretro's proven serial callback bridge into `LocalLink`. Initially execute both cores on one scheduler thread using the cycle-delta strategy; do not use independent emulator threads for serial execution.

## Remote input

Send complete current P2 button state with protocol/session ID, sequence number and timestamp. Newer packets supersede older ones. UDP is appropriate for realtime input.

## Remote Play save-file handling

Both active Game Boys run on the host, so the host owns active emulator state during Remote Play. P1 and P2 must always use distinct persistent save paths even when both cores load the same ROM.

Example:

```text
Pokemon Crystal [P1].sav
Pokemon Crystal [P2].sav
```

V1 keeps both saves on the host. A later client-owned portable P2-save feature may reliably upload a save before the session and return the updated save afterward without requiring a ROM or emulator on the client.

Portable save transfer must be transactional: temporary files, previous-save backup, size/hash verification (e.g. SHA-256), promotion only after successful verification, controlled save directories, reliable transfer channel, and recovery after disconnect. Design persistent storage as a per-slot bundle so RTC/auxiliary cartridge data can be supported in addition to a plain `.sav`.

Native NetLink is different: each PC runs its own emulator and normally owns its own save locally.

## Zero-configuration Internet connectivity

Normal users should never need to find a public IP address, choose a port, or manually configure port forwarding.

Connection establishment should try several mechanisms automatically and report only a simple result such as `Direct` or `Relay` to the user.

Recommended strategy:

```text
1. Exchange candidate endpoints through coordination server
2. Try direct IPv6 where available
3. Try UDP NAT traversal / hole punching
4. Try automatic router mapping where useful:
   - UPnP IGD
   - NAT-PMP
   - PCP
5. Fall back to relay if direct connection cannot be established
```

The exact ordering may be adjusted after real-world testing. These methods may also be attempted in parallel when that reduces connection time safely.

UPnP is not sufficient by itself because it may be disabled/unsupported and cannot solve every CGNAT, double-NAT, hotel, enterprise or mobile-network configuration. Relay fallback is therefore required for a reliable consumer-facing experience.

Direct P2P is preferred because video traffic is much larger than input traffic and direct transport minimizes both server bandwidth and latency.

## Coordination server

The coordination service does not emulate Game Boy hardware. It manages short-lived session metadata needed to bring peers together:

- create/join room
- opaque room/session token
- protocol/application compatibility
- endpoint candidates (IPv4/IPv6)
- NAT traversal coordination
- relay credentials/endpoint when required
- session expiry and teardown

Do not place the host ROM, raw save path, or unnecessary personal/network information into invite URLs.

## Room codes and invite links

Support both manual short room codes and one-click invite links.

Example user flow:

```text
HOST
Create Online Game
      |
      v
Coordination server creates session
      |
      +--> Room code: K7M4Q
      |
      +--> Copy Invite Link
```

The invite should carry an opaque, short-lived session token, not the host's raw IP address/port.

### Custom application URL

Register a Windows URL protocol handler such as:

```text
sameboylink://join/<opaque-session-token>
```

Clicking the link launches/focuses SameBoy Link, parses the join token, contacts the coordination server, resolves the current session, and automatically begins connection establishment.

### HTTPS share link

Also support a normal shareable HTTPS form such as:

```text
https://<project-domain>/join/<opaque-session-token>
```

This is preferable for Discord/chat/email because ordinary HTTPS links are widely recognized. The landing page can attempt to open the registered `sameboylink://` handler and otherwise provide an `Open in SameBoy Link` action and installation information.

The concrete public domain is intentionally not fixed in the code plan until a domain is selected.

### Invite-token requirements

Invite/session tokens should:

- be cryptographically random/unguessable rather than sequential IDs
- be opaque to clients
- expire automatically
- become invalid when the room/session is closed
- resolve server-side to current endpoint/relay/session metadata
- not expose IP addresses, ports, save filenames, filesystem paths, ROM paths or authentication secrets directly in the URL
- be scoped to joining a session rather than granting broad account/server authority

The coordination service can therefore change the host's effective endpoint, NAT candidate or relay path without invalidating the invite link while the session remains active.

## Automatic join flow

```text
User clicks invite
      |
Windows opens SameBoy Link
      |
Client extracts opaque token
      |
HTTPS/TLS request to coordination service
      |
Validate token + protocol compatibility
      |
Receive session connection candidates
      |
Try direct/hole-punch/router mapping as appropriate
      |
Relay fallback if required
      |
Connected
      |
P2 input -> host
P2 video/audio <- host
```

No IP address or port is presented to the user in the normal flow.

## Security notes for URL handling

Treat custom URLs as untrusted input. Enforce strict scheme/path/token parsing and maximum lengths. Never allow URL parameters to become arbitrary filesystem paths or command-line execution. Do not automatically load arbitrary local ROM/save files from an invite URL. Require the server token to resolve to a valid live session before connecting.

The coordination API should use authenticated/encrypted transport (TLS). Realtime P2P/relay traffic should also gain session authentication and encryption before public Internet release.

## Audio

Keep per-core audio separate. Host local output may select P1/P2/mix; Remote Play normally sends P2 audio. Use small blocks and deliberately bounded jitter buffers.

## Latency telemetry

Instrument controller event, input send/receive/application, P2 frame completion, encode, video send/receive, decode, texture upload and presentation request. Initial targets are roughly under 1 ms each for host emulation processing, encode and decode where hardware permits, with total local streaming processing around 1–3 ms excluding network/frame/display waits. These are measurement targets, not correctness assumptions.

## Implementation order

1. **T0 Windows baseline** — reproducible SDL build and instrumentation.
2. **T1 EmulatorSlot** — remove one-core frontend assumption.
3. **T2 LocalLink** — second core + libretro serial bridge.
4. **T3 Dual scheduler** — cycle-delta synchronization.
5. **T4 Multiplayer input** — separate P1/P2 ownership.
6. **T5 Dual presentation** — local dual video/audio.
7. **T6 Save isolation** — independent P1/P2 persistent data.
8. **T7 Remote input** — direct-IP/LAN prototype.
9. **T8 Raw/native video reference** — LAN framebuffer stream and latency measurement.
10. **T9 Quality/codec layer** — Balanced/Pixel Perfect/Low Bandwidth and optional hardware codecs.
11. **T10 Remote audio** — bounded low-latency P2 audio.
12. **T11 Coordination service** — room/session tokens and endpoint exchange.
13. **T12 Zero-config connectivity** — IPv6/direct UDP hole punching + UPnP/NAT-PMP/PCP + relay fallback.
14. **T13 Invite links** — room codes, `sameboylink://` Windows handler and HTTPS share links.
15. **T14 Client save transfer (optional)** — portable transactional P2 saves.
16. **T15 Latency optimization** — tune scheduling/queues/transport/presentation.
17. **T16 Native NetLink** — optional serial-over-Internet backend after Remote Play is stable.

## Historical first coding task — complete

Introduce `EmulatorSlot` around the existing SDL `GB_gameboy_t` while keeping one active slot and preserving normal SameBoy behavior. Only after that builds and behaves identically should Core B and LocalLink be enabled.

## Next coding tasks

1. Physically verify protocol-v8 automatic pairing and first-client locking on two PCs.
2. Implement coordination, NAT traversal and relay fallback so manual public-IP entry and port forwarding are no longer needed.
3. Resume performance tuning only for a repeatable audible or visible fault.
4. Replace the development pairing/transport security before public release.
