# Remote Play performance evidence and optimization plan

This document is the canonical record for physical Remote Play performance
tests and the optimization work derived from them. Functional regression
history remains in `DEVELOPMENT_PROGRESS.md`; actionable work remains mirrored
in `TODO.md`.

## Measurement rules

- Use the same checksummed build on both PCs.
- Start both roles with `start-sameboy-with-log.cmd` and exit normally so final
  counters are flushed.
- Keep active diagnostic writes on local storage. The launcher exports and
  verifies the completed directory to the shared `LOGS` root only after exit,
  so SMB latency cannot perturb the realtime measurement.
- Let the launcher keep each start in its own uniquely named Host/Client folder
  below the shared `LOGS` directory; do not move or rename individual files.
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

Summarize a completed host/client pair with:

```powershell
.\summarize-windows-link-logs.ps1 `
  -HostPath "M:\SAME-LINKTEST\LOGS\20260819-120000-HOSTPC-host-a1b2c3d4" `
  -ClientPath "M:\SAME-LINKTEST\LOGS\20260819-120005-CLIENTPC-client-e5f6a7b8" `
  -TestLabel "lan-ethernet-host-wifi-client"
```

Each path may name a complete launcher log directory or its combined
`sameboy-session.log`. The script writes schema-versioned JSON plus a readable
Markdown report below the ignored `build\regression\summaries` directory by
default. It normalizes media rates, estimates the expected disconnect tail and
groups detailed timing/loss events into ten-second windows.

Tests A-C used protocol v4, PCM S16LE stereo at 48 kHz, lossless pixel RLE,
session ID 1 and development source commit `1ac9ddab8a17`. Test D used the same
audio/video formats with protocol v5 and development commit `15e77b3e10bf`.
Both tested source trees were dirty and are evidence for development decisions,
not release qualification.

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

This remains a development proof only. Protocol v5 has no authentication or
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

## Test D — protocol v5 Wi-Fi/Wi-Fi side-scroll repetitions

Protocol v5 increased the video payload from 1,024 to 1,280 bytes while keeping
the latest-complete-frame policy and adding no host or client frame queue. Three
physical Wi-Fi/Wi-Fi repetitions used a demanding 60 FPS horizontal side-scroll
test. Normal RLE traffic fell from protocol v4's 19.60 datagrams per frame to
15.68-16.05, approximately 19% fewer datagrams.

| Client measurement | Run 1 | Run 2 | Run 3 |
|---|---:|---:|---:|
| Active duration | 132.207 s | 120.643 s | 76.062 s |
| Video superseded before presentation | 0.84% | 0.71% | 0.92% |
| Average latency | 37.855 ms | 34.487 ms | 36.670 ms |
| p95 latency | 53.298 ms | 45.673 ms | 48.060 ms |
| Maximum latency | 66.541 ms | 60.358 ms | 59.419 ms |
| Average / maximum frame receive span | 2.280 / 25.726 ms | 2.153 / 18.976 ms | 2.647 / 19.024 ms |
| Maximum audio arrival gap | 234.965 ms | 41.503 ms | 132.607 ms |
| Audio underflows | 3 | 2 | 2 |
| Host average / maximum video burst | 0.254 / 0.815 ms | 0.247 / 0.794 ms | 0.227 / 0.943 ms |

The user accepted this as the current playable Wi-Fi baseline. Run 3 also
captured a rare horizontal seam in the host P1-only window while the client
image remained intact. The cause was local presentation of slot 0's active
framebuffer while the next frame could be drawn. Local Link and Remote Host
P1-only presentation now use the immutable previous completed framebuffer;
this correction adds no network or input latency.

## Test E — schema-v3 Wi-Fi/Wi-Fi audio telemetry

The `audio-telemetry` build physically verified the bounded arrival histograms
and corrected adapter selection over approximately 111.3 active seconds. Both
machines recorded their active 1.2 Gbit/s Wi-Fi interfaces instead of a
disconnected Ethernet route, and executable hashes matched.

| Measurement | Result |
|---|---:|
| Audio arrival average / p50 / p95 / p99 | 5.170 / 1 / 18 / 20 ms |
| Normal ten-second-window maximum | 30.164-34.256 ms |
| Audio dropped / stale / decode errors / trims | 0 / 0 / 0 / 0 |
| Audio underflows | 2 |
| Video dropped / superseded | 0 / 51 (0.77%) |
| Input-to-present average / p95 / maximum | 37.701 / 52.662 / 54.106 ms |
| Host video burst average / maximum | 0.288 / 0.729 ms |

One audio underflow occurred at 90.8 seconds during a 27.420 ms arrival gap;
the adaptive target rose from 45 to 55 ms and remained stable. The other was
paired with a 3.795-second gap immediately before the network thread stopped,
so it is treated as shutdown/pause tail rather than steady-state network
behavior. The user heard no audio fault during the run. Increasing the minimum
PCM buffer solely to remove these counters would add latency without an
observed benefit, so the current parameters remain the accepted baseline.

## Test F — schema-v4 extended Wi-Fi/Wi-Fi baseline

The `timing-windows-v4` build ran for approximately 453.6 active seconds and
physically verified the new ten-second timing windows. Both machines again
recorded active 1.2 Gbit/s Wi-Fi interfaces and matching executable hashes.

| Measurement | Result |
|---|---:|
| Clock / input-active latency windows | 46 / 7 |
| RTT average / p95 / maximum | 15.340 / 18.490 / 20.594 ms |
| Jitter average / maximum | 5.139 / 6.985 ms |
| Input-to-present average / p95 / maximum | 37.890 / 51.476 / 61.886 ms |
| Video-network average / p95 / maximum | 4.578 / 12.459 / 18.347 ms |
| Video dropped / superseded | 0 / 164 (0.61%) |
| Present-call average / maximum / slow | 0.087 / 0.973 ms / 0 |
| Audio arrival average / p95 / p99 / maximum | 5.000 / 18 / 20 / 50.983 ms |
| Audio dropped / stale / decode errors / trims | 0 / 0 / 0 / 0 |
| Audio underflows | 3 |

The three underflows occurred around 102, 223 and 314 seconds as the adaptive
target decayed toward 40-50 ms and then recovered upward. They did not coincide
with packet loss. The user judged the audio acceptable for now, while noting
that it was not monitored continuously for the entire run. This is sufficient
for the current practical baseline but not evidence that every callback was
inaudible. PCM parameters remain unchanged; revisit decay/hysteresis only if a
future test exposes a repeatable audible fault.

## Evidence-based conclusions

1. Protocol validation and bounded queues are working: rejected/stale counts
   remained zero and neither host accumulated send drops.
2. Protocol v4 lossless video needed roughly 9.5-9.7 Mbit/s plus 1.536 Mbit/s
   PCM before UDP/IP overhead. Protocol v5 reduces the normal datagram count
   from roughly 20 to 16 per frame without increasing buffering.
3. A wired host remains useful for controlled baselines, but repeated
   Wi-Fi/Wi-Fi play is currently acceptable and Test E had no audible audio
   fault or media packet loss.
4. A larger fixed audio buffer alone is not the right first fix. Test B reached
   its 100 ms target and still underflowed, while Test C alternated between
   trimming bursts and later consuming the queue.
5. Protocol v4 superseded-before-present stayed essentially constant (3.25%
   versus 3.27%) across opposite network directions. Protocol v5 repetitions
   reduced this to 0.71-0.92%, although the first VSync-only experiment felt
   unchanged; that counter is evidence, not a complete smoothness measure.
6. Protocol v5 averaged 34.5-37.9 ms input-to-present latency with p95 below
   54 ms across the accepted repetitions. Counters must be correlated with the
   heard/seen result; do not add buffering to eliminate an inaudible isolated
   underflow or a shutdown-tail event.

## Test G1 — protocol-v6 Ethernet/Ethernet matrix run 1

The first clean protocol-v6 matrix run used the desktop as 1 Gbit/s Ethernet
host and the laptop as 1 Gbit/s Ethernet client. Matching executable hashes and
source commit `5197fc65905e` were recorded. Active media ran for approximately
556.8 seconds, exceeding the five-minute requirement.

| Client measurement | Result |
|---|---:|
| Video completed / dropped / rejected | 33,256 / 0 / 0 |
| Video presented / superseded | 33,221 / 35 (0.11%) |
| Audio packets received / dropped / stale | 111,361 / 0 / 0 |
| Audio arrival average / p95 / p99 / maximum | 5.014 / 18 / 19 / 188.604 ms |
| Audio underflows / trims | 7 / 18 |
| Input-to-present average / p95 / maximum | 28.739 / 36.558 / 58.416 ms |
| Present-call average / maximum / slow | 0.101 / 0.771 ms / 0 |

The run exposed a telemetry defect before the matrix could be accepted. The
client's cross-machine clock offset stayed unchanged for almost 400 seconds
because it was updated only by a new all-time-minimum RTT sample. Reported
video-network time therefore drifted from roughly 1 to 27 ms and jumped back
when a 0.075 ms lower RTT changed the offset estimate by 25.1 ms. Local
input-to-present time, receive span and packet counters were unaffected, but
the run is not a valid long-duration video-network baseline.

The clock estimator now keeps the all-time minimum only for uncertainty while
updating offset gradually from RTT samples at or below the current smoothed
RTT. This follows slow physical clock drift without accepting the most delayed
half of samples. The matrix restarts with a newly checksummed build after
automated regression and a physical confirmation that offset updates continue.

## Optimization plan

### P0 — reproducible performance gate and richer telemetry

1. Extend the implemented `summarize-windows-link-logs.ps1` JSON/Markdown
   summary as new telemetry is added. Schema v4 handles normalized rates,
   latency percentiles, audio-arrival and timing windows, event windows and the
   shutdown tail.
2. Keep the implemented launcher metadata and unique shared-log naming aligned
   with the summarizer. Adapter selection now rejects disconnected, zero-speed
   and addressless interfaces before recording the active default route.
3. Use the implemented ten-second RTT, jitter, video-network and total-latency
   windows to locate bursts automatically. Queue-age telemetry remains deferred
   because the accepted immediate sender has no sender queue to measure.
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

The accepted zero-buffer experiment raises video payload from 1,024 to 1,280
bytes. The SameBoy Link UDP payload is then 1,388 bytes, or 1,416/1,436 bytes
after UDP plus IPv4/IPv6 headers, and remains below a 1,500-byte MTU. Physical
tests reduced the normal RLE frame from about 20 to about 16 datagrams without
delaying frame completion. Host burst duration and client receive-span
telemetry provide the continuing evidence.

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

Test E did not meet a literal zero-counter Wi-Fi target, but its single
steady-state callback underflow was inaudible and did not repeat after the
adaptive target rose. Preserve current latency until an audible or repeatable
failure justifies a parameter change.

### P3 — pace client video presentation

1. Keep the existing single latest-complete-frame mailbox: do not add a host or
   client frame queue that permanently increases input latency.
2. Test adaptive OpenGL VSync first. It synchronizes on-time frames but permits
   a late frame to present immediately instead of waiting through another full
   refresh; fall back to unbuffered VSync-off presentation when unsupported.
3. Measure frames actually presented, completed frames superseded before
   presentation, presentation-call duration and calls exceeding 18 ms.
4. Use host timestamps to diagnose cadence mismatch, but keep newest-frame-wins
   behavior during congestion and never accumulate delayed frames.
5. Compare adaptive OpenGL and the SDL fallback against the same physical 60 Hz
   side-scroll test before considering any deeper scheduler change.

The physical client did not support adaptive VSync and selected the intended
`off-fallback` mode. Presentation calls averaged 0.092 ms, peaked at 0.432 ms
and never crossed 18 ms, so blocking buffer swaps were not the observed hitch's
root cause. The zero-queue policy remains the accepted baseline.

Gate: superseded frames below 0.5% on stable LAN, zero incomplete frames on
wired LAN, average input-to-present latency at or below 40 ms and no latency
growth over ten minutes. Any smoother mode must not add a permanent frame of
latency.

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
repeat the direct-Internet run and set quality adaptation thresholds. Protocol
v8 now locks a Host lifetime to its automatically paired first client. Stronger
pairing, per-datagram protection, encryption and safe coordination remain
mandatory before any public-facing Internet release.

## Recommended implementation order

1. Keep the accepted media baseline unchanged while protocol-v8 one-click pairing is physically verified.
2. Continue coordination/NAT traversal work after that focused two-PC regression.
3. Resume the remaining matrix, PCM tuning, priority scheduling or chunk pacing only if an audible, visible or repeatable measurable need appears.
4. Revisit stronger transport security before public release.

Protocol v6's handshake, peer notices and reconnect lifecycle passed physical
two-PC testing, and the first corrected Ethernet/Ethernet matrix run supplied
enough evidence to pause broad performance testing. Protocol v8 now adds
automatic first-client pairing. The baseline launcher remains available when a
repeatable fault or later security regression requires comparable measurements.
