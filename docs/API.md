# HTTP API and UART reference

Base URL: `http://flowcount.local` or the IP shown in Connection. The P4 serves UI and API together.

## Authentication and errors

Application routes require **HTTP Basic, admin/admin**, including the USB monitor. Missing/invalid credentials return 401. WPA2 and venue Wi-Fi passwords are separate. HTTP is unencrypted and intended for a trusted LAN.

POST bodies use `Content-Type: application/json`, 1–512 bytes. A supplied `Origin` must match `http://` plus the request `Host`; other-origin CORS is not enabled. Functional failures may return **HTTP 200 with `ok:false`**: inspect JSON as well as status.

| Method / path | Result |
| --- | --- |
| `GET /` | Embedded HTML UI |
| `GET /state` | Current state, image state, recent events and network |
| `GET /image.jpg` | Latest JPEG with `X-Frame-State` |
| `POST /command` | Execute an allowed counter/camera command |
| `GET /network` | Network status, no passwords |
| `POST /network` | Save/change, forget or reconnect venue Wi-Fi |
| `POST /scan` | Start asynchronous scan with `{}` |
| `GET /networks` | Scan progress/results |

Captive-portal probe routes may redirect to `http://192.168.4.1/`. Unknown routes return 404. `/health` belongs to the Python monitor, not the embedded API.

## State

`GET /state` returns:

| Field | Meaning |
| --- | --- |
| `state` | Latest processed-frame object |
| `image_state` | Metadata bound to latest JPEG; may be older than `state` |
| `connected` | Some state has been published since boot; also check freshness |
| `age_s` | Seconds since state publication |
| `events` | Up to 25 RAM-only recent events: `ms`, `id`, `direction` (`entry`/`exit`) |
| `network` | Current connection state |
| `logs` | Empty on device; USB monitor collects console messages |

Important `state` fields:

- `frame`, `ms` (uptime milliseconds, not wall-clock date), `boot_id`, `reset_reason`.
- `entries`, `exits`, `balance`, `dirty`, `preview`, `score`, `full_body_only`.
- `processing_fps`, `preview_fps` (generated JPEGs), `capture_ms`, `inference_ms`, `jpeg_ms`.
- `gate`: `vertical`, `line`, `band` (half width), `positive_entry`, `roi_min`, `roi_max`, all geometry normalized 0–1.
- `tracks`: temporary `id`, normalized `box:[x1,y1,x2,y2]`, `score`, `recovered`, `side` (−1/0/+1), `pending_hits`.
- `ignored_detections`: excluded normalized boxes without track IDs.
- `candidates`: `box`, `score`, `reason` (`low_score`, `closeup`, `tracked`); `best_score` is maximum proposal score or zero.
- `camera`: controls, `dirty`, capabilities/limits and measured exposure/gain.

`tracked` in a candidate means it passed the start threshold; only `tracks` establishes actual identity. Weak proposals from 0.30 may continue existing tracks, never create new ones. `full_body_only` is a compatibility name for geometric top/bottom cropping, not pose validation. Whole-image boxes touching all four edges within 1.5% are excluded even when that option is false.

Camera fields include `fps`, `quality`, `automatic`, `max_exposure_us`, `exposure_us`, `gain`, `brightness`, `contrast`, `saturation`, `mirror`, `flip`, `width`, `height`, `sensor_fps`, `actual_exposure_us`, `actual_gain`, `exposure_supported`, `gain_supported`, `exposure_min_us`, `exposure_max_us`, `gain_max`. The `gain` control is a sensor-table index; `actual_gain` is a signal multiple.

Image-health fields: `mean_luma`, `luma_stddev`, `dark_percent`, `bright_percent`, `image_health` (`ok`,`dark`,`flat`,`bright`). They are advisory RGB565 measurements, not proof of a covered lens. Reset reasons include `power_on`,`software`,`panic`,`watchdog`,`brownout`,`other`. Use `boot_id` to avoid mixing trial baselines across reboots.

## Images

`GET /image.jpg` returns `image/jpeg`, `Cache-Control: no-store` and exact image-frame JSON in `X-Frame-State`. It returns 503 if no JPEG is available yet. The Python bridge uses 404 in that case.

`GET /image.jpg?after=N` returns **204 with no body** if N equals the current JPEG ID, otherwise the image. It does not wait for a new frame. Preserve your previous image on 204. Pausing preview stops new generation but may leave the previous JPEG available. Check frame ID, not just HTTP success.

## Counter and camera commands

Request: `POST /command` with `{"command":"save"}`. Success: `{"ok":true,"command":"save"}`.

| Command | Effect |
| --- | --- |
| `save` | Persist totals and counter settings |
| `reset` | Clear totals and tracks, then save |
| `preview 0` / `preview 1` | Disable/enable JPEG generation; counting continues |
| `score 0.55` | Set start confidence, clear tracks, save |
| `gate h 0.50 0.06 1 0.05 0.95` | Horizontal band, clear tracks, save |
| `gate v 0.50 0.06 1 0.05 0.95` | Vertical band, clear tracks, save |
| `body 1` / `body 0` | Enable/disable optional cropped close-up exclusion, clear tracks, save |
| `status` | Publish state/acknowledge on device; not in Python HTTP command allowlist |
| `camera stream 8 60` | JPEG target fps and quality |
| `camera auto 20000` | Automatic gain/exposure with ceiling in microseconds |
| `camera manual 8000 9` | Manual microseconds and gain-table index |
| `camera image 0 100 100 0 0` | Brightness, contrast %, saturation %, mirror, flip |
| `camera save` | Persist camera settings |
| `camera restore` | Restore saved camera profile |
| `camera defaults` | Preview initial profile without saving |

Camera previews remain in RAM until saved; counter saves do not replace `camera save`. Orientation changes clear active tracks and preserve totals.

```text
gate <h|v> <position> <half_width> <positive_is_entry:0|1> <lateral_start> <lateral_end>
```

Positive is down for `h`, right for `v`. Commands use decimal points and no newlines. Limits: score 0.30–0.95; half width 0.01–0.25; position strictly inside half-width margins; lateral bounds 0–1, separated by at least 0.10. Camera: fps 1–12, quality 30–85, brightness −64..64, contrast 50–150, saturation 0–150, booleans 0/1. Exposure/gain also validate actual driver limits. The UI confirms reset; API clients must add confirmation themselves if needed.

UART-only maintenance: `apkey NEW_KEY` saves an 8–32-character WPA2 key, with reboot required to apply. ACK does not echo it. It does not change web/router credentials and is rejected over HTTP. Diagnostic replay is also rejected over HTTP.

## Network

`GET /network` includes `ap_ssid`, `ap_ip`, `hostname` without `.local`, `sta_ssid`, `sta_ip`, `sta_connected`, `ready`, `disconnect_reason`, `phase`, `message`, `retry_in_s`, `attempts`, `startup_error`, `rssi`, `connected_for_s`, `mdns_ready`, `scanning`.

Phases: `starting`, `error`, `setup`, `connecting`, `retrying`, `connected`. RSSI is measured in dBm, not a throughput guarantee. No AP or router password is returned.

Save credentials with `POST /network`:

```json
{"ssid":"YOUR_2G4_NETWORK","password":"YOUR_NETWORK_PASSWORD"}
```

SSID length is 1–32 bytes; password 8–63 bytes or empty for an open network. Names must match exactly. Success acknowledges stored credentials and a requested switch, **not a completed connection**. Poll `sta_connected` and `sta_ip`.

`{"forget":true}` forgets venue Wi-Fi but preserves recovery AP. `{"reconnect":true}` reconnects saved credentials without resending them. Either can interrupt the client connection.

`POST /scan` with `{}` starts scanning. Poll `/networks` until `scanning:false`; response includes `networks:[{ssid,rssi,channel,open}]` and `error` (empty on success). Scanning can briefly slow preview and does not change saved credentials.

## Example

```sh
curl --user admin http://flowcount.local/state
curl --user admin -H "Content-Type: application/json" -d '{"command":"save"}' http://flowcount.local/command
```

`curl` prompts for the password. In PowerShell use `curl.exe` if `curl` is an alias; quoting may differ. Use the included network checker for broader verification.

## UART transport

UART0, 921600 baud; text and binary payloads share the stream:

| Prefix | Payload |
| --- | --- |
| `@NETWORK` | Boot access JSON, **includes private AP key** |
| `@NETWORK_STA` | Assigned IP and hostname |
| `@STATE` | Inference/counting state |
| `@EVENT` | Crossing event |
| `@ACK` | Command acknowledgement |
| `@JPEG` | Size, CRC32 and frame JSON, followed by raw JPEG bytes |
| `@ERROR` | Camera startup failure |
| `@SCAN` | Network scan results |

Text commands end with newline. Wi-Fi commands are `wifi ` followed by JSON. Prefer the supplied tools to avoid corrupting binary framing. `usbpreview 1` leases UART preview for 15 seconds, renewed by the monitor every five seconds; `usbpreview 0` stops it. These keepalive ACKs cannot complete a user's unrelated command. UART `scan` produces ACK followed by SCAN.

The Python monitor binds only `127.0.0.1`, exposes a similar API and accepts a smaller command allowlist. Its `/health` is unauthenticated for local HTML connection help. It is an installation/diagnostic tool, not a dependency of autonomous operation.
