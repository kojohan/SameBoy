# SameBoy Link — UI and Session Architecture

This document defines the target user-facing architecture for SameBoy Link. It is the design contract for the frontend work that replaces the current developer command-line modes with normal SameBoy UI flows.

## Product principle

SameBoy Link remains one SameBoy application and one executable for every role. There is no separate server application or separate remote-play client application.

The normal SameBoy interface remains intact. Link functionality is exposed through a dedicated `Link` menu.

The same executable can operate as:

- ordinary single-player SameBoy;
- a two-core Local Link session;
- a Remote Host that runs both linked Game Boy cores;
- a Remote Client that displays and controls Player 2.

The UI selects a session mode. Networking, emulation and presentation behavior belong to the session implementation rather than being spread through unrelated frontend code.

## Target Link menu

Initial target:

```text
Link
├─ Local Link…
├─ Host Remote Link…
├─ Join Remote Link…
├─ Disconnect
└─ Remote Link Settings…
```

User-facing terminology should prefer **Host** and **Join** rather than requiring users to understand client/server terminology. Internal implementation names may continue to use `remote_host` and `remote_client`.

`Disconnect` is disabled when no link session is active. Other menu items should be enabled/disabled according to the current session state rather than allowing conflicting modes to start simultaneously.

## Controller mapping

SameBoy Link should reuse SameBoy's normal controller/input configuration UI rather than introducing a separate configuration system for multiplayer.

The normal control-mapping interface should expose Player 1 and Player 2 as separate selectable mappings, conceptually:

```text
Controls
├─ Map Controls P1…
└─ Map Controls P2…
```

The exact placement and wording may follow the existing SameBoy UI conventions, but P1 and P2 must be independently configurable.

Requirements:

- Player 1 and Player 2 have separate persistent input mappings;
- keyboard and supported game controllers can be assigned independently to either player;
- Local Link uses both mappings on the same computer;
- Remote Host uses the P1 mapping locally and receives P2 gameplay input from the network;
- Remote Client uses its local P2 mapping to generate the input state sent to the host;
- changing P2 controls should use the same mapping workflow and UI conventions as ordinary SameBoy controls;
- the temporary hard-coded P2 keyboard mapping used by the developer prototype must not be the final user-facing solution;
- single-player behavior and the existing P1 mapping should remain compatible with normal SameBoy usage.

The intent is that a user who already understands how to configure controls in SameBoy should configure multiplayer controls in the same place and in the same way, with the only meaningful addition being a P1/P2 selection.

## Session modes

The frontend should have an explicit session-mode concept equivalent to:

```text
SINGLE_PLAYER
LOCAL_LINK
REMOTE_HOST
REMOTE_CLIENT
```

The exact C enum/API may differ, but these four states are the intended behavioral model.

### SINGLE_PLAYER

Normal SameBoy behavior.

- one active `EmulatorSlot`;
- local video/audio/input;
- existing SameBoy behavior should remain unchanged wherever possible.

### LOCAL_LINK

Two Game Boy instances run inside the same SameBoy process.

- two active `EmulatorSlot` objects;
- `LocalLink` connects their emulated serial/link interfaces;
- dual-core scheduler keeps both cores synchronized;
- independent Player 1 and Player 2 controls using the configured P1/P2 mappings;
- independent cartridge persistence/save paths;
- both screens are presented locally;
- both players are controlled on the host PC.

This is the production UI form of the current `--local-link` developer path.

### REMOTE_HOST

The host runs **both** Game Boy instances. The emulated Game Boy link cable therefore remains local to one machine.

- two active `EmulatorSlot` objects on the host;
- the two cores use the same `LocalLink` and synchronized scheduler as Local Link;
- Player 1 input uses the host's configured P1 mapping;
- Player 2 input comes from the Remote Client;
- Player 2's completed framebuffer is streamed to the client;
- Player 2 audio is streamed to the client;
- Player 2 cartridge/save ownership remains on the host in V1;
- host may present Player 1 only or a diagnostic/optional side-by-side view.

This architecture deliberately keeps Internet latency away from the emulated Game Boy serial link. Network latency affects remote controller/video/audio transport instead of link-cable timing.

### REMOTE_CLIENT

The same SameBoy executable acts as the Player 2 endpoint.

For Remote Play V1 the client **does not run a Game Boy core**.

- receives Player 2 video from the host;
- receives Player 2 audio from the host;
- renders through the SameBoy/SDL frontend path where practical;
- captures local Player 2 input using the configured P2 mapping;
- sends Player 2 input state to the host;
- shows connection and latency diagnostics as appropriate;
- owns no ROM or Game Boy save state in V1.

This is the production UI form of the current remote-client developer path.

## Required ownership boundary

`GameSession` should be the central owner/coordinator of active play mode.

The frontend/menu layer should request transitions such as starting Local Link, hosting Remote Link, joining Remote Link or disconnecting. It should not independently implement emulator/network behavior.

The goal is to avoid accumulating mode checks such as `if (remote_host)` and `if (local_link)` throughout unrelated SDL frontend code. Mode-specific behavior should be encapsulated behind session/network interfaces wherever practical.

Conceptually:

```text
SameBoy SDL UI
    │
    ├── normal menus/render/input
    │
    └── Link menu
          │
          ▼
      GameSession
          │
          ├── SINGLE_PLAYER ── EmulatorSlot 0
          │
          ├── LOCAL_LINK ───── EmulatorSlot 0 + EmulatorSlot 1 + LocalLink
          │
          ├── REMOTE_HOST ──── EmulatorSlot 0 + EmulatorSlot 1 + LocalLink
          │                         │
          │                         └── Remote Play transport to P2
          │
          └── REMOTE_CLIENT ── Remote Play client transport/presentation
                                (no emulation core in V1)
```

## UI flows

### Local Link…

Target flow:

1. User chooses `Link > Local Link…`.
2. SameBoy obtains the ROM(s) required for Player 1 and Player 2.
3. A two-slot `GameSession` starts.
4. The configured P1/P2 controller mappings are applied.
5. Both linked screens are presented locally.
6. `Link > Disconnect` returns to a clean non-linked state.

The first polished implementation may initially use the same ROM for both slots if necessary, but the architecture must not assume this permanently; Game Boy link games can require different compatible cartridges/versions.

### Host Remote Link…

Target flow:

1. User chooses `Link > Host Remote Link…`.
2. SameBoy starts/creates a Remote Host session.
3. Host obtains a room/invite code once Internet coordination is implemented.
4. The host runs both Game Boy cores locally.
5. Player 2 connects remotely.
6. P1 uses the host's normal configured P1 controls; P2 input comes from the connected client.
7. P2 input/video/audio use the existing Remote Play transport pipeline.
8. The Game Boy serial link remains local between the two host cores.
9. Disconnect cleanly releases remote input and preserves host-owned persistence.

During the transition from developer prototype to Internet service, direct-IP connection may remain available as a developer/advanced option.

### Join Remote Link…

Target flow:

1. User chooses `Link > Join Remote Link…`.
2. User enters/pastes an invite code/link, or an advanced direct endpoint during development.
3. SameBoy resolves/connects to the host.
4. The normal application window becomes the Player 2 remote presentation.
5. The client's configured P2 controls generate the input state sent to the host.
6. Video/audio are received and presented locally.
7. Disconnect returns SameBoy to its normal idle/single-player frontend state.

## Relationship to current command-line modes

The existing developer command-line paths are valuable test harnesses and should not be removed until the menu flows are stable.

Current concepts include:

```text
--local-link
--remote-input-host
--remote-input-client
--remote-session
```

The Link menu should call the same underlying session and transport implementations rather than creating a second implementation. Command-line modes may remain as development/regression entry points after the UI is added.

## Remote Play V1 versus future Native NetLink

Remote Play V1 is the primary implementation target:

```text
HOST PC
  Game Boy P1 ── emulated local link ── Game Boy P2
      │                                  │
      │                                  ├── video ──► Remote Client
      │                                  ├── audio ──► Remote Client
      │                                  ◄── input ─── Remote Client
      ▼
  local P1
```

This is intentionally different from Native NetLink.

A possible later Native NetLink mode would run one Game Boy core on each computer and transport serial/link events over the network:

```text
PC 1                                      PC 2
Game Boy P1 ◄──── network link ────────► Game Boy P2
```

Native NetLink has substantially harder timing, jitter, synchronization and determinism requirements. It remains optional/later work and must not complicate the Remote Play V1 architecture prematurely.

If Native NetLink is implemented later, the existing `Link` menu remains the user-facing home for it. It may become an advanced connection mode or separate menu action without changing the core product principle that both peers use the same SameBoy application.

## Internet target

The final Remote Host/Remote Client UX should not require manual IP addresses or router configuration for ordinary users.

Planned progression:

1. current direct-IP developer transport;
2. coordination service;
3. room/session token model and short room codes;
4. endpoint candidate exchange;
5. direct IPv6 where possible;
6. UDP NAT traversal/hole punching;
7. automatic port mapping where useful;
8. relay fallback when direct connectivity fails;
9. invite links such as `sameboylink://join/<token>`;
10. automatic `Direct`/`Relay` connection status without exposing unnecessary networking complexity.

## Near-term implementation target

Before expanding the Internet coordination layer, convert the proven backend into the intended SameBoy interaction model.

Priority:

1. introduce/normalize explicit `GameSession` session modes;
2. add the `Link` menu to the SDL frontend;
3. expose separate `Map Controls P1` and `Map Controls P2` configuration through SameBoy's existing control UI;
4. wire `Local Link…` to the existing Local Link backend and both configured control mappings;
5. wire `Host Remote Link…` to the existing Remote Host backend;
6. wire `Join Remote Link…` to the existing Remote Client backend and P2 mapping;
7. implement clean session teardown/`Disconnect` behavior;
8. retain CLI entry points for automated/developer testing;
9. then continue room-code/coordination/NAT-traversal work against these stable UI/session boundaries.

## Acceptance criteria for the UI/session milestone

The milestone is complete when:

- ordinary SameBoy single-player still behaves normally;
- one executable provides all four session roles;
- SameBoy's normal controls UI allows P1 and P2 mappings to be configured independently;
- Local Link uses the configured P1 and P2 mappings without requiring hard-coded developer keys;
- Remote Client uses its configured P2 mapping for remote gameplay input;
- Local Link can be started from the `Link` menu without command-line arguments;
- Remote Host can be started from the `Link` menu without command-line arguments;
- Remote Client can be started/joined from the `Link` menu without command-line arguments;
- Local Link and Remote Host share the same dual-core/link implementation rather than duplicating it;
- Remote Client V1 does not unnecessarily instantiate a Game Boy core;
- `Disconnect` tears down network/link state cleanly and releases remote input;
- CLI developer paths continue to work for regression testing;
- the UI architecture leaves room for room codes, invite links, Direct/Relay status and optional future Native NetLink.

## Design decisions recorded

The following are intentional project decisions unless explicitly revisited:

1. SameBoy Link is a SameBoy fork/interface extension, not a separate launcher plus separate client/server applications.
2. Both Remote Host and Remote Client use the same SameBoy executable.
3. Link functionality belongs under a dedicated `Link` menu in the normal SameBoy UI.
4. Remote Play V1 runs both Game Boy cores on the host.
5. The Remote Client is a streaming/input endpoint and does not emulate a Game Boy in V1.
6. Local Link and Remote Host share the local dual-core/link scheduler implementation.
7. P1 and P2 controls are independently configurable through SameBoy's normal control-mapping UI.
8. Local Link uses both local mappings; Remote Client uses the P2 mapping for the input it sends to the host.
9. Developer CLI modes remain useful test paths but are not the intended end-user UX.
10. Native link-over-network is a later optional mode and must not block the Remote Play product path.
