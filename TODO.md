# SameBoy Link — TODO

This file is the actionable development checklist. Keep `ROADMAP.md` focused on phases and goals; keep implementation details in `SAMEBOY_LINK_TECHNICAL_PLAN.md` and `TECHNICAL_OVERVIEW.md`.

## Now — Phase 0 / Phase 1

- [ ] Confirm reproducible Windows SDL debug build from `sameboy-link`.
- [ ] Document exact Windows prerequisites and build command.
- [ ] Add fork-specific frontend logging/timing category.
- [ ] Record runtime framebuffer format, frame cadence and audio sample rate.
- [ ] Introduce `EmulatorSlot` without changing single-player behavior.
- [ ] Move per-core framebuffer pointers/state into `EmulatorSlot`.
- [ ] Move per-core ROM/save-path state into `EmulatorSlot`.
- [ ] Add `GameSession` owning one or two slots.
- [ ] Keep `active_slot_count == 1` until regression testing passes.
- [ ] Verify normal battery saves, save states, input, audio and rendering still work.

## Next — Local Link MVP

- [ ] Create slot/core 1 (Player 2).
- [ ] Add `LocalLink` module.
- [ ] Port serial bit bridge from `libretro/libretro.c`.
- [ ] Port cycle-delta dual-core scheduler.
- [ ] Add independent vblank tracking for each core.
- [ ] Add independent P1/P2 controller state.
- [ ] Add side-by-side local rendering.
- [ ] Keep per-core audio streams identifiable.
- [ ] Guarantee separate P1/P2 battery-save paths.
- [ ] Add basic serial/link diagnostics.
- [ ] Test at least one DMG link game.
- [ ] Test at least one CGB link game.
- [ ] Run extended session test for drift/crashes/save corruption.

## LAN Remote Play

- [ ] Define protocol version and session identifiers.
- [ ] Define realtime P2 input packet with full button mask + sequence number.
- [ ] Implement UDP input sender on client.
- [ ] Implement remote-input receiver/queue on host.
- [ ] Apply newest valid P2 input state deterministically.
- [ ] Expose completed P2 native framebuffer before host scaling/composition.
- [ ] Implement raw/native framebuffer LAN reference transport.
- [ ] Implement client framebuffer reconstruction.
- [ ] Reuse SameBoy/SDL rendering path for client-side nearest-neighbour/integer scaling.
- [ ] Add remote P2 audio path.
- [ ] Add bounded video/audio queues.
- [ ] Drop obsolete video instead of accumulating latency.

## Streaming quality

- [ ] Add common encoder/decoder interface.
- [ ] Implement `Balanced` preset.
- [ ] Implement `Pixel Perfect` lossless preset.
- [ ] Add bit-identical host/client framebuffer validation test for Pixel Perfect.
- [ ] Implement `Low Bandwidth` preset.
- [ ] Benchmark raw vs fast lossless vs tile/XOR-delta approaches.
- [ ] Benchmark conventional low-latency codec(s).
- [ ] Benchmark GPU hardware encode/decode where available.
- [ ] Select backend based on measured total latency/CPU/bandwidth, not codec speed alone.

## Latency telemetry

- [ ] Timestamp client controller event.
- [ ] Timestamp input send/host receive/application.
- [ ] Timestamp host P2 frame completion.
- [ ] Timestamp encode begin/end.
- [ ] Timestamp network video send/receive.
- [ ] Timestamp decode begin/end.
- [ ] Timestamp client texture upload/presentation request.
- [ ] Add developer latency overlay.
- [ ] Track RTT, jitter, packet loss, queue depth and dropped frames.

## Internet coordination

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

- [ ] Simple top-level UI: Play / Local Link / Host Online / Join Online / Settings.
- [ ] Controller assignment UI.
- [ ] Connection-status UI.
- [ ] Quality preset UI.
- [ ] Compatibility matrix.
- [ ] Exportable diagnostic logs.
- [ ] Windows packaging.
- [ ] Automated smoke/regression tests.
- [ ] Contributor/development setup notes.

## Optional later — Native NetLink

- [ ] Define network serial/link event protocol.
- [ ] Require ROM identity/revision verification on both PCs.
- [ ] Adaptive link/jitter buffer.
- [ ] Clock-drift correction.
- [ ] Determinism tests for save-state restore + replay.
- [ ] Evaluate speculative execution/rollback only after measurements justify it.
