# silent_powmr — diagnostic / bring-up firmware

A self‑contained **ESP‑IDF** app (no TuyaOpen) for bringing up and operating the
inverter over Wi‑Fi: read its live values, probe arbitrary registers, drive a few
settings by hand, and update itself over the air. It's the **working** firmware in
this repo — flash it first.

It reuses the host‑tested [`core/`](../core) Modbus codec and register decode, so
what you see here is exactly how the production firmware interprets the bus
(including this unit's quirky byte order: 4500‑range measurements little‑endian,
5000‑range settings big‑endian).

> **Not read‑only.** Besides reads, it exposes **password‑gated** writes for two
> settings (charging mode, max charge current) and **web OTA**. Both write paths
> require the control password. Everything else (the page, the register probe) only
> reads.

## What it does

- **Read page** (`/`) — battery V, load W + VA, grid (present/lost), temperature,
  battery flow (±W/±A), charger status, error code, and the reg‑5017 fan lever with
  its decoded mode. Auto‑refreshes every 3 s.
- **Register table** — read any window (`/api/regs`), shown as **addr · name · hex ·
  decoded value**, with one‑click presets (Telemetry / Settings / Charge‑Status).
  Decoding applies this unit's scaling and per‑address enums; reads are
  little‑endian, 5000+ settings big‑endian. Keep counts **≤ ~14** (the unit rejects
  larger windows).
- **Register probe** — `/api/raw` (one arbitrary read: slave, start, count, baud,
  TX/RX swap) and `/api/sweep` (tries a list of candidate slave/baud/window/swap
  combos) to find a responding configuration with no PC Modbus tool. The same probe
  is available on the **USB serial console** (`r <slave> <start> <count> [baud]
  [swap]` for one read, `s` for the sweep).
- **Manual controls** (password‑gated) — set **charging mode** CSO/SNU/OSO
  (reg 5017) and **max total charge current** 10–80 A (reg 5022). Writes are
  whitelisted to those two registers and value ranges; the write retries and tries
  function 0x06 then 0x10 (some settings only accept write‑multiple).
- **Change control password** — from the page, with the current password, stored in
  NVS.
- **Web OTA** (`/update`) — upload a new app image; two‑slot partitions with
  rollback.

## Flashing (first time: USB)

Prerequisite: [ESP‑IDF](https://docs.espressif.com/projects/esp-idf/) **v5.5.x**
(CI uses v5.5.4).

```bash
idf.py -C diag set-target esp32        # or esp32c3 / esp32s3
idf.py -C diag build flash monitor -p /dev/ttyUSB0
```

(Or `cd diag && idf.py build flash monitor -p …`.) The **first** flash must be over
USB — it lays down the bootloader and the two‑slot OTA partition table. After that
you can update over Wi‑Fi (see OTA below).

No toolchain handy? CI builds this and uploads a `.bin` artifact
(`silent_powmr_diag-esp32`), built in the `espressif/idf:v5.5.4` image.

## Wi‑Fi setup (no hardcoded credentials)

1. On first boot it starts a SoftAP **`silent_powmr-setup`** (password
   `powmr-setup`). Join it from a phone/laptop.
2. Browse to **http://192.168.4.1/** — pick your network, enter the password, Save.
   It stores the creds in NVS and reboots as a station.
3. It announces the DHCP hostname **`silent-powmr-diag`** — find it in your router's
   lease table to get the IP, then open `http://<ip>/`. (The IP is also printed on
   the serial monitor at boot.)
4. **Re‑provision Wi‑Fi:** hold the **BOOT** button (GPIO0) while powering on — it
   ignores the stored creds for that boot and comes up in the setup AP so you can
   enter a new network. (Physical action only; nothing on the web page changes
   Wi‑Fi.)

## Manual controls & the control password

The page can change two inverter settings, both behind a **control password**:

- **Charging mode (reg 5017)** — CSO (0) / SNU (1) / OSO (2). This is the fan lever:
  **OSO** stops grid charging (fans off), **SNU** charges from the grid (fans run).
- **Max total charge current (reg 5022)** — 10–80 A.

Enter the password in the page (it's remembered in the browser), pick a value, Set,
and confirm. Writes are restricted server‑side to exactly those two registers and
their valid ranges.

The control password is **stored in NVS** (default **`powmr-ota`**) and can be
changed from the page (**Change password**, needs the current one). The **same**
password also gates **web OTA** (HTTP Basic Auth, user **`admin`**). **Change it
before relying on either.** Constants live in
[`main/diag_web.cpp`](main/diag_web.cpp).

Optionally you can also require a login just to *view* the page: set `kAuthUser` /
`kAuthPass` in `diag_web.cpp` (empty by default = open viewing). Basic Auth is
unencrypted over HTTP — fine on a trusted LAN; for encryption, serve over HTTPS.

## Firmware updates over Wi‑Fi (OTA)

After the first USB flash you can update with no cable:

1. Open **`http://<ip>/update`** (link in the page header). It prompts for the OTA
   login — user **`admin`**, password = the **control password** (default
   `powmr-ota`).
2. Upload a new app image (`diag/build/silent_powmr_diag.bin`). It flashes the spare
   slot and reboots into it.

**Safety net:** rollback is enabled. A new image must boot far enough to bring
Wi‑Fi + the web server up and mark itself valid; if it doesn't, the bootloader
reverts to the previous image on the next reset — so a botched remote flash recovers
itself instead of bricking. Only the **app image** is uploaded over OTA; the
bootloader and partition table (which rarely change) still need a USB flash.

## Using it to calibrate `core/registers.h`

Most values are already hardware‑confirmed (see the root README's *Confirmed
protocol & wiring*); use this page to re‑verify on your unit or after a firmware
change:

1. **Addresses & scaling** — compare each register's decoded value to the inverter's
   front panel (battery V, load W). If a measurement looks byte‑swapped, flip
   `kReadLittleEndian` in [`main/diag_main.cpp`](main/diag_main.cpp); adjust
   `kVoltageScale` / `kCurrentScale` in
   [`core/registers.h`](../core/include/silent_powmr/registers.h) so values match.
2. **Reg 5017 enum** — set the inverter to each mode and read 5017 (settings are
   big‑endian). Confirmed here: **CSO=0, SNU=1, OSO=2**.
3. **Slave id / pins** — if you get "NO inverter reply", use the probe/sweep
   (`/api/sweep` or the serial `s` command) to find the right slave id and whether
   TX/RX are swapped; the defaults are slave 5, TX=GPIO17, RX=GPIO16 in
   `main/diag_main.cpp`.

Feed any corrected values back into `core/registers.h`; the production firmware
picks them up automatically.

## USB serial vs OTA

- **USB serial** is needed for the **first** flash and for the low‑level probe
  console (`r …` / `s`). It also prints the assigned IP at boot.
- **OTA** (over Wi‑Fi) is for **subsequent** app updates only — no cable, but it
  can't replace the bootloader/partition table.
