# silent_powmr

Firmware for an ESP32 that makes a **PowMR 24V 4.2kW hybrid inverter**
(POW‑HVM4.2K‑24V‑D, used as a whole‑apartment UPS with **no solar**) **quiet**,
and exposes it as a **SmartLife (Tuya)** device plus a local web dashboard.

## The problem

In the inverter's default configuration the battery is held on charge from the
grid, so the **internal fans run constantly** — and they're loud. The fan lever
is the **charger source priority** (Modbus register **5017**): when the unit is
allowed to charge from the grid the fans spin; when it isn't, they stop.

On this menu the values are **CSO=0 / SNU=1 / OSO=2** (confirmed live by switching
modes and reading reg 5017 — this differs from the generic `0..3` maps you'll see
elsewhere). With no panels:

- **SNU (1)** = *Solar and Utility* → charges from the grid → **fans run**.
- **OSO (2)** = *Solar only* → never charges from the grid → **fans off**.

So "go quiet" simply means write **5017 = 2 (OSO)**, and "recharge" means write
**5017 = 1 (SNU)**. That's safe **as long as the battery is full and grid power is
present** — and the control loop only goes quiet under exactly those conditions.

## What it does

1. **Silences the inverter** with a small autonomous control loop on the ESP32
   that flips reg 5017:
   - grid present **and** battery full → **OSO** (quiet);
   - grid lost **or** battery drops to the recharge threshold → **SNU** (recharge);
   - **fail‑safe**: lost comms or a fault → **SNU** (a UPS must never be left
     uncharged).

   The loop runs **on the device** — it keeps working during an internet/cloud
   outage.
2. **Monitors & controls remotely** via **SmartLife (Tuya)** Data Points and a
   **mobile‑friendly local web page** served by the ESP32 (no cloud needed).

> **SoC note:** the inverter's own state‑of‑charge (reg 4507) is not trustworthy
> on this unit (no BMS link), so it is **not** displayed and the production
> control loop is intended to gate "battery full" on **battery voltage**, not SoC.

## Confirmed protocol & wiring

Hardware‑verified on POW‑HVM4.2K‑24V‑D:

| Item | Value |
|------|-------|
| Protocol | **Modbus RTU**, function 3 (read) / 6 or 16 (write) |
| Serial | **2400 8N1**, slave id **5** |
| Bus | ESP32 **UART2** — GPIO17 = TX, GPIO16 = RX — via a **MAX3232** to the inverter's RS232 (RJ45) port |
| Measurement registers (4500‑range) | **little‑endian** (byte‑swapped vs standard Modbus) |
| Settings registers (5017 / 5018) | **big‑endian** |
| Scaling | voltages ×0.1; battery currents (4508/4509) whole amps (×1); temperature ×1 |
| Grid present | AC‑input voltage (reg 4502) **≥ 180 V** (not a status‑flag bit) |

**Register windows matter.** The unit only answers reads of **≤ ~14 registers**
inside specific windows; a single large read (e.g. 4501 count 61) is rejected with
no reply. `poll()` therefore issues **two** reads — **4502 count 13** and **4546
count 16** — plus a best‑effort single read of the error register (4530). The gap
4515..4545 is left unread.

This is **not** the Voltronic PI30 / `QPIGS` ASCII protocol that some
similarly‑named PowMR models use. References:
[odya/esphome‑powmr‑hybrid‑inverter](https://github.com/odya/esphome-powmr-hybrid-inverter)
and [leodesigner/powmr_comm](https://github.com/leodesigner/powmr_comm). The
register map, scaling, windows, byte order, and the reg‑5017 values are
centralized in
[`core/include/silent_powmr/registers.h`](core/include/silent_powmr/registers.h).

## Repository layout

```
core/      Portable C++17: Modbus codec, PowMR register decode, quiet-mode FSM,
           inverter client + control cycle, Tuya DP mapping. Hardware-free.
test/      Host unit & integration tests (tiny built-in harness, no deps).
sim/       Simulated Modbus inverter used by the integration tests.
diag/      Standalone ESP-IDF diagnostic / bring-up firmware (WORKING — flash this
           first). Wi-Fi provisioning, web read page, register probe, manual
           controls, web OTA. See its README.
firmware/  Production ESP32 SmartLife app (ESP-IDF + TuyaOpen). WRITTEN but not yet
           built/validated on the toolchain — a reference for a future phase.
.github/   CI: host tests (gating) + ESP-IDF diagnostic-firmware artifact build.
```

## The two firmwares (honest status)

| Firmware | What it is | Status |
|----------|-----------|--------|
| **`diag/`** | Standalone ESP‑IDF bring‑up tool. Read page, register probe, password‑gated manual writes, web OTA. | ✅ **Working** — all features verified live over Wi‑Fi against the real inverter. Builds in CI. |
| **`firmware/`** | The SmartLife/Tuya production app (autonomous quiet loop + DPs + local page). | ✍️ **Written, not yet built** — authored against documented TuyaOpen/ESP‑IDF APIs but never compiled on a toolchain. Reference for the future SmartLife phase. |

Both reuse the same portable **`core/`** library verbatim, so the control‑critical
logic is identical and already validated by the host tests below.

## The diagnostic firmware (`diag/`)

A self‑contained ESP‑IDF app for bringing up and operating the inverter over
Wi‑Fi. Verified features:

- **Wi‑Fi provisioning** via a SoftAP portal — **no hardcoded credentials**;
  announces DHCP hostname **`silent-powmr-diag`**.
- **Mobile‑friendly read page** — battery V, load W + VA, grid, temperature,
  charger mode, and a decoded register table (per odya's map, with this unit's
  big/little‑endian handling).
- **Register probe** (`/api/raw`, `/api/sweep`) for arbitrary reads and a
  multi‑config sweep, plus a USB serial console — for calibration without a PC
  Modbus tool.
- **Manual controls** — charging mode (CSO/SNU/OSO, reg 5017) and max total charge
  current (10–80 A, reg 5022), **gated by a control password** (stored in NVS,
  default `powmr-ota`) with an in‑page **change‑password** feature.
- **Web OTA** (`/update`) — two‑slot partitions with **rollback**, so a botched
  remote flash reverts itself.

See [`diag/README.md`](diag/README.md). SoC is intentionally hidden (no BMS link).

## Control logic (`core/quiet_controller`)

A deterministic state machine with two charger‑priority intents — **CHARGE** (SNU)
and **QUIET** (OSO):

| In state | Switches to | When |
|----------|-------------|------|
| QUIET | CHARGE | grid lost **or** battery below the recharge threshold, **immediately** |
| CHARGE | QUIET | grid present **and** battery full, after a dwell |
| any | CHARGE | status invalid **or** fault (fail‑safe) |

Hysteresis, a **grid‑flicker debounce**, and a **minimum dwell** before going quiet
prevent rapid toggling. Switching toward CHARGE is immediate (the safe direction);
switching to QUIET respects the dwell.

## SmartLife Data Points (production app)

Telemetry (reported): battery V/%, load W/A, grid present, battery power/current
(±), charger mode, temperature, fault. Control (writable): auto‑quiet enable,
manual charger mode, recharge threshold. Full table with IDs/scales:
[`firmware/README.md`](firmware/README.md#tuya--smartlife-setup) /
[`core/include/silent_powmr/dp_map.h`](core/include/silent_powmr/dp_map.h).

## Build & test (host)

The portable core + its tests are plain C++17 and build with any compiler:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

This builds `core/`, the simulated inverter (`sim/`), and the host test suite —
**47 unit/integration tests (144 checks)**, including the full *poll → decide →
write* loop against the simulated Modbus inverter. On Windows with MSVC, run from a
*Developer PowerShell* (or `Enter-VsDevShell`) so `cl` is on `PATH`. CI runs the
same on Ubuntu/gcc.

## CI

[`.github/workflows/ci.yml`](.github/workflows/ci.yml):

- **host‑tests** (gating) — builds the core and runs all unit/integration tests.
- **diag‑firmware** — builds the diagnostic ESP‑IDF app inside the
  `espressif/idf:v5.5.4` Docker image (toolchain baked in, no per‑run install) and
  uploads the `.bin` artifact (`silent_powmr_diag-esp32`).

The production `firmware/` (TuyaOpen) is **not** built in CI yet — it needs a first
bring‑up on the toolchain.

## Status

| Part | State |
|------|-------|
| `core/` logic (Modbus, decode, FSM, DP, control loop) | ✅ implemented & **host‑tested** (47 tests) |
| Host build + CI (gating) | ✅ green locally (MSVC) and on CI (gcc) |
| `diag/` diagnostic firmware | ✅ **working**, verified live; builds in CI |
| Confirmed inverter config (serial, windows, byte order, reg‑5017 values, scaling) | ✅ **hardware‑verified** — see *Confirmed protocol & wiring* |
| `firmware/` SmartLife app (UART, Tuya, web glue) | ✍️ written; **needs first build on the TuyaOpen toolchain** |

## License

See [LICENSE](LICENSE).
