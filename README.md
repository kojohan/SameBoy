# SameBoy Link

**SameBoy Link** is an experimental Windows-focused fork of [SameBoy](https://github.com/LIJI32/SameBoy) aimed at making Game Boy and Game Boy Color Link Cable multiplayer simple both locally and over the Internet.

> Development branch: `sameboy-link`
>
> Status: Phase 0 through Phase 3 are complete. Local Link, direct-IP Remote Host/Join, persistent P1/P2 controls, the Remote Client menu and SameBoy's full client-side OpenGL shader/filter pipeline are integrated into the normal SDL frontend. Protocol v6 adds an explicit connection handshake and visible waiting/session/protocol status while retaining protocol v5's physically verified low-latency media path. Its connection notices and bidirectional peer-reconnect lifecycle are physically verified on two PCs. The v5 path is playable over Ethernet and Wi-Fi and passed a five-minute manual public-IPv4/UDP port-forwarding test. PCM is the stable audio baseline; Opus remains experimental and deferred.

## What we are building

The first Internet implementation uses a **Remote Play** model:

```text
HOST PC

SameBoy P1 <------ local emulated link cable ------> SameBoy P2
   ^                                                   ^
   |                                                   |
local input                                      remote input
                                                       |
                                                       |
CLIENT PC ---------------------------------------------+

HOST PC ---------------- P2 video/audio -------------> CLIENT PC
```

Both Game Boy instances run on the host, so the Game Boy link cable remains local and timing-accurate. The client only sends Player 2 input and receives Player 2 video/audio.

This means:

- only the host needs the ROM;
- Remote Play does not require both users to match ROM region/revision/hash;
- P1 and P2 use separate save files on the host;
- the client can scale/render the native Game Boy framebuffer locally;
- nearest-neighbour/integer scaling and reusable SameBoy filters can remain client-side;
- Internet connection should require no manual IP/port forwarding in the normal flow.

A later optional **Native NetLink** backend may run one emulator on each PC and send serial/link information over the Internet, but it is deliberately not the first implementation.

## Planned user experience

Local multiplayer:

```text
Open ROM -> Local Link -> assign P1/P2 controllers -> Play
```

Internet multiplayer:

```text
Host Online Game -> Copy Invite Link -> friend clicks -> SameBoy Link opens -> Connected
```

The connection layer is planned to support direct IPv6/UDP, NAT traversal/hole punching, UPnP IGD, NAT-PMP, PCP and relay fallback so users normally never need to configure their router manually.

### Current direct-IP development flow

Until room codes and automatic connectivity are implemented:

1. The host opens a ROM and chooses `Link > Remote Link Settings…` to set the UDP port and shared session ID.
2. The host chooses `Link > Host Remote Link…`.
3. The client chooses `Link > Join Remote Link…`, enters `IP:port` and uses the same session ID.
4. The client presses Escape for local video, audio and P2 control settings. `Disconnect` returns to the ordinary SameBoy start window.

Internet testing currently requires manual UDP forwarding of the selected host port. Only the host needs the ROM.

For the fixed-role performance matrix, start
`start-performance-baseline.cmd` from the same published build on both PCs.
Choose the same setup and repetition on both sides, then follow the displayed
Host/Join roles and play for at least five minutes. The ordinary diagnostic
launcher records locally during play and exports uniquely named, matching test
labels to the shared `LOGS` root after normal shutdown.

## Streaming goals

The remote client receives Player 2's **native Game Boy framebuffer**, not a pre-scaled desktop capture.

Planned quality presets:

- **Balanced** — default low-latency native-resolution stream;
- **Pixel Perfect** — lossless native-resolution framebuffer;
- **Low Bandwidth** — more aggressive compression for constrained links.

Scaling and display effects happen locally on the client. GPU hardware encode/decode may be supported where benchmarks show a real end-to-end latency or CPU benefit.

## Project documentation

Start here depending on what you want to know:

- **[ROADMAP.md](ROADMAP.md)** — clear phase-by-phase development plan and exit criteria.
- **[TODO.md](TODO.md)** — actionable development checklist.
- **[DEVELOPMENT_PROGRESS.md](DEVELOPMENT_PROGRESS.md)** — completed work, measurements and regression evidence.
- **[REMOTE_PLAY_PERFORMANCE_PLAN.md](REMOTE_PLAY_PERFORMANCE_PLAN.md)** — physical network results and the measured optimization plan.
- **[build-faq.md](build-faq.md)** — Windows prerequisites, one-command build and automated Link smoke test.
- **[LINK_UI_ARCHITECTURE.md](LINK_UI_ARCHITECTURE.md)** — target Link menu, session modes and P1/P2 control ownership.
- **[TECHNICAL_OVERVIEW.md](TECHNICAL_OVERVIEW.md)** — contributor-friendly architecture overview.
- **[SAMEBOY_LINK_TECHNICAL_PLAN.md](SAMEBOY_LINK_TECHNICAL_PLAN.md)** — detailed implementation decisions and technical notes.
- **[SAMEBOY_LINK_PLAN.md](SAMEBOY_LINK_PLAN.md)** — product/feature planning notes.
- **[CONTRIBUTING.md](CONTRIBUTING.md)** — upstream SameBoy contribution guidance plus SameBoy Link workflow and validation notes.

## Current implementation priority

The completed foundation now includes the reproducible Windows build, dual-core
Local Link, explicit session modes, persistent independent controls, menu-driven
Host/Join/Disconnect, measured LAN Remote Play, adaptive PCM audio and a
settings-capable Remote Client with local SameBoy shaders/filters. Protocol v6
adds an explicit `Hello`/response handshake and visible `Connecting`, `Waiting`,
session-mismatch, protocol-mismatch and `Connected` states. When play begins,
P1 briefly sees `Player 2 connected` and P2 sees `Player 1 connected` directly
on their respective gameplay images. If either peer disappears, the remaining
player receives the corresponding `Player 2 disconnected` or
`Player 1 disconnected` notice; P2 then transitions to `Waiting for host` and
automatically resumes if the same connection returns. Disconnecting P2 and
joining again also works without restarting the host: each new client request
starts a fresh input-sequence generation and releases any old held buttons. If
P1 disconnects and hosts again, the still-running P2 recognizes the new host,
clears stale video/audio sequence state and resumes on the first fresh frame.
It retains protocol v5's media formats and reduced lossless-video datagram count
without adding a frame queue; repeated Wi-Fi/Wi-Fi v5 play is the accepted media baseline. Bounded client-side audio
arrival histograms now provide session and ten-second p50/p95/p99 measurements
without per-packet logging. A physical Wi-Fi/Wi-Fi verification had no audible
audio faults, so the current low-latency PCM settings remain unchanged. The next
work is:

1. complete the remaining fixed-role baseline matrix using the physically verified ten-second RTT/jitter/video/total-latency windows;
2. revisit PCM, priority scheduling or lower-bandwidth video only when repeated audible/visible evidence justifies it;
3. build session authentication and encryption on the v6 handshake, then add coordination, NAT traversal and relay fallback.

See [REMOTE_PLAY_PERFORMANCE_PLAN.md](REMOTE_PLAY_PERFORMANCE_PLAN.md),
[ROADMAP.md](ROADMAP.md) and [TODO.md](TODO.md) for the full breakdown.

## Technical principle

Keep the SameBoy emulator core as close to upstream as practical. Multiplayer policy, Remote Play, networking, invitations and save-transfer logic should live in the Windows/SDL frontend and fork-specific modules rather than being mixed into `Core/`.

SameBoy already exposes the serial APIs we need, and its libretro frontend already contains a working two-instance local link reference. Our first task is to adapt that architecture cleanly to the Windows SDL frontend.

## Saves

Remote Play represents two independent virtual cartridges on the host. P1 and P2 must therefore never share the same active save path.

V1 keeps both saves on the host. A later feature may let the remote player upload a personal P2 save before a session and receive the updated save afterward using transactional backup/hash verification.

## ROMs

SameBoy Link does not distribute commercial ROMs. Users are responsible for providing game data they are legally entitled to use.

## Upstream SameBoy

This repository is based on **SameBoy**, an open-source Game Boy / Game Boy Color emulator written in portable C with SDL, Cocoa and libretro frontends.

Official upstream repository: [LIJI32/SameBoy](https://github.com/LIJI32/SameBoy)

Official SameBoy website: [sameboy.github.io](https://sameboy.github.io/)

The project should remain structured so future upstream changes can be merged with minimal conflict.

## License

SameBoy is licensed under the Expat license, with the upstream project's additional iOS exception. See [LICENSE](LICENSE) for the complete license text and copyright notices.
