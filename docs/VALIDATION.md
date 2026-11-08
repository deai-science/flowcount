# Validation, measurements and limitations

FlowCount is a prototype. Tests demonstrate specific behavior under stated conditions; they do not establish a universal accuracy rate.

## What was measured

Latest recorded bench measurements before public packaging:

| Quantity | Result | Scope |
| --- | --- | --- |
| Sensor output mode | 30 fps | IMX219 1640 × 1232 RAW10 configuration |
| Application processing | 9.93 fps | On-P4 state telemetry, tested scene/settings |
| JPEG generation | 7.71 fps | On-P4 preview generation telemetry |
| USB received frames | 11 valid JPEGs in about 6 seconds (1.78 fps) | Distinct images with CRC checks; 2 fps USB cap |
| Embedded web response | HTTP 200, 172511-byte pre-translation UI | Native client resolved `flowcount.local` through the recovery AP |

The 8 fps default (1–12 adjustable) is a requested preview target. These measurements are not a promise for every scene, client or Wi-Fi link. Browser-received fps must be measured in that browser; generated JPEG fps is not a substitute.

The prior branded UI was visually tested at mobile/tablet/desktop sizes using a local API fixture. Native HTTP/domain checks against the device passed; an automated Chromium check during temporary Wi-Fi switching did not complete. Public documentation screenshots are made from the actual English UI with explicitly illustrative data.

## Counting regressions and field evidence

The English publication build passed locally with ESP-IDF 5.5.5. All four
portable C++ suites passed, including **39 counting scenarios**, plus **16 Python
tests** and the JavaScript UI regression checks. The actual English UI was
reviewed at 390 × 844 and 1366 × 950 CSS viewport sizes using a localhost-only
synthetic fixture. Navigation, unsaved gate feedback and a saved-gate
acknowledgement were checked. These UI fixture checks do not exercise a physical
camera or establish counting accuracy.

- An earlier manual two-out-and-back trial matched 2 entries and 2 exits. Later stationary-scene false events and missed passages in other framings showed why that was insufficient acceptance evidence.
- A November 3 screen-recording replay used 222 extracted frames, inferred on the P4 and replayed through native C++ counting. The older saved 0.75 threshold produced 0/0; the adjusted 0.55 configuration produced the 2 entries / 1 exit visible in that specific sequence, without extra events in its empty sections. This is a screen-recording regression, not a fresh entrance trial.
- A November 5 crowd screen replay compared the same degraded input and threshold: old tracking produced 3 crossings, revised tracking 6 at 0.55. At 0.50 it produced 7; 0.45 reached 8 but also tracked background structures. The live screen's accumulated total was different. Do not equate a replay result with all actual people in the original scene.
- November 8 obstructed/moving-camera clips held totals at 1/2. Twelve sampled frames yielded two high-confidence near-whole-image false proposals. The always-on four-edge geometric filter now excludes that pattern. These clips do not provide a valid full-crossing accuracy trial.

Private footage and logs are deliberately excluded. The portable test suite includes synthetic regression cases for the supported tracking behavior. A maintainer can reproduce those cases without identifiable recordings; reproducing the private screen replays requires the original consented data.

## Remaining limits

- The detector can merge adjacent/occluding people into one box. The tracker cannot recover separate people from a permanently merged detection.
- Pedestrian bodies must be sufficiently visible and large. An overhead head-only installation is outside the current model's intended use.
- Blur, darkness, backlighting, railings and furniture can cause false or missing boxes. High model confidence does not prove a real person.
- Very fast crossings, long occlusion, ID switches and geometry changes can miss or duplicate counts despite bounded recovery.
- One camera/gate does not measure occupancy across unmonitored entrances. Balance can be negative.
- Abrupt power loss can discard unsaved crossings from the last five-minute save interval. Events are RAM-only.
- Fixed credentials/HTTP, one default hostname, no OTA, no durable event queue and no MQTT remain prototype constraints.

## Repeatable field protocol

1. Record firmware commit, board/camera, fixed placement, lighting, gate, confidence and body-filter settings. Never include passwords.
2. Verify separate detector boxes on both sides. Save configuration and start the UI crossing trial.
3. Manually label at least 20 entries and 20 exits, including ordinary speed, pauses, reversals and pairs. Avoid unlabelled crossings between runs.
4. Test an empty scene for a defined interval and record false events. Repeat under representative changes in light and crowding.
5. Compare entries and exits separately. Report misses, extras and wrong-direction counts against labelled crossings; include the denominator and duration. Do not report only the final balance, which can hide errors.
6. Measure application, generated JPEG and actual received browser fps separately. Check router reconnection and a controlled reboot after saving totals.
7. Repeat after moving the camera or changing calibration. Report conditions where the system fails as well as those where it matches.

There is no final independently labelled acceptance dataset or certified production accuracy in this source release.
