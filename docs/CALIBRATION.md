# Calibration guide

Calibrate after the camera is installed at its final position. A good crossing band cannot compensate for missing or merged person detections.

## Camera → Gate → Test

1. Open `http://flowcount.local/` on the same network. Start preview.
2. In **Setup → Camera**, check a sharp, stable, unobstructed image. Each person needs a separate box on both sides of the planned band. Avoid overhead head-only views, furniture, railings and backlighting.
3. Fix orientation, exposure and image settings, then **Save camera**.
4. Open **Gate and detection**, select motion direction and drag the band/endpoints. The green arrow is entry. Save the gate and any confidence/filter change.
5. Open **Test** and compare known walks with your own manual counts. Try both directions, a pause inside, a turn back, crossing outside the ROI, pairs and normal crowding.

The test records a local baseline; it does not reset totals. It detects reboot/counter resets and requires a fresh trial afterward. Matching a short trial does not prove general accuracy.

## Camera controls

| Control | Purpose | Practical starting point |
| --- | --- | --- |
| Preview fps | Target JPEG output rate | 8; range 1–12. Higher target cannot exceed processing/network capacity |
| JPEG quality | Preview detail versus bandwidth | 60; range 30–85. Does not affect detector input |
| Automatic exposure | Adapts exposure and gain with changing light | Recommended |
| Maximum automatic exposure | Caps motion blur in automatic mode | Default 20 ms; try 8–12 ms for moving people with sufficient light |
| Manual exposure | Locks frame light-collection duration | Stable lighting only; shorter is darker but sharper |
| Gain | Amplifies sensor signal and noise | UI uses sensor table index; measured gain is shown in × |
| Brightness | Digital light/dark adjustment before detection | 0; cannot restore clipped detail |
| Contrast | Separates shadows/highlights | 100%; excess hides useful details |
| Saturation | Color intensity before detection | 100%; 0 is grayscale |
| Mirror / flip | Correct mounting orientation | Both for a camera mounted 180°; recheck entry direction |

Exposure/gain limits come from the actual driver. The sensor runs at 30 fps in this mode, but processing and preview are slower. Camera v2.1 has no software-controlled focus motor; white balance stays automatic.

Camera changes preview after releasing a control. **Save camera** persists them. **Undo changes** restores the saved profile; **Neutral defaults** previews defaults without saving. Orientation changes clear active tracks, preserving totals. A floating mobile thumbnail keeps feedback visible while scrolling.

## Gate and detection controls

| Control | Purpose |
| --- | --- |
| Movement direction | Vertical band for left/right; horizontal for up/down |
| Gate position | Center of the band along the travel axis, normalized 0–1 |
| Confirmation width | Full band width in UI; protocol `band` is half this width |
| Entry direction | Positive is right/down; negative is left/up |
| Gate start/end | Extent perpendicular to travel; exclude unrelated side areas |
| Track-start confidence | Minimum score to start an identity; default 0.55, range 0.30–0.95 |
| Exclude cropped close-ups | Reject boxes spanning both top and bottom; enabled by default |
| Show weak proposals | Visual diagnostic overlay; does not change counting |

At `line=0.5, band=0.06`, the band spans 0.44–0.56. Start/end default to 0.05/0.95 and must remain at least 0.10 apart. The band must fit strictly inside the image. A wider band resists small center fluctuations but needs more travel and visible space on each side.

Orange is an unsaved draft; counting still uses the saved green geometry. **Save counting gate** applies the gate. The save dock applies pending settings, with visible acknowledgement. Moving a slider while a previous request is in flight keeps the newer draft pending.

Confidence starts identities, not a percentage accuracy guarantee. Established tracks can recover compatible weak detections down to 0.30. Start at 0.55; try 0.50 only after inspecting boxes. Lower values can track walls or furniture. Raising confidence too far misses real people.

The body filter is geometric, not pose estimation. It allows a single touched edge. Whole-image boxes touching all four edges within 1.5% are always excluded, even with the optional filter off. Confirm framing and false positives whenever changing filtering.

## Reading the live view

- Solid warm boxes: tracked person, temporary ID and score. “Recovered” means weak-detection continuation.
- Dotted yellow boxes: low-confidence proposals without a confirmed identity.
- Gray boxes: excluded framing/close-up proposals.
- Green band/arrow: saved counting zone and entry direction. Orange: local edits.
- Detection fps: application processing throughput. Received fps: your browser's actual distinct frames. Requested fps: a target.

**Freeze view** only freezes your screen. **Pause stream** stops new JPEG generation. Counting continues in both cases. Image-health warnings are hints, not lens-state certification or automatic counting shutdown.

## Diagnose a missed crossing

| Observation | Next check |
| --- | --- |
| No box on a person | Lighting, blur, body size, occlusion and confidence |
| Two people in one box | Change placement/separation; tracking cannot invent separate detections |
| Box/ID disappears within the band | Keep body visible; inspect time gaps and occlusion |
| Entry counted as exit | Reverse entry direction and save |
| Box seen but no event | Complete both sides, stay inside endpoints, allow two observations per side |
| False counts with no person | Check background/railings, raise confidence if appropriate, inspect weak proposals |
| Image dark/flat | Uncover lens, verify ribbon and lighting before counting tests |
| Camera moved | Fix it, recalibrate gate, start a new trial |

Balance is not automatically occupancy. Save totals before cutting power; only saved totals survive reboot. Keep trial notes with manual counts and conditions. See [validation](VALIDATION.md) for a repeatable field protocol.
