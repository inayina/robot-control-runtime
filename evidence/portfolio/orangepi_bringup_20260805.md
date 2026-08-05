# Orange Pi 4 Pro bring-up summary — 2026-08-05

## Provenance

| field | value |
|---|---|
| platform | Orange Pi 4 Pro 4GB |
| hostname/address | redacted |
| architecture | aarch64 |
| kernel | `6.6.98-sun60iw2` |
| compiler | GCC 11.4.0 |
| build type | Debug |
| source HEAD | `35419f35a85454a51587775bb30925d13ed8d4e8` |
| git dirty | `true` — provisional only |

## Observed

- 3.8 GiB visible memory; TF boot media; 6×Cortex-A55 + 2×Cortex-A76.
- Wi-Fi SSH, source synchronization, native CMake configure/build and non-vcan CTest completed.
- Release installed under `/opt/robot-control-runtime/releases/35419f35a854/`; `current` and binary
  SHA-256 matched the generated manifest.
- System user `rcr` and three units were installed; node simulator remained disabled.
- Kernel configuration reported `# CONFIG_CAN is not set`. `vcan` module/device creation failed,
  therefore `rcr-vcan.service` was unsupported and `rcrd.service` remained inactive through its
  declared dependency.

## Not closed

- No Orange Pi vcan/SocketCAN/rcrd lifecycle, bounded stop, crash restart or new-session evidence.
- Power label, undervolt/throttle observation, board silkscreen and exact NTP synchronization state
  were not fully captured.
- The source tree was dirty, so this is not a reproducible release baseline.

This report proves ARM build and deployment-contract observations. It does not prove physical CAN,
hard real-time behavior, functional safety or a continuously running Orange Pi daemon.
