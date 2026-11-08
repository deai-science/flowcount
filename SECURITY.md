# Security and privacy

FlowCount 1.0 is a local-network prototype. It is not hardened for Internet exposure.

## Current model

- The UI/API and USB monitor use fixed HTTP Basic credentials **admin/admin**. These are public defaults. Changing them currently requires code changes in both servers and clients.
- HTTP is unencrypted. Local clients may be able to observe credentials and live images. Use a trusted, isolated LAN; do not forward device ports to the Internet.
- The AP has a separate WPA2 key generated at first boot and stored in NVS. Venue credentials are also stored in NVS. Flash/NVS encryption and secure boot are not enabled by default.
- UART exposes the AP key at boot in `@NETWORK`. Backups can contain network credentials. Keep access files and backups private.
- POST requests validate origin when supplied, commands and lengths. These checks do not replace isolation or unique credentials.
- Frames are processed in memory; the firmware does not record video to storage by default. Clients receive live JPEGs. Track IDs are temporary, not biometric identities.

## Reporting a vulnerability

Use GitHub private vulnerability reporting if enabled, or contact the maintainers through [deai.science](https://deai.science/). Do not post exploit details or secrets publicly before coordinating a fix. Include commit, reproduction and impact, using synthetic data where possible.

There is no guaranteed support period or response SLA. Configurable authentication is future work. Third-party dependencies retain their own support policies.
