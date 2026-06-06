# silent_powmr firmware (ESP32 + TuyaOpen) — production SmartLife app

> ⚠️ **Status: WRITTEN, NOT YET BUILT.** This is the production SmartLife/Tuya app
> for a **future phase**. It was authored against the documented TuyaOpen / ESP‑IDF
> APIs but **has never been compiled on the toolchain** (none was available where
> this repo was generated), and it is **not** built in CI. Treat it as a reference
> implementation: expect a normal first‑build bring‑up. For something that works on
> the hardware **today**, use the diagnostic firmware in [`../diag`](../diag).
>
> The control *logic* it runs is the host‑tested [`core/`](../core), so the
> behaviour — not just the syntax — is already validated by the host test suite.

The interesting control logic lives in the repo‑root [`core/`](../core) library
(Modbus codec, register decode, the quiet‑mode FSM, DP mapping) and is **fully
host‑unit‑tested**; this app only adds the ESP32 glue:

| File | Role |
|------|------|
| `src/uart_transport.*` | `ModbusTransport` over ESP‑IDF UART (RS232 / MAX3232) |
| `src/inverter_app.*`   | Autonomous control‑loop task; NVS config; thread‑safe snapshot |
| `src/tuya_glue.cpp`    | SmartLife (Tuya) client + DP report/receive |
| `src/web_server.cpp`   | Mobile‑friendly local status page + JSON API + controls |
| `src/main.cpp`         | `tuya_app_main()` entry, wiring it all together |

## Wiring & confirmed protocol

Inverter RJ45 **RS232** port → MAX3232 → ESP32 **UART2**:

| RJ45 pin | Signal | ESP32 |
|---------:|--------|-------|
| 1 | TX  | GPIO16 (RX2) |
| 2 | RX  | GPIO17 (TX2) |
| 8 | GND | GND |

Hardware‑verified on POW‑HVM4.2K‑24V‑D (full details in the
[root README](../README.md#confirmed-protocol--wiring)):

- **Modbus RTU, 2400 8N1, slave id 5**, function 3 (read) / 6 or 16 (write).
- **Byte order:** 4500‑range measurement registers are **little‑endian**
  (byte‑swapped); 5000‑range settings (5017/5018) are **big‑endian**.
- **Register windows:** the unit only answers reads of **≤ ~14 registers** in
  specific windows, so `poll()` issues **two** reads — **4502 count 13** and **4546
  count 16** (plus a single read of error reg 4530); a single large read is
  rejected.
- A small cap (e.g. 10–100 nF) on the ESP32 RX line tames the inverter's RF noise.
- If you get no comms, **swap TX/RX** (this family is known to have TX2/RX2
  swapped) — see `kSlaveId` / GPIO constants in `src/inverter_app.*`.

## Tuya / SmartLife setup

1. On [iot.tuya.com](https://iot.tuya.com): create a **Custom** product, note its **PID**.
2. Add Data Points matching [`core/include/silent_powmr/dp_map.h`](../core/include/silent_powmr/dp_map.h):

   | DP ID | Type | Meaning | Scale |
   |------:|------|---------|-------|
   | 101 | value | Battery voltage | 0.1 V |
   | 102 | value | Battery SoC | % |
   | 103 | value | Load power | W |
   | 104 | value | Load current | 0.1 A |
   | 105 | bool  | Grid present | — |
   | 106 | value | Battery power (±) | W |
   | 107 | value | Battery current (±) | 0.1 A |
   | 108 | enum  | Charger mode (0 charge / 1 quiet) | — |
   | 109 | value | Temperature | 0.1 °C |
   | 110 | value | Fault code | — |
   | 120 | bool  | Auto quiet enable | — |
   | 121 | enum  | Manual charger mode (0/1) | — |
   | 122 | value | Recharge SoC threshold | % |

3. Create a **device license** (UUID + AuthKey). Copy `src/secrets.example.h` to
   `src/secrets.h` and fill in PID/UUID/AuthKey. `secrets.h` is git‑ignored.

## Build & flash (first bring‑up — not yet done)

Prerequisite: [TuyaOpen](https://github.com/tuya/TuyaOpen) checked out and its
environment set up (`. ./export.sh`), which provides `tos.py` and pulls the ESP32
platform toolchain.

```bash
cd firmware
tos.py config choice      # select esp32 / esp32c3 / esp32s3
                          # -> writes app_default.config (commit it so repeat/CI
                          #    builds are non-interactive)
tos.py build              # output: .build/bin/silent_powmr_QIO_*.bin
tos.py flash -p /dev/ttyUSB0
tos.py monitor -p /dev/ttyUSB0
```

`app_default.config` is currently a **placeholder** — the TuyaOpen toolchain wasn't
available where this repo was generated, so the concrete Kconfig values still need
to be produced on a first build. Wi‑Fi is provisioned by pairing the device in the
SmartLife app (TuyaOpen netmgr); once on the network, the local page is at
`http://<device-ip>/`.

## Confirmed values (already calibrated)

The hardware‑specific values are **confirmed** on POW‑HVM4.2K‑24V‑D and baked into
[`core/include/silent_powmr/registers.h`](../core/include/silent_powmr/registers.h):

- **reg 5017 (the fan lever):** CSO=0, **SNU=1** (charge, fans run), **OSO=2**
  (quiet, fans off). Settings are read/written **big‑endian**.
- **scaling:** voltages ×0.1; battery currents (4508/4509) whole **amps** (×1);
  temperature ×1.
- **baud/slave:** 2400 8N1, slave id 5. **byte order:** 4500‑range measurements
  little‑endian, 5000‑range settings big‑endian.

If you want to re‑verify on your own unit (or after a firmware change), the
diagnostic firmware ([`../diag`](../diag)) reads these off live and lets you set
reg 5017 by hand. Any corrected value goes back into `registers.h`, which the
firmware picks up automatically.

> Note: the SmartLife DPs report battery/load current as 0.1 A‑scaled values
> ([`dp_map.h`](../core/include/silent_powmr/dp_map.h)) — the DP layer multiplies
> the whole‑amp reading by 10, so the app shows e.g. `9.0 A` for a raw `9`.
