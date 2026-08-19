# SameBoy Link — TODO

This file is the actionable development checklist. Keep `ROADMAP.md` focused on phases and goals; keep implementation details in `SAMEBOY_LINK_TECHNICAL_PLAN.md` and `TECHNICAL_OVERVIEW.md`.

## Complete — Phase 0 / Phase 1

- [x] Confirm reproducible Windows SDL debug build from `sameboy-link`.
- [x] Document exact Windows prerequisites and build command.
- [x] Add fork-specific frontend logging/timing category.
- [x] Record runtime framebuffer format, frame cadence and audio sample rate.
- [x] Introduce `EmulatorSlot` without changing single-player behavior.
- [x] Move per-core framebuffer pointers/state into `EmulatorSlot`.
- [x] Move per-core ROM/save-path state into `EmulatorSlot`.
- [x] Add `GameSession` owning one or two slots.
- [x] Keep `active_slot_count == 1` until regression testing passes.
- [x] Verify normal battery saves, save states, input, audio and rendering still work.

## Complete — Local Link MVP

- [x] Create slot/core 1 (Player 2).
- [x] Add `LocalLink` module.
- [x] Port serial bit bridge from `libretro/libretro.c`.
- [x] Port cycle-delta dual-core scheduler.
- [x] Add independent vblank tracking for each core.
- [x] Add independent P1/P2 controller state.
- [x] Add side-by-side local rendering.
- [x] Keep per-core audio streams identifiable.
- [x] Guarantee separate P1/P2 battery-save paths.
- [x] Add basic serial/link diagnostics.
- [x] Test at least one DMG link game.
- [x] Test at least one CGB link game.
- [x] Run extended session test for drift/crashes/save corruption.

## Current — UI and session integration

- [x] Add explicit `SINGLE_PLAYER`, `LOCAL_LINK`, `REMOTE_HOST` and `REMOTE_CLIENT` modes to `GameSession`.
- [x] Centralize valid session start/stop transitions and teardown.
- [x] Route existing CLI modes through the same session lifecycle API.
- [x] Add a dedicated `Link` submenu to SameBoy's existing SDL menu system.
- [x] Add `Local Link…`, `Host Remote Link…`, `Join Remote Link…`, `Disconnect` and `Remote Link Settings…` actions.
- [x] Add independent persistent keyboard mappings for P1 and P2.
- [x] Add independent persistent controller selection/mappings for P1 and P2.
- [x] Use both local mappings in Local Link and the client P2 mapping in Remote Play.
- [x] Wire Local Link, Host and Join menu actions to the existing backends.
- [x] Ensure `Disconnect` releases network/link/input state and returns to a clean session.
- [x] Keep CLI modes working as developer/regression entry points.
- [x] Add a Remote Client Escape menu with persistent local video/audio/P2-control settings.
- [x] Return Remote Client to the ordinary idle frontend after Disconnect.
- [x] Regression-test all four modes plus ordinary single-player.
  - [x] One-PC regression: headless single-player, SDL single-player, menu-driven
    Local Link, menu-driven Remote Host/Join loopback and clean Disconnect.
  - [x] Repeat the menu-driven Remote Host/Join flow on two physical PCs.

## LAN Remote Play

- [x] Define protocol version and session identifiers.
- [x] Define realtime P2 input packet with full button mask + sequence number.
- [x] Implement UDP input sender on client.
- [x] Implement remote-input receiver/queue on host.
- [x] Apply newest valid P2 input state deterministically.
- [x] Expose completed P2 native framebuffer before host scaling/composition.
- [x] Implement raw/native framebuffer LAN reference transport.
- [x] Implement client framebuffer reconstruction.
- [x] Make the remote P2 window freely resizable without stretching its aspect ratio.
- [x] Add a runtime host-view toggle between P1-only and side-by-side presentation.
- [x] Reuse SameBoy's OpenGL shader/filter path for client-side presentation, with an SDL renderer fallback.
- [x] Remove the temporary P2 button-state overlay from Remote Client gameplay presentation.
- [x] Add remote P2 audio path.
- [x] Add bounded video/audio queues.
- [x] Drop obsolete video instead of accumulating latency.
- [x] Replace the fixed PCM queue with an adaptive jitter buffer and clock-drift correction.
- [x] Implement selectable Opus Restricted Low Delay audio with 5 ms packets and PLC.
- [x] Compare PCM and Opus on the physical Wi-Fi client; retain PCM as the stable baseline and defer Opus.

## Current — Remote Play pacing and resilience

- [x] Add a double-click Windows launcher that exports complete Host and Remote Client diagnostic logs.
- [x] Capture direct-Internet and opposite-direction Ethernet/Wi-Fi physical measurements.
- [x] Add a host/client log summarizer with JSON/Markdown output and shutdown-tail handling.
- [x] Record machine role, active nonzero-speed network medium/link speed and test label in log metadata.
- [x] Add bounded p50/p95/p99/max and ten-second-window telemetry for audio packet arrival.
- [x] Physically verify schema-v3 audio windows and active-adapter metadata on Wi-Fi/Wi-Fi.
- [x] Add comparable ten-second-window telemetry for RTT/jitter, video network time and total latency.
- [x] Complete an extended 7.5-minute Wi-Fi/Wi-Fi schema-v4 baseline.
- [ ] Add sender queue-age windows only if a sender queue is implemented; no such queue exists in the accepted baseline.
- [x] Count frames actually presented separately from completed and superseded frames.
- [ ] Run three five-minute repetitions for both-Ethernet and fixed-role Ethernet/Wi-Fi baselines.
- [ ] Add a bounded priority sender scheduler: input/clock, then audio, then video.
- [x] Add a zero-buffer 1,280-byte video payload experiment with host burst and client receive-span telemetry.
- [x] Present P1-only Local Link/Remote Host from the latched complete framebuffer to prevent mixed-frame seams.
- [ ] Pace video chunks across the frame interval and drop stale unsent video before it delays realtime traffic.
- [x] Evaluate PCM startup, adaptive target/hysteresis, capacity and drift from measured percentiles; retain the current low-latency settings because the physical run had no audible fault.
- [x] Evaluate zero-queue adaptive VSync on a physical 60 Hz client; retain unbuffered VSync-off fallback where unsupported.
- [ ] Add deterministic delay/jitter/loss regression after the physical LAN gates pass.
- [ ] Meet the quantitative P0–P4 gates in `REMOTE_PLAY_PERFORMANCE_PLAN.md`.

## Streaming quality

- [ ] Add common encoder/decoder interface.
- [x] Implement first `Balanced` preset with lossless pixel RLE and raw fallback.
- [ ] Implement `Pixel Perfect` lossless preset.
- [ ] Add bit-identical host/client framebuffer validation test for Pixel Perfect.
- [ ] Implement `Low Bandwidth` preset.
- [ ] Benchmark raw vs fast lossless vs tile/XOR-delta approaches.
- [ ] Benchmark conventional low-latency codec(s).
- [ ] Benchmark GPU hardware encode/decode where available.
- [ ] Select backend based on measured total latency/CPU/bandwidth, not codec speed alone.

## Latency telemetry

- [x] Timestamp client controller event.
- [x] Timestamp input send/host receive/application.
- [x] Timestamp host P2 frame completion.
- [x] Timestamp encode begin/end.
- [x] Timestamp network video send/receive.
- [x] Timestamp decode begin/end.
- [x] Timestamp client texture upload/presentation request.
- [x] Add developer latency overlay.
- [x] Track RTT, jitter, packet loss, queue depth and dropped frames.

## Internet coordination

- [x] Prove the current direct-IP protocol across public IPv4 with temporary manual UDP forwarding.
- [ ] Define coordination-service API.
- [ ] Create room/session token model.
- [ ] Generate short room codes.
- [ ] Generate cryptographically random opaque invite tokens.
- [ ] Add token expiry/session teardown.
- [ ] Exchange IPv4/IPv6 endpoint candidates.
- [ ] Add protocol-version compatibility check.
- [ ] Keep ROM/save/filesystem paths out of invite metadata.

## Zero-config connectivity

- [ ] Direct IPv6 connection attempt.
- [ ] UDP NAT traversal / hole punching.
- [ ] UPnP IGD mapping support.
- [ ] NAT-PMP support.
- [ ] PCP support.
- [ ] Relay transport fallback.
- [ ] Prefer direct path automatically when available.
- [ ] Show simple `Direct` / `Relay` connection status.
- [ ] Test CGNAT/double-NAT/restrictive-network scenarios.

## Invite links

- [ ] Register Windows `sameboylink://` URL scheme.
- [ ] Implement strict URL/token parser.
- [ ] Implement `sameboylink://join/<token>` handling.
- [ ] Define HTTPS share-link format.
- [ ] Add `Copy Invite Link` UI action.
- [ ] Auto-launch/focus app and resolve token through coordination service.
- [ ] Reject expired/invalid tokens cleanly.
- [ ] Ensure URLs cannot inject filesystem paths or arbitrary commands.

## Save handling

- [ ] Store P1/P2 persistence in separate controlled directories/paths.
- [ ] Never allow two cores to alias the same active save path.
- [ ] Preserve P2 host-owned save after normal or unexpected disconnect in V1.
- [ ] Design persistent-data bundle abstraction for `.sav`, RTC and auxiliary state.
- [ ] Later: client-owned portable P2 save upload.
- [ ] Later: transactional return of updated P2 save.
- [ ] Later: temporary files + backup + SHA-256 verification.
- [ ] Later: explicit recovery/resume after interrupted save transfer.

## Security before public Internet release

- [ ] Treat all network packets and invite URLs as untrusted.
- [ ] Validate packet lengths, versions, sequence ranges and session IDs.
- [ ] Add rate limiting / malformed-packet handling.
- [ ] Use TLS for coordination service.
- [ ] Authenticate realtime sessions.
- [ ] Encrypt P2P/relay traffic before public release.
- [ ] Never accept peer-provided arbitrary local file paths.
- [ ] Do not deserialize arbitrary remote SameBoy save states in Remote Play mode.

## Release/polish

- [ ] Polish the normal SameBoy `Link` menu and session dialogs.
- [ ] Polish P1/P2 controller assignment and validation feedback.
- [x] Connection-status UI before the first client frame plus per-player connected/disconnected gameplay notices and client transition back to waiting.
- [ ] Quality preset UI.
- [ ] Compatibility matrix.
- [x] Exportable diagnostic logs.
- [ ] Windows packaging.
- [x] Automated one-PC Windows smoke/regression test for single-player, Local Link, OpenGL/SDL Remote Play loopback and handshake error paths.
- [x] Contributor/development setup notes.

## Optional later — Native NetLink

- [ ] Define network serial/link event protocol.
- [ ] Require ROM identity/revision verification on both PCs.
- [ ] Adaptive link/jitter buffer.
- [ ] Clock-drift correction.
- [ ] Determinism tests for save-state restore + replay.
- [ ] Evaluate speculative execution/rollback only after measurements justify it.
