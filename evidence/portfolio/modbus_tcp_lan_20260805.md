# Modbus TCP two-host demo — 2026-08-05

Topology: Orange Pi client over the existing Wi-Fi management LAN to a ThinkPad reference server on
TCP port 1502. Hostnames, IP addresses and MAC addresses are intentionally omitted.

Observed application sequence:

```text
read holding[0..3]: 0x1234 0xabcd 0x0 0x0
write_multiple ok addr=20 qty=3
```

Covered function codes: `0x03`, `0x06`, `0x10`. The Orange Pi build did not include libmodbus;
libmodbus interoperability was exercised separately on the ThinkPad development environment.

This is a teaching/reference-server LAN demo. It does not prove a field PLC/instrument, network
security partitioning, real-time behavior, EtherCAT traffic, or integration into `rcrd`.
