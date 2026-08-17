# SameBoy Link

**SameBoy Link** is an experimental Windows-focused fork of [SameBoy](https://github.com/LIJI32/SameBoy) aimed at making Game Boy and Game Boy Color Link Cable multiplayer simple both locally and over the Internet.

> Development branch: `sameboy-link`
>
> Status: planning / early implementation. The architecture and milestones are documented; the multiplayer frontend is not yet a finished release.

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
- **[TECHNICAL_OVERVIEW.md](TECHNICAL_OVERVIEW.md)** — contributor-friendly architecture overview.
- **[SAMEBOY_LINK_TECHNICAL_PLAN.md](SAMEBOY_LINK_TECHNICAL_PLAN.md)** — detailed implementation decisions and technical notes.
- **[SAMEBOY_LINK_PLAN.md](SAMEBOY_LINK_PLAN.md)** — product/feature planning notes.
- **[CONTRIBUTING.md](CONTRIBUTING.md)** — upstream SameBoy contribution guidance; fork-specific contributor notes will be added as implementation begins.

## Current implementation priority

The immediate development order is:

1. establish a reproducible Windows SDL build;
2. refactor the one-core SDL frontend into `EmulatorSlot` / `GameSession` abstractions without changing single-player behavior;
3. create a second SameBoy core;
4. port the proven local link implementation from `libretro/libretro.c`;
5. add independent P1/P2 input, rendering and save paths;
6. prove Remote Play input + native framebuffer streaming on LAN;
7. add Internet coordination, zero-config connectivity and invite links;
8. optimize latency based on measurements.

See [ROADMAP.md](ROADMAP.md) and [TODO.md](TODO.md) for the full breakdown.

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
