# SameBoy Link — Roadmap

This roadmap describes the planned development phases for the SameBoy Link fork. The project is experimental and the order may change when measurements or compatibility testing show a better path.

## Project goal

Build a Windows-focused SameBoy fork that makes Game Boy / Game Boy Color Link Cable multiplayer simple locally and over the Internet.

The primary Internet mode is **Remote Play**:

- the host runs both linked SameBoy instances locally;
- the client sends Player 2 input;
- the host streams Player 2 video/audio back;
- only the host needs the ROM;
- the Game Boy serial link never crosses the Internet.

A later optional **Native NetLink** mode may run one emulator on each PC and transport link/serial timing over the network.

---

## Phase 0 — Reproducible Windows baseline

**Status:** Complete (2026-08-17).

**Goal:** prove that the current SDL/Windows fork builds and runs before structural changes.

Deliverables:

- reproducible Windows debug build;
- build notes/toolchain documentation;
- timing/log instrumentation;
- no regression in ordinary single-player SameBoy behavior.

**Exit criterion:** an upstream-derived Windows build starts and plays normal GB/GBC ROMs correctly.

---

## Phase 1 — Frontend multi-instance foundation

**Status:** Complete (2026-08-17).

**Goal:** remove the SDL frontend assumption that exactly one `GB_gameboy_t` exists.

Deliverables:

- `EmulatorSlot` abstraction;
- `GameSession` abstraction;
- per-slot framebuffer and persistent data paths;
- single-player continues to work through slot 0.

**Exit criterion:** one-player behavior is unchanged while the frontend is structurally ready for a second core.

---

## Phase 2 — Local Link MVP

**Status:** Complete (2026-08-17).

**Goal:** run two SameBoy instances in one Windows process with a real emulated Game Boy link between them.

Deliverables:

- second SameBoy core;
- local serial bridge based on SameBoy's existing libretro implementation;
- shared cycle-delta scheduler;
- independent P1/P2 input;
- side-by-side local rendering;
- independent P1/P2 save paths;
- basic link diagnostics.

**Exit criterion:** known two-player link games establish and maintain a working session for extended play.

---

## Phase 3 — Remote Play LAN prototype

**Status:** Complete (the two-PC LAN playability criterion and full-pipeline latency measurement were both verified on 2026-08-17 with an Ethernet host and Wi-Fi client).

**Goal:** prove the host/client model before adding matchmaking or Internet traversal.

Deliverables:

- client sends complete P2 controller state to host;
- both emulators remain on host;
- native P2 framebuffer is streamed to client;
- client renders/scales locally;
- P2 audio streaming;
- latency telemetry across the full pipeline.

**Exit criterion:** two PCs on a LAN can play a link game with the remote player controlling P2 and seeing/hearing P2 output.

---

## Phase 4 — Streaming quality layer

**Status:** In progress (Balanced lossless video and adaptive PCM are physically verified. Opus was tested and deferred; client resizing and independent P1-only host presentation are complete. The current transport also passed a five-minute manual public-IPv4 test; Internet-oriented video pacing/compression remains).

**Goal:** provide good image quality without adding unnecessary latency.

Planned modes:

- **Balanced** — implemented first pass using lossless pixel RLE with automatic raw fallback;
- **Pixel Perfect** — lossless native-resolution transport;
- **Low Bandwidth** — more aggressive compression for constrained connections.

Client-side presentation remains local so nearest-neighbour scaling, integer scaling, SameBoy color correction and reusable SameBoy shaders/filters can be applied after decode.

Candidate backends include raw/reference transport, fast lossless compression, tile/XOR delta methods, conventional low-latency codecs, and optional GPU hardware encode/decode where measurement shows a benefit.

**Exit criterion:** stream quality is selectable and queues remain bounded without latency growth.

---

## Current cross-phase milestone — UI and session integration

**Status:** Implementation complete; final physical four-mode regression in progress (2026-08-18).

**Goal:** convert the proven CLI backends into the intended SameBoy interaction model before expanding Internet coordination.

Deliverables:

- explicit `SINGLE_PLAYER`, `LOCAL_LINK`, `REMOTE_HOST` and `REMOTE_CLIENT` session modes;
- one lifecycle/transition API owned by `GameSession`;
- a dedicated `Link` menu in the normal SameBoy interface;
- independent persistent P1/P2 keyboard and controller mappings;
- Local Link, Host, Join and Disconnect actions backed by the existing implementations;
- retained CLI entry points for regression and developer testing.
- a Remote Client Escape menu for local video/audio/P2-control settings and clean return to the idle frontend.

**Exit criterion:** all four roles are selectable in the same executable, normal single-player behavior remains intact, and Disconnect releases link/network/input state cleanly.

See `LINK_UI_ARCHITECTURE.md` for the design contract. Automated build, loopback, menu and Disconnect checks pass. Final physical Host/Join verification, full client shader reuse, visual polish, packaging and compatibility work remain.

---

## Phase 5 — Internet coordination

**Goal:** remove manual IP/port exchange.

Deliverables:

- coordination service;
- room/session creation;
- short room codes;
- opaque short-lived invite tokens;
- endpoint exchange;
- protocol compatibility checks.

**Exit criterion:** host and client can discover each other through a room code without manually entering IP addresses.

---

## Phase 6 — Zero-configuration connectivity

**Goal:** make port forwarding unnecessary in the normal flow.

Connection mechanisms:

- direct IPv6 where available;
- UDP hole punching / NAT traversal;
- UPnP IGD;
- NAT-PMP;
- PCP;
- relay fallback.

Direct P2P is preferred for latency and server-bandwidth reasons. Relay exists so restrictive NAT/CGNAT networks still work.

**Exit criterion:** typical users can connect without opening router settings.

---

## Phase 7 — One-click invitations

**Goal:** reduce joining a game to clicking a link.

Deliverables:

- `sameboylink://join/<token>` Windows URL handler;
- HTTPS share-link form;
- `Copy Invite Link` UI;
- automatic app launch/join;
- secure token parsing and expiry.

Target flow:

```text
Host Game -> Copy Invite Link -> friend clicks -> SameBoy Link opens -> Connected
```

**Exit criterion:** no IP, port or room-code typing is required when using an invite link.

---

## Phase 8 — Save portability

**Goal:** allow remote players to bring persistent game saves without requiring a ROM locally.

V1 already keeps distinct P1/P2 save files on the host. This later phase adds optional client-owned P2 save transfer.

Deliverables:

- transactional upload before session;
- verified download after session;
- backups and temporary files;
- cryptographic hash validation;
- RTC/auxiliary persistent-data support where required;
- recovery after disconnect.

**Exit criterion:** a remote player can safely bring a Pokémon-style save to a host and receive the updated save back.

---

## Phase 9 — Polish and release engineering

**Goal:** make the project usable beyond development testing.

Deliverables:

- simple Windows multiplayer UI;
- controller configuration;
- connection/latency status;
- developer diagnostics behind an advanced option;
- packaging/update strategy;
- compatibility matrix;
- automated smoke/regression testing.

---

## Phase 10 — Native NetLink (optional/experimental)

**Goal:** investigate one-emulator-per-PC link transport after Remote Play is stable.

This mode requires significantly harder synchronization and compatibility work because Game Boy serial timing must cross the network.

Potential work:

- serial-event transport;
- adaptive link buffer;
- clock-drift correction;
- deterministic snapshots;
- speculative execution / rollback if justified by measurements;
- ROM/revision/hash verification on both PCs.

This phase must not complicate or delay the primary Remote Play implementation.
