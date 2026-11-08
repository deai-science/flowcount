# Third-party notices

## IMX219 ISP tuning

`config/imx219_default.json` is an unmodified copy from `mushbraindave/esp_cam_sensor_imx` 0.4.0, from [esp32-p4-imx-camera](https://github.com/mushBrainDave/esp32-p4-imx-camera). Its Apache-2.0 license is preserved in `config/LICENSE`.

## Captive portal DNS

`components/captive_dns/` comes from the [ESP-IDF 5.5.5 captive_portal example](https://github.com/espressif/esp-idf/tree/v5.5.5/examples/protocols/http_server/captive_portal). Source files retain their original notices:

```text
SPDX-FileCopyrightText: 2021-2023 Espressif Systems (Shanghai) CO LTD
SPDX-License-Identifier: Unlicense OR CC0-1.0
```

## Downloaded components and model

Component Manager downloads packages declared in `main/idf_component.yml` and resolved in `dependencies.lock`. They retain their licenses and notices. This source release does not redistribute downloaded components, model binaries or firmware binaries.

`espressif/pedestrian_detect` 0.3.2 is based on ESP-DL; its component includes an Espressif MIT license. Review the actual downloaded model and transitive package licenses before distributing firmware binaries. FlowCount's PolyForm Noncommercial license and any separate commercial agreement do not replace upstream conditions.

## Hardware references

Manufacturer documents are linked in [docs/SOURCES.md](docs/SOURCES.md); their PDFs and rendered pages are excluded. No board or camera hardware design is relicensed here.

## Branding and media

The UI embeds the official DeAI Labs isotype, `deai-isotype-v2.png`, linked by [deai.science](https://deai.science/) and hosted on [its public site](https://deai-labs.pages.dev/deai-isotype-v2.png). It is included at the owner's request for offline use. DeAI Labs and FlowCount marks remain their owner's property; the software license grants no trademark endorsement.

Diagrams, prototype photos and AI-retouched versions are documented in [docs/MEDIA.md](docs/MEDIA.md), including editing disclosures and reproduction permissions.
