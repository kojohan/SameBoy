# Remote Play performance evidence and optimization plan

This document is the canonical record for physical Remote Play performance
tests and the optimization work derived from them. Functional regression
history remains in `DEVELOPMENT_PROGRESS.md`; actionable work remains mirrored
in `TODO.md`.

## Measurement rules

- Use the same checksummed build on both PCs.
- Start both roles with `start-sameboy-with-log.cmd` and exit normally so final
  counters are flushed.
- Store complete host and client folders separately; do not rename only the
  combined file and overwrite raw logs.
- Record which physical PC is Host/Client and whether each path is Ethernet,
  Wi-Fi or Internet.
- Derive active media duration from received media counters rather than total
  launcher lifetime. Each PCM packet represents 5 ms; video runs nominally at
  59.727501 Hz.
- Treat the host/client counter difference at shutdown carefully. The host can
  continue transmitting for roughly its 500 ms input timeout after the client
  closes, which is not network loss.
- Repeat comparisons with fixed machine roles. The two LAN direction tests
  below also swapped the desktop and laptop roles, so network medium and
  machine performance are partially confounded.

All tests below used protocol v4, PCM S16LE stereo at 48 kHz, lossless pixel RLE
video, session ID 1 and development source commit `1ac9ddab8a17`. The tested
source tree was dirty and is evidence for development decisions, not a release
qualification.

## Test A — direct public IPv4

The desktop host listened on UDP 45930 through a temporary router forward and
the remote client connected over the public Internet. The active stream ran for
approximately 484.5 seconds.

| Host measurement | Result |
|---|---:|
| Input packets accepted / rejected / stale | 11,422 / 0 / 0 |
| Video frames sent / host-side dropped | 28,945 / 0 |
| Video chunks sent | 586,302 |
| Encoded video ratio | 0.219 |
| PCM packets sent / host-side dropped | 96,884 / 0 |
| Clock pings / pongs | 1,091 / 1,091 |
| P1 / P2 serial bits at disconnect | 117,416 / 93,048 |

The transport and linked game session worked for more than eight minutes with
no host-side send failure. Detailed client receive metrics were not captured:
the first logging launcher did not retain stderr when Join relaunched the
Remote Client process. The launcher now gives that child explicit stdout/stderr
files, and a self-test confirms that client diagnostics reach the combined log.

This remains a development proof only. Protocol v4 has no authentication or
encryption, and manual forwarding must not be treated as a public feature.

## Test B — Wi-Fi host, Ethernet client

The laptop hosted over Wi-Fi and the desktop client used Ethernet. The active
client stream ran for approximately 51.0 seconds.

| Client measurement | Result |
|---|---:|
| Video completed / dropped / rejected | 3,046 / 0 / 0 |
| Video superseded before presentation | 99 (3.25%) |
| Audio packets dropped / stale / decode errors | 0 / 0 / 0 |
| Audio underflows / trim events | 33 / 13 |
| Audio frames trimmed | 2,340 (48.75 ms) |
| Maximum audio arrival gap | 100.425 ms |
| Final audio target / buffered | 100 / 59 ms |
| Final smoothed RTT / jitter | 13.545 / 4.955 ms |
| Average / maximum input-to-present latency | 41.663 / 74.622 ms |

The first 11 underflows occurred during the first 6.2 seconds. After roughly
27 stable seconds, underflows resumed about once per second until disconnect.
No audio sequence gaps occurred. This combination points to bursty arrival and
sender/client pacing rather than packet loss: the maximum arrival gap exceeded
the usable headroom of a jitter buffer capped at a 100 ms target and 120 ms
physical queue.

## Test C — Ethernet host, Wi-Fi client

The desktop hosted over Ethernet and the laptop client used Wi-Fi. The active
client stream ran for approximately 98.3 seconds.

| Client measurement | Result |
|---|---:|
| Video completed / dropped / rejected | 5,873 / 8 / 0 |
| Video superseded before presentation | 192 (3.27%) |
| Audio packets dropped / stale / decode errors | 12 / 0 / 0 |
| Audio underflows / trim events | 2 / 49 |
| Audio frames trimmed | 10,011 (208.56 ms) |
| Maximum audio arrival gap | 86.261 ms |
| Final audio target / buffered | 90 / 80 ms |
| Final smoothed RTT / jitter | 15.480 / 5.944 ms |
| Clock uncertainty | 1.129 ms |
| Average / maximum input-to-present latency | 36.527 / 167.422 ms |

The two underflows, all 12 missing audio packets and all eight dropped video
frames belong to one disturbance around client time 28.5 seconds. Four audio
sequence gaps lost 3, 3, 2 and 4 packets. Video network time reached 142.464 ms,
smoothed RTT temporarily reached 49.261 ms and three input samples exceeded
100 ms total latency. The session recovered without stale packets, decoder
errors or rejected frames and remained stable afterward.

Moving the host workload to the Ethernet desktop reduced normalized audio
underflow frequency by approximately 97% compared with Test B. This is not a
pure Ethernet/Wi-Fi comparison because the PCs also swapped roles: host average
PCM encode time improved from 0.10 to 0.06 microseconds, while client average
decode time increased from 2.83 to 6.36 microseconds on the laptop.

## Evidence-based conclusions

1. Protocol validation and bounded queues are working: rejected/stale counts
   remained zero and neither host accumulated send drops.
2. The lossless stream currently needs roughly 9.5–9.7 Mbit/s for video plus
   1.536 Mbit/s for PCM before UDP/IP overhead. Sending a frame's roughly 20
   chunks as a burst competes with 5 ms audio, input and clock packets.
3. A wired host is the preferred current setup. It greatly improved continuous
   audio delivery, although Wi-Fi can still produce an isolated 80–140 ms burst.
4. A larger fixed audio buffer alone is not the right first fix. Test B reached
   its 100 ms target and still underflowed, while Test C alternated between
   trimming bursts and later consuming the queue.
5. Video superseded-before-present stayed essentially constant (3.25% versus
   3.27%) across opposite network directions. Presentation/send pacing, rather
   than packet loss alone, is the likely cause.
6. The 36–42 ms normal average input-to-present latency is usable, but isolated
   bursts can exceed 160 ms. Optimization must preserve low normal latency while
   preventing media bursts from delaying input and audio.

## Optimization plan

### P0 — reproducible performance gate and richer telemetry

1. Add `summarize-windows-link-logs.ps1` to produce one JSON/Markdown summary
   from a host/client log pair, including normalized rates and shutdown-tail
   handling.
2. Record machine role, network medium, adapter/link speed, build hash and test
   label in launcher metadata.
3. Add p50/p95/p99/max audio inter-arrival gap, RTT, jitter, video network time,
   queue age and input-to-present latency. Report metrics in fixed time windows
   so a single burst can be located automatically.
4. Count frames actually presented separately from completed, superseded and
   repeated frames. Log host frame cadence and sender queue age.
5. Establish a baseline matrix with at least three five-minute runs per setup:
   both Ethernet; fixed desktop host with wired/wireless client; fixed laptop
   host with wired/wireless client; then a controlled Internet run.

Gate: a test result must be machine-readable and comparable without manually
reading the full logs.

### P1 — prioritize and pace transport traffic

1. Replace immediate whole-frame UDP bursts with a bounded sender scheduler.
2. Give input and clock-sync highest priority, audio next, and video chunks the
   remaining per-tick budget.
3. Pace video chunks across the approximately 16.7 ms frame interval instead of
   emitting about 20 datagrams back-to-back.
4. Drop an unsent stale video frame rather than delaying input/audio or growing
   sender queue age.
5. Instrument queue age, scheduling delay and datagrams per burst for each
   packet class.

Initial gate: on both-Ethernet five-minute runs, zero post-start audio
underflows, zero media sequence gaps, p99 audio arrival gap below 30 ms and
maximum unsent video age below one frame. Wi-Fi target is p99 below 60 ms with
no input packet starvation.

### P2 — make adaptive PCM resilient without hiding latency

1. Separate startup prebuffering from steady-state underflow recovery so the
   client does not oscillate between trim and starvation.
2. Choose the target from recent p95/p99 arrival gaps plus explicit safety
   headroom, with hysteresis and slow decay.
3. Size physical queue capacity above the maximum adaptive target; do not simply
   raise the normal target for every connection.
4. Revisit drift-controller gain and the ±2,000 ppm clamp using recorded buffer
   trajectories.
5. Keep sequence-gap concealment bounded. For PCM, evaluate a small optional
   redundancy/FEC strategy only after scheduler pacing is measured; do not add
   retransmission latency to realtime audio.

Gate: zero post-start underflows in a ten-minute wired run and target zero in
normal Wi-Fi runs, with settled audio target at or below 100 ms and no repeated
trim/underflow cycle.

### P3 — pace client video presentation

1. Present using host frame timestamps and a small bounded latest-frame queue,
   rather than allowing multiple completed frames to be replaced before the UI
   can present one.
2. Measure completed-to-present queue age and distinguish deliberate pacing
   skips from incomplete-frame network drops.
3. Keep the newest-frame low-latency policy during genuine congestion; never
   accumulate an unbounded frame queue.
4. Test OpenGL VSync modes and the SDL fallback against the same presented-frame
   counters.

Gate: superseded/pacing-skipped frames below 0.5% on stable LAN, zero incomplete
frames on wired LAN, average input-to-present latency at or below 40 ms and no
latency growth over ten minutes.

### P4 — reduce bandwidth after pacing is correct

1. Benchmark tile-change maps and XOR/delta RLE against the lossless pixel-RLE
   reference, including forced keyframes and recovery after packet loss.
2. Reduce video datagram count as well as encoded bytes; fewer packets per frame
   directly reduce burst pressure on Wi-Fi queues.
3. Implement the planned Pixel Perfect and Low Bandwidth presets through a
   common encoder/decoder interface.
4. Evaluate conventional or hardware codecs only on end-to-end latency, CPU,
   bandwidth and recovery behavior—not encode time alone.

Gate: each preset has measured bandwidth, CPU, input-to-present latency and loss
recovery results. Balanced must improve burst behavior without regressing the
lossless reference's correctness.

### P5 — controlled impairment and Internet readiness

After P1–P4 pass physical LAN gates, add deterministic delay/jitter/loss tests,
repeat the direct-Internet run and set quality adaptation thresholds. Session
authentication/encryption and safe coordination remain mandatory before any
public-facing Internet release.

## Recommended implementation order

1. Log summarizer and percentile/window telemetry.
2. Priority sender scheduler plus video-chunk pacing.
3. Repeat the physical baseline matrix.
4. Tune adaptive PCM from the new buffer/inter-arrival evidence.
5. Add timestamp-driven client presentation pacing.
6. Benchmark lower-bandwidth lossless/delta transport.
7. Run controlled impairment and longer Internet regressions.
