# Public media, provenance and editing notes

Only the files listed here are approved public media. Private test recordings, snapshots, serial logs and manufacturer document images are not exported.

## Prototype photographs

| File | Provenance |
| --- | --- |
| [Front original](media/prototype-front-original.jpeg) | Owner-supplied photograph of prototype 1.0, November 8, 2026 |
| [Rear original](media/prototype-rear-original.jpeg) | Owner-supplied photograph of the opened prototype, same date |
| [Front retouched](media/prototype-front-retouched.png) | Built-in OpenAI image generation/editing tool, using front original as edit target |
| [Rear retouched](media/prototype-rear-retouched.png) | Same tool, using rear original as edit target |

The project owner requested public inclusion and retouching. Edits aim at cleaner light, white balance and presentation. They are **AI-assisted photo edits**, not untouched records. Fine component/label or geometry details can differ despite preservation instructions. Originals are included for comparison; use the schematic and pin table, not retouched photos, for assembly. The enclosure, ribbon and rubber band are real prototype features. No enclosure CAD is claimed to be available.

### Final rear-edit prompt

> Use case: lighting-weather. Edit target: the supplied real rear-view photograph of the FlowCount prototype 1.0. Create a restrained editorial retouch for an open-source hardware README: slightly improve white balance, even lighting, clarity and straight crop; keep the pale wood tabletop natural and uncluttered. Preserve the actual hardware exactly: every circuit board component, printed label, camera board, ribbon cable and its bends, rubber band, all connectors and ports, screws, black 3D-printed frame, dimensions and relative positions. Do not add or remove hardware, repair the prototype, invent details, replace labels, add text or logos, or make a render. Retain real photographic texture. Show the entire assembly with breathing room in a portrait composition. This must remain an honest retouched photo of this exact prototype.

### Final front-edit prompt

> Use case: lighting-weather. Edit target: the supplied real front-view photograph of the FlowCount prototype 1.0. Make a restrained editorial retouch for an open-source hardware README: slightly improve white balance, even illumination, clarity and crop. Remove only the unrelated partial pen at the top edge by cropping or tabletop cleanup. Keep the pale wooden table natural. Preserve the exact real black 3D printed enclosure, its rough printed surface, every ventilation opening and mounting hole, visible camera lens and board edges, rubber band, proportions and relative positions. Do not add, remove or redesign any part of the assembly; do not hide the rubber band or invent details. No text or logos. Retain photographic texture and show the entire prototype with breathing room in a straight portrait composition. This must be an honest retouched photograph, not a product render.

## Diagrams and UI

- `flowcount-hero.svg`: project banner and original FlowCount direction mark.
- `architecture.svg`: functional firmware/data-flow diagram.
- `hardware.svg`: functional module/pin map, explicitly not a mechanical drawing or pin-accurate schematic.
- `counting.svg`: illustrative confirmed-side counting sequence, not captured detections.
- `ui-overview.jpg`: browser rendering of the actual English `tools/monitor.html` at mobile and desktop sizes, with **illustrative synthetic data and a drawn scene**. It is not a live accuracy/performance measurement. The screenshot composition identifies the fixture data.
- `deai-isotype.png`: official DeAI Labs branding asset also embedded in the offline UI. See `THIRD_PARTY_NOTICES.md` for its source.

The SVGs are authored vector source and can be edited with a text editor or vector tool. No generated hardware rendering is presented as a factual schematic. The supplied video is a private reference and is not published.

## Reuse

Project-authored diagrams and documentation media are shared with the documentation under the repository's PolyForm Noncommercial license to the extent rights are held by the project. Commercial uses outside the license's permitted purposes require a separate agreement. Preserve attribution and editing disclosures. This does not grant rights to third-party hardware marks or imply DeAI Labs endorsement. DeAI Labs/FlowCount logos and names remain marks of their owner. AI edits do not replace the original photographs as evidence of physical details. The quoted editing prompts record the earlier draft wording; they are not statements of the current license.
