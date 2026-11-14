# Changes in vscpl2drv-udp

Every change that goes into a release is listed here, newest release first.
Add each change under **Unreleased** when it is made. When a release is
tagged, rename **Unreleased** to the version and date, and start a new
**Unreleased** section above it.

## Unreleased

- Man pages: vscpl2drv-udp(1) now lists every configuration setting and has an example; new vscp-udp-peer(1). The man pages are built with pandoc in CMake; the old scripts, generated pages and the copied vscpl2drv-socketcan page are removed
- Add the optional `reuse-address` setting so the server socket can share its port with vscpsim and other VSCP programs on the same host
- Fix the CI checksum step: readable SHA256SUMS files, bash 3.2 on macOS, and jobs that run in a subfolder
- Add scripts/download-artifacts.sh to download and verify the newest CI build artifacts
- CI writes a SHA256SUMS-<artifact> checksum file into every uploaded build artifact
- Removed unised folder (51af583)
- Edit (e49a0ed)
- Avoid macOS test subprocess deadlock (23576c1)
- Prevent test sockets leaking into subprocesses (bf74d0e)
- Add checkpoints around macOS loopback reads (bacd653)
- Fix Windows invalid syslog level test expectation (155cf51)
- Validate syslog level on Windows (afa1b32)
- Fix Windows syslog expectations in loopback tests (6c46453)
- Hide C helper symbols in driver (eeb6555)
- Hide driver implementation symbols (7308aee)
- Fix Windows driver symbol block (cf36eb5)
- Log driver receiver packet warnings (c1eb641)
- Add loopback timeout diagnostics (6eed2a9)
- Avoid blocking UDP open on worker startup (1f05c07)
- Signal receiver startup before polling (029ddfd)
- Wait for UDP receiver worker startup (ab93648)
- Added some test configurations (ad2049d)
- Expose subprocess receiver diagnostics in loopback failures (aefd0ff)
- Initial plan (5a450d7)
- Protected keys in memory (5ead49d)
- Remove Windows-invalid stray manual (3694b47)
- Remove stray manual with Windows-invalid trailing-dot filename (e6252ee)
- Initial plan (d846098)
- Initial plan (c4cd313)
- CI's added (eb414ba)
- Initial code (0091904)
- Start of repo (892e011)
- Added submodule (4e150b7)
- Transfered from min repository (debe7f7)
- Initial commit (519414b)

No release has been tagged yet.
