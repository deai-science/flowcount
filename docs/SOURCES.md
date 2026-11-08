# Technical sources

Use the pinned versions and lockfile for reproduction. Links to upstream main branches may evolve. Board compatibility was validated in this project; upstream support alone does not validate every board revision.

## Hardware and camera

- [GUITION board schematic, manufacturer documentation mirrored by DRubioG](https://github.com/DRubioG/JC-ESP32P4-M3-DEV/blob/main/5-Schematic/schematic.pdf): J3 CSI, camera GPIO7/8 and UART.
- [GUITION English specifications](https://www.guition.com/icms/upload/fb081940d6fc11f09850077a33e1404f/FTPData/UEditor/file/2026121/1768961096471/JC-ESP32P4-M3-DEV%20Specifications-EN.pdf).
- [IMX219 community component 0.4.0](https://components.espressif.com/components/mushbraindave/esp_cam_sensor_imx/versions/0.4.0/readme).
- [Driver source and examples](https://github.com/mushBrainDave/esp32-p4-imx-camera), consulted revision `9a3f8ca28e93526e0854ba45ad3b461b33ef2dac`.
- [Espressif video components architecture](https://docs.espressif.com/projects/esp-video-components/en/latest/esp32p4/Get_Started/index.html).

The ISP tuning JSON is unchanged from component 0.4.0 and retains `config/LICENSE`. Manufacturer PDFs are linked, not redistributed.

## Inference and connectivity

- [PedestrianDetect model source](https://github.com/espressif/esp-dl/tree/master/models/pedestrian_detect).
- [pedestrian_detect 0.3.2](https://components.espressif.com/components/espressif/pedestrian_detect/versions/0.3.2/readme).
- [esp_video 2.4.0](https://components.espressif.com/components/espressif/esp_video/versions/2.4.0/readme).
- [ESP-Hosted 3.0.10](https://components.espressif.com/components/espressif/esp_hosted/versions/3.0.10/readme).
- [esp_wifi_remote 1.6.5](https://components.espressif.com/components/espressif/esp_wifi_remote/versions/1.6.5/readme).
- [mDNS 1.8.2](https://components.espressif.com/components/espressif/mdns/versions/1.8.2/readme).
- [ESP-IDF 5.5.5 captive portal example](https://github.com/espressif/esp-idf/tree/v5.5.5/examples/protocols/http_server/captive_portal): origin of the local DNS component.

The project uses a pretrained detector; no custom model was trained on venue images. FlowCount supplies the tracking/counting logic around its boxes.

## Project and licensing

- [DeAI Labs](https://deai.science/).
- [PolyForm Noncommercial License 1.0.0](https://polyformproject.org/licenses/noncommercial/1.0.0).
- [OSI definition and commercial-use distinction](https://opensource.org/faq).
- [Vitalik's explanation of copyleft](https://vitalik.eth.limo/general/2025/07/07/copyleft.html), background rather than FlowCount's selected license.
- [GitHub Actions checkout source](https://github.com/actions/checkout).

See `THIRD_PARTY_NOTICES.md` for attribution and `MEDIA.md` for public asset provenance.
