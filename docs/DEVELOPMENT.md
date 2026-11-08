# Development and recovery

## Reproducible firmware build

Use ESP-IDF **5.5.5** and the ESP32-P4 toolchain. The manifest allows 5.5.x; the lockfile pins resolved packages. Begin in an activated IDF terminal:

```sh
idf.py build
idf.py -p COM5 -b 460800 flash
```

`sdkconfig.defaults` sets the tested chip/memory/CSI/ISP/SDIO configuration. Generated `sdkconfig`, builds and downloaded packages are not versioned. Review **FlowCount people counter** in `menuconfig` for another board.

`tools/idf.ps1` finds the Windows IDF environment and respects a local `.build-dir` file. In the original development workspace this avoids an old `build` junction. A clean clone can create its own `build` normally. Never move an existing build directory: generate a fresh one with `idf.py -B NEW_PATH build`.

The ISP tool splits paths containing spaces. CMake stages tuning into a space-free cache; override with `-DPC_IPA_CACHE_DIR=/path/without/spaces`. Do not modify downloaded dependencies to work around local path issues.

## Host checks

Requires Python 3.11+, Node.js 22+ and a C++17 compiler with CMake. Use Debug because the portable test executables use `assert`.

```sh
python -m pip install -r requirements-dev.txt
python -m unittest discover -s tests -p "test_*.py" -v
node tests/test_ui.js
cmake -S tests -B tools/host-build -DCMAKE_BUILD_TYPE=Debug
cmake --build tools/host-build --config Debug
ctest --test-dir tools/host-build -C Debug --output-on-failure
python tools/check_publication.py
```

Tests cover association/crossing cases, camera validation, network retry timing, detection policy, UART bridge/authentication and browser draft/trial behavior. They are regression checks, not field accuracy estimates. CI runs host checks and an ESP-IDF firmware build.

## Verify a device

```sh
python tools/check_network.py --url http://flowcount.local
python tools/check_network.py --url http://flowcount.local --scan
```

The second command briefly scans Wi-Fi. Checks authenticate with the documented defaults, exercise frame progression and image metadata, reject unauthenticated/cross-origin/unsupported requests, and issue a counter save. They do not reset totals or change venue credentials. They do not prove every person is counted.

For USB setup, use `tools/device_console.py` or `tools/monitor.py`, not both at once. UART runs at 921600 baud. `--reset` reboots the P4; save totals first when needed. UART preview leases expire automatically and are capped at 2 fps.

## Diagnostic replay

`tools/replay_detector.py` sends consented RGB565 test frames to the P4 diagnostic path, receives detector proposals and records their timestamps. Use `--help` for options. `main/diagnostic_replay.cpp` is UART-only and CRC-checks payloads; HTTP rejects replay commands. This allows the actual embedded detector to be tested without a second inference engine.

Screen recordings are degraded, resized observations of an already processed preview. Replaying them is useful for regression but does not reproduce raw sensor frames. Never turn a screen replay count into a live accuracy claim. Keep recordings/results local unless explicitly cleared for publication.

## Back up before destructive recovery

Routine updates use `flash` and preserve NVS. The following tools are for explicit recovery, not normal updates:

- `tools/backup_flash.py`: read-only complete 16 MB P4 backup with packet pacing and MD5 verification; writes a SHA-256 manifest. Uses esptool 4.x internal interfaces from the tested IDF environment.
- `tools/verify_backup.py BACKUP.bin`: validates size, manifest and SHA-256 before a destructive operation.
- `tools/flash.ps1 -Backup PATH -Port COM5`: verifies a complete backup, builds first, then **erases P4 flash and writes the application**. Erases NVS too. Use only when that destruction is intended.

Backups may contain passwords and belong outside public source. Never upload flash dumps, serial boot access output or `.private/`. The project provides no routine C6 flashing procedure.

## Export a clean source package

```sh
python tools/sync_repository.py --output ../esp32-p4-people-counter --zip
```

Selection is explicit: source, English docs, community files and approved media. `.private`, logs, captures, generated configuration, build products, manufacturer PDFs and downloaded components are excluded. The tool rejects symlinks and an exported device AP key, writes a manifest and a ZIP, and does not copy Git history. Review the manifest before publishing.

The development folder can contain local-only investigation notes that are not part of the release. Publish the selected clean export, not an indiscriminate archive of the development workspace.
