# macOS Specific Issues
## Attempting to build the Cocoa frontend fails with NSInternalInconsistencyException

When building on macOS, the build system will make a native Cocoa app by default. In this case, the build system uses the Xcode `ibtool` command to build user interface files. If this command fails, you can fix this issue by starting Xcode and letting it install components. After this is done, you should be able to close Xcode and build successfully.

## Attempting to build the SDL frontend on macOS fails on linking

SameBoy on macOS expects you to have SDL2 installed via Brew, and not as a framework. Older versions expected it to be installed as a framework, but this is no longer the case.

# Windows Build Process

## Quick build

From PowerShell in the repository root:

```powershell
.\build-windows.ps1
```

The script locates the installed build tools, configures the Visual Studio x64
developer environment, and builds the SDL frontend in debug mode. The result is
written to `build\bin\SDL\sameboy.exe`.

Useful options:

```powershell
# Remove previous build output before building
.\build-windows.ps1 -Clean

# Make an optimized release build
.\build-windows.ps1 -Configuration release

# Build all Windows-compatible targets
.\build-windows.ps1 -Target all

# Use SDL2 from a non-default location
.\build-windows.ps1 -SdlRoot D:\Libraries\SDL2

# Use an Opus installation from a non-default location
.\build-windows.ps1 -OpusRoot D:\Libraries\msys64\mingw64
```

Build a PCM-only executable without compiling or linking Opus support:

```powershell
.\build-windows.ps1 -DisableOpus -Clean
```

This build does not require or deploy `libopus-0.dll`. The remote host uses PCM
by default; `--remote-audio opus` is unavailable in this executable.

## Compact portable Windows release

Create a clean, optimized and shareable ZIP with one command:

```powershell
.\build-portable-windows.ps1
```

The archive is written beside the script by default and its filename is ignored
by Git. This keeps previous archives outside the `build` directory removed by a
clean build. To publish directly to the test share with a recognizable name:

```powershell
.\build-portable-windows.ps1 `
  -DestinationRoot "M:\SAME-LINKTEST\portable" `
  -Label "protocol-v8"
```

Only the ZIP needs to be shared. The recipient extracts its `SameBoyLink`
folder and starts `sameboy.exe`; no installation is required. The script always
performs a clean `release` build, strips debug information through the normal
Makefile release path, disables experimental Opus and verifies the finished ZIP.
PCM Remote Play remains available.

The archive contains the executable, SDL2, boot ROMs, normal shaders, palettes,
menu background and license. It deliberately excludes `prefs.bin`, debugger
symbols/text, Opus, manifests, diagnostic launchers and development scripts.
Preferences and saves are created only by the recipient while using the app.

`SAMEBOY_SDL2_ROOT` and `SAMEBOY_OPUS_ROOT` may be used instead of the matching
parameters. The defaults are `C:\SDL2` and `C:\msys64\mingw64`.

## Automated SameBoy Link smoke test

Pass a link-capable Game Boy or Game Boy Color ROM to the Windows regression
harness:

```powershell
.\test-windows-link.ps1 -RomPath "C:\path\to\link-game.gb"
```

The script builds the debug SDL frontend, then verifies ordinary single-player,
Local Link, Remote Host/Client OpenGL loopback and the Remote Client `--nogl`
SDL fallback. It waits for measured/synchronized frames, at least 120 decoded
remote video frames, at least 600 PCM packets, zero video drops/rejections,
zero audio drops/stale packets/decode errors and successful host input, video
and audio activation.

The supplied ROM is copied into the ignored `build\regression` area for the
test, so its original battery save cannot be modified. The temporary ROM copy
is removed afterward; logs and isolated test saves remain below the printed
run directory for diagnosis. Every process is tracked by PID and closed during
success or failure.

Useful options:

```powershell
# Reuse an already-built debug executable
.\test-windows-link.ps1 -RomPath "C:\path\to\link-game.gb" -SkipBuild

# Display test windows instead of starting them minimized
.\test-windows-link.ps1 -RomPath "C:\path\to\link-game.gb" -ShowWindows

# Avoid locally occupied UDP ports or allow more time on a slower machine
.\test-windows-link.ps1 -RomPath "C:\path\to\link-game.gb" -BasePort 46000 -TimeoutSeconds 30
```

## Shared Windows test builds

Publish a complete, versioned SDL runtime to a network disk with:

```powershell
.\publish-windows-build.ps1 -DestinationRoot "M:\SAME-LINKTEST"
```

The publisher runs the debug build unless `-SkipBuild` is supplied, copies the
executable, DLLs, shaders, palettes and boot resources into a new
`builds\<timestamp>-<commit>` directory, excludes `prefs.bin`, writes a JSON
manifest and verifies every copied file with SHA-256. Never replace a published
directory in place while either machine is using it.

If the SMB share grants Windows execute access, both computers can launch the
same `sameboy.exe` directly from that versioned directory. Otherwise run the
included `run-shared-windows-build.ps1`; it verifies the same manifest, copies
the immutable build to a per-user Local AppData cache and starts it locally.
ROMs and battery saves should not be stored in the shared runtime directory.
The diagnostic launcher stores logs in the separate shared `LOGS` directory.

The current development NAS is available as `M:` on the build PC and the test
root is `M:\SAME-LINKTEST`, or `\\KONAS\sdc\SAME-LINKTEST` over UNC.
OpenMediaVault's `sdc` SMB share is
configured to preserve execute bits for newly published files. Older build
directories created before that setting must not be used for direct execution.

## Prerequisites

The script checks these prerequisites and reports a specific missing tool or
file before starting the build:

- Visual Studio 2022 C++ Build Tools and a Windows SDK;
- LLVM/Clang with LLD;
- GNU Make and the standard MSYS command-line tools;
- RGBDS (`rgbasm`, `rgblink`, `rgbfix`, and `rgbgfx`);
- the SDL2 Visual C++ development package;
- MSYS2's 64-bit Opus package (`pacman -S mingw-w64-x86_64-opus`).

## Manual setup

### SDL2

For [libSDL2](https://libsdl.org/download-2.0.php), download the Visual C++ Development Library pack. Place the extracted files within a known folder for later. Both the `\x64\` and `\include\` paths will be needed.  

The following examples will be referenced later: 

- `C:\SDL2\lib\x64\*`
- `C:\SDL2\include\*`

### RGBDS

After downloading [rgbds](https://github.com/gbdev/rgbds/releases/), ensure that it is added to the `%PATH%`. This may be done by adding it to the user's or SYSTEM's Environment Variables, or may be added to the command line at compilation time via `set path=%path%;C:\path\to\rgbds`.  

### MSYS and Make

Ensure that the `Git\usr\bin` directory is included in `%PATH%`. Like rgbds above, this may instead be manually included on the command line before installation: `set path=%path%;C:\path\to\Git\usr\bin`. Similarly, make sure that the directory containing `make.exe` is also included.

## Manual build

Within a command prompt in the project directory:

```
vcvars64
set lib=%lib%;C:\SDL2\lib\x64
set include=%include%;C:\SDL2\include
make
```
On some versions of Visual Studio, you might need to use `vcvarsx86_amd64` instead of `vcvars64`. Please note that these directories (`C:\SDL2\*`) are the examples given within the "SDL Port" section above. Ensure that your `%PATH%` properly includes `rgbds` and `Git\usr\bin`, and that the `lib` and `include` paths include the appropriate SDL2 directories.

## SameBoy Link runtime diagnostics

The SDL frontend writes a small SameBoy Link diagnostic block to standard error
when a ROM session starts. It records the renderer and native framebuffer format,
the selected audio driver and sample rate, and both nominal and measured frame
cadence. The cadence measurement uses the first 120 normal frames and is emitted
once per ROM session.

To capture the diagnostics from PowerShell:

```powershell
$process = Start-Process `
    -FilePath .\build\bin\SDL\sameboy.exe `
    -ArgumentList 'C:\path\to\game.gb' `
    -RedirectStandardError .\build\sameboy-link.log `
    -PassThru
$process.WaitForExit()
```

Each line is prefixed with a category such as `[SameBoy Link][video]`,
`[SameBoy Link][audio]`, or `[SameBoy Link][timing]`, so later link and latency
diagnostics can use the same log stream without changing the emulator core.

## Local Link

Open a ROM in the normal SDL frontend and choose `Link > Local Link...` to start
two cores with the same ROM and show their native framebuffers side by side.
Player 1 and Player 2 keyboard/controller mappings are configured independently
under Control Options, and both profiles persist between runs.

The command-line entry point remains available for development and regression
testing:

```powershell
.\build\bin\SDL\sameboy.exe --local-link "C:\path\to\link-game.gb"
```

The default keyboard mappings use arrow keys, X, Z, Enter and Backspace for
Player 1 and W/A/S/D, K, J, I and U for Player 2. Player 2's battery save is
written beside the ROM with the suffix `.p2.sav`; it never shares Player 1's
`.sav` path. Player 2 audio is mixed locally, and `Link > Disconnect` releases
the second core and returns to ordinary single-player.

The game compatibility matrix is still incomplete, so record the ROM revision,
model and observed link behavior when reporting a Local Link problem.

## LAN remote-input developer mode

The normal LAN UI avoids all manual connection settings. P1 opens a ROM and
chooses `Link > Host Remote Link…`; SameBoy detects its LAN address, creates a
fresh internal session key and copies an endpoint/session invite. P2 copies that
invite and chooses `Link > Join Remote Link…`. Use `Remote Settings…`
only to override the advertised address for public-IP forwarding/VPNs or to
reach the technical port, Session ID and manual join-address controls.

For CLI automation or advanced diagnostics, `--remote-auto-pair` exercises the
same first-client flow. This also enables the two-core Local Link mode:

```powershell
.\build\bin\SDL\sameboy.exe --remote-input-host 45900 --remote-session 424242 --remote-auto-pair "C:\path\to\link-game.gb"
```

The host shows both native screens by default. To start with only P1 visible on
the host while P2 continues to run and stream normally, add
`--remote-host-view p1`. Use `--remote-host-view both` for the explicit
side-by-side mode. Press `F9` during netplay to switch the host view immediately;
this does not pause, hide or disconnect P2 on the remote computer.

On the Player 2 PC, replace the address with the host's LAN IPv4 address:

```powershell
.\build\bin\SDL\sameboy.exe --remote-input-client 192.168.1.10:45900 --remote-session 424242
```

For a one-PC loopback test, use `127.0.0.1:45900`. Focus the remote P2 window
and use the same keyboard layout as P1: arrow keys, X, Z, Enter and Backspace.
The dedicated Local Link P2 layout (W/A/S/D, K, J, I and U) remains available
as an alternative. Local Link on one keyboard still keeps the two layouts
separate. The remote window is freely resizable and preserves the native screen
aspect ratio with centered letterboxing or pillarboxing. Under OpenGL, Escape
opens Video Options where Scaling Filter cycles through the same shader/filter
set as ordinary SameBoy. `--nogl` forces the nearest/bilinear SDL renderer
fallback for compatibility and regression testing.

Protocol v8 automatically sends a fresh internal key to the first client and
then rejects clients without it for the rest of that Host lifetime. The initial
transfer and realtime datagrams are not encrypted. This direct-IP mode therefore
remains development-only. The client displays the
host's native P2 framebuffer and plays its P2 audio automatically. The current
media path uses lossless pixel RLE by default and falls back to raw RGBA8 for any
frame that would grow. Tetris DX measured roughly 14–22% of the raw video size
(about 6–10 Mbit/s rather than 44 Mbit/s). Remote audio defaults to the
uncompressed 48 kHz stereo 16-bit PCM reference (approximately 1.54 Mbit/s), or
the host can select Opus Restricted Low Delay:

```powershell
.\build\bin\SDL\sameboy.exe --remote-input-host 45900 --remote-session 424242 --remote-auto-pair --remote-audio opus "C:\path\to\link-game.gb"
```

Both modes use 5 ms packets and the same adaptive jitter buffer. The client title
shows smoothed RTT, clock-synchronization uncertainty, latest input-to-present
latency, jitter, dropped video frames and queued audio milliseconds. Detailed
stage timings are written under `[SameBoy Link][timing]`.

## Temporary direct Internet test

The current direct-IP protocol can be tested manually over the Internet, but it
is not a public-release transport. Protocol v8 pairs and locks to the first
client automatically, but an active observer could capture the plaintext key.
Realtime datagrams are not encrypted, and P2 video/audio is not confidential.
Use a fresh session, open only one test port,
close the host afterward and remove the router forwarding rule immediately.

On the host router, forward one UDP port to the host PC's private IPv4 address
using the same internal and external port. Then start the host, for example:

```powershell
.\build\bin\SDL\sameboy.exe --remote-input-host 45930 --remote-session 123456789 --remote-auto-pair --remote-host-view p1 "C:\path\to\link-game.gb"
```

The client connects to the router's public IPv4 address and forwarded port:

```powershell
.\sameboy.exe --remote-input-client PUBLIC_IP:45930 --remote-session 123456789
```

For a menu-driven test with a persistent diagnostic log, double-click
`start-sameboy-with-log.cmd` beside `sameboy.exe`. Start Host or Join from the
Link menu as usual, then exit SameBoy normally when the test is finished. The
launcher automatically finds the enclosing `SAME-LINKTEST` directory and
exports every completed launch below `M:\SAME-LINKTEST\LOGS`. While SameBoy is
running, all stdout/stderr and client diagnostics stay on the local disk below
`%LOCALAPPDATA%\SameBoy Link\LogStaging`; no realtime log write goes to the NAS.
After SameBoy exits, the complete folder is copied to a hidden staging name on
the share, SHA-256 verified file by file and atomically renamed to its final
unique name. The folder name records timestamp, computer, role and launch ID,
for example
`20260819-120000-HOSTPC-host-a1b2c3d4`. Host, Client, Local Link and an unused
menu launch are classified as `host`, `client`, `local` and `idle`.

Mapped-drive and UNC launches are both supported. When Join relaunches SameBoy
as the dedicated Remote Client process, the logging launcher detects that child
PID and remains open until the client exits. It does not rename or combine the
folder while the client is still writing diagnostics. If the NAS export fails,
the launcher prints the retained local log path and returns an error instead of
discarding the capture.

`session-info.txt` records the detected role, active network interface,
Wi-Fi/Ethernet medium, link speed, local IPv4, build hash and an optional test
label. To add a label when starting from a terminal, use:

```powershell
.\start-sameboy-with-log.cmd -TestLabel "both-ethernet-run1"
```

Set `SAMEBOY_LINK_LOG_ROOT` or pass `-LogRoot` to override the destination. If
the launcher is not below `SAME-LINKTEST` and `M:\SAME-LINKTEST` is unavailable,
it falls back to `Documents\SameBoy-Link-Logs`. The final combined file path is
printed when SameBoy closes.

To compare a completed Host and Remote Client capture automatically, keep their
log directories separate and run this command from the source tree:

```powershell
.\summarize-windows-link-logs.ps1 `
  -HostPath "M:\SAME-LINKTEST\LOGS\20260819-120000-HOSTPC-host-a1b2c3d4" `
  -ClientPath "M:\SAME-LINKTEST\LOGS\20260819-120005-CLIENTPC-client-e5f6a7b8" `
  -TestLabel "lan-ethernet-host-wifi-client"
```

The paths may instead point directly to each `sameboy-session.log`. By default,
the script creates a timestamped JSON and Markdown pair under
`build\regression\summaries`. Schema v4 checks build hashes, accounts for the
normal shutdown tail, computes normalized loss/underflow rates and extracts
latency percentiles, bounded audio-arrival percentiles, ten-second audio windows,
ten-second RTT/jitter/video/total-latency windows and timed event windows. Audio
telemetry is aggregated in memory so logging does not write once per packet
during play. Queue age is omitted until a sender queue actually exists.

Run the client from a genuinely different Internet connection. A client on the
same LAN may fail when using the public address if the router lacks NAT loopback;
that does not prove the forwarding is broken. If the router's reported WAN
address differs from the public address, the connection may be behind CGNAT and
manual forwarding will not work without a VPN, public IPv6, traversal or relay.

The first manual public-IPv4 test completed roughly five minutes of play with
18,590 video frames, 62,188 PCM packets and zero host-side send drops. This is a
development proof only; it does not change the authentication/encryption warning
or make permanent port forwarding safe.

Later instrumented physical runs include a longer direct-IPv4 host capture and
opposite-direction LAN tests with Wi-Fi on the Host and Client respectively.
Use `REMOTE_PLAY_PERFORMANCE_PLAN.md` for the exact counters, interpretation,
test limitations and the current optimization gates. The logging launcher now
keeps Host and Client launches separate automatically in the shared `LOGS`
directory, so no post-test file move or rename is needed.
