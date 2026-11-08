# Changelog

## Prototype 1.0 — 2026-11-08

First public source release of FlowCount by DeAI Labs.

- Licensing: PolyForm Noncommercial 1.0.0 for permitted community uses, with a separate paid commercial route; source-available rather than OSI open source. This replaces the unpublished local MIT draft.

- P4 / IMX219 capture, on-device detection and bidirectional gate counting.
- Hungarian association, weak-detection continuation, constrained occlusion recovery and geometric close-up filtering.
- Persistent totals and calibration, separate camera previews and saves.
- Mobile-first UI, `flowcount.local`, guided setup and crossing comparisons.
- C6 AP+STA Wi-Fi, scanning, bounded retries and connection telemetry.
- HTTP/UART diagnostics, USB preview and portable regression tests.
- English source/UI/docs, prototype photographs, editable diagrams and contribution guidance.

Known limits: merged boxes in crowds, placement/lighting sensitivity, fixed web credentials, unencrypted HTTP, no OTA/MQTT and no enclosure CAD. See `docs/VALIDATION.md` and `SECURITY.md`.
