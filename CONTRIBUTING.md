# Contributing to FlowCount

Thank you for helping make embedded vision easier to understand and reproduce.
Read the [architecture](docs/ARCHITECTURE.md) and [development guide](docs/DEVELOPMENT.md) before changing behavior.

## Report a reproducible problem

Use the issue templates. Include board/chip revision, camera, ESP-IDF and commit versions, calibration, expected versus observed counts, and reproduction steps. Distinguish a missing detector box from a tracking error. Describe placement and lighting. Report processing, generated JPEG and received browser fps separately.

Never upload passwords, `.private/`, serial `@NETWORK` output, flash dumps or identifiable footage without permission. Prefer synthetic normalized-box sequences for counter bugs. Share a manually labelled clip only with the consent of everyone recorded.

## Development workflow

1. Fork the repository and create a focused branch.
2. Keep source, comments, UI messages and documentation in English.
3. Preserve HTTP/UART fields and NVS formats, or document and test a migration.
4. Add a regression case for tracking/counting changes. Test false positives as well as recovered crossings.
5. Run the C++, Python and JavaScript checks in the README. Build firmware for hardware-facing changes.
6. Update documentation and `CHANGELOG.md`. Explain the trigger, resulting behavior and validation in your pull request.

Create a new ESP-IDF build when changing paths; do not move an existing build directory. Flash the P4 preserving NVS for normal updates. Do not reflash the C6 to investigate an ordinary UI or tracking issue.

Avoid unrelated formatting and dependency upgrades in behavior fixes. Do not add cloud dependencies to the counting path. Explain memory and latency costs, and test realistic frame intervals. A tracker cannot separate a person the detector never detects individually; document that boundary.

FlowCount is source-available under PolyForm Noncommercial 1.0.0 with a separate commercial route. Discuss licensing with the maintainers before submitting code. Authors retain their rights; any permission needed for commercial relicensing must be agreed explicitly before merge. A DCO sign-off alone does not grant that permission. Existing third-party files retain their own licenses. Preserve upstream attribution. AI-assisted contributions are welcome: review generated code, identify generated media and supply reproducible validation. See [the licensing guide](docs/LICENSING.md).

## Maintainer source export

In the original development workspace, update the clean sibling export and ZIP after changes:

```sh
python tools/sync_repository.py --output ../esp32-p4-people-counter --zip
```

The exporter selects public files explicitly. Only named prototype media in `docs/media/` are approved for publication. Local recordings, captures, credentials, manufacturer PDFs, dependencies and builds are excluded. Review the manifest before making a public commit.
