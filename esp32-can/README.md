# OBD-II diagnostic bench

```
Set 1: ECU simulator                    Set 2: diagnostic tool bench
Nano + MCP2515 + encoder + 16x2 LCD     Nano + MCP2515  ->  ESP32 DevKit (Wi-Fi scan-tool page)
        0x7E8 answers  <==CAN 500 kbps OBD-II==>  0x7DF requests / 0x7E0 flow control
```

| Sketch | Board | Folder |
|---|---|---|
| NanoSensorSimulator (the ECU) | Set 1 Nano | `arduino-nano/NanoSensorSimulator` |
| NanoObdTester (scan tool, CAN side) | Set 2 Nano | `arduino-nano/NanoObdTester` |
| EspObdDashboard (scan tool, screen) | Set 2 ESP32 WROOM-32 DevKit 38 pin, "ESP32 Dev Module" | `esp32-can/EspObdDashboard` |

Libraries: **mcp_can** (coryjfowler) for both Nanos, plus **AltSoftSerial**
(Paul Stoffregen) for the Set 2 Nano. Both MCP2515 modules have 8 MHz crystals.

## Wiring

**MCP2515 → Nano**

| MCP2515 | Set 1 Nano (ECU) | Set 2 Nano (tester) |
|---|---|---|
| VCC / GND | 5V / GND | 5V / GND |
| CS | **D8** | **D10** |
| SI / SO / SCK | D11 / D12 / D13 | D11 / D12 / D13 |
| INT | D2 | D2 |

On Set 1, TPS is on **D3**, because D11 is the MCP2515's SI.

**CAN bus:** CANH ↔ CANH, CANL ↔ CANL. Fit the **J1** 120 Ω jumper on **both** modules.

**Set 2 Nano ↔ ESP32** (AltSoftSerial, 38400 baud)

| Nano | ESP32 |
|---|---|
| D9 (TX) | 10 kΩ → **GPIO16**, and 20 kΩ from GPIO16 to GND (5 V → 3.3 V) |
| D8 (RX) | **GPIO17** (direct) |
| GND | GND |

D0/D1 stay free, so uploads over USB work with the ESP32 connected.

## What the ECU answers

| Request | Answer |
|---|---|
| 01 00 | Supported PIDs: 01, 05, 0B, 0F, 11, 14 |
| 01 01 | MIL (on when any DTC is stored) + DTC count |
| 01 05 / 0F | ECT / IAT, °C + 40 |
| 01 0B | MAP kPa |
| 01 11 | TPS, % × 255 / 100 |
| 01 14 | O2 B1S1, V × 200 (B = FF) |
| 03 | Stored DTCs (multi-frame when there are 3 or more) |
| 04 | Clears the DTCs. A fault that is still active stores its DTC again at once |

A long press on the Set 1 encoder sets a fault. Its DTC is stored and stays
until **Clear DTC**. While a fault is active, mode 01 gives that sensor's
fail-safe value: IAT 20 °C, ECT 80 °C, O2 0.45 V, MAP 100 kPa, TPS 0 %.

| Sensor | LOW (0 V) | HIGH (full scale) |
|---|---|---|
| IAT | P0112 | P0113 |
| ECT | P0117 | P0118 |
| O2 | P0131 | P0132 |
| MAP | — | P0108 |
| TPS | — | P0123 |

## Range and engine checks (Set 1)

Besides the circuit faults, the ECU checks that each value stays in its
**normal** range and inside its **safe** limit. A level has to hold for 1 s
before it counts, so sweeping the knob through a range doesn't set a code.

| DTC | Check | Normal | Pending (outside normal) | Confirmed + MIL (beyond safe) |
|---|---|---|---|---|
| **P0217** | Engine coolant over temperature | ECT ≤ 105 °C | 106–115 °C | above 115 °C |
| P0111 | IAT range/performance | IAT ≤ 45 °C | 46–50 °C | above 50 °C |
| **P0171** | System too lean | O2 ≥ 0.20 V | 0.10–0.19 V | below 0.10 V |
| **P0172** | System too rich | O2 ≤ 0.80 V | 0.81–0.90 V | above 0.90 V |
| P0106 | MAP range: high at closed throttle (TPS ≤ 5 %) | MAP ≤ 60 kPa | 61–80 kPa | above 80 kPa |
| P0121 | TPS range: open with no load (MAP ≤ 30 kPa) | TPS ≤ 50 % | 51–80 % | above 80 % |

- **Pending** (mode 07) shows only while the value is outside normal. The MIL stays off.
- **Confirmed** (mode 03) turns the MIL on and stays stored after the value
  returns to normal, until **Clear DTC**.
- A check is skipped while one of its sensors has a circuit fault (long press).
- The thresholds are constants under "Range and engine checks" in
  `NanoSensorSimulator.ino`.
- On the Set 1 LCD, `*` after a sensor letter = outside normal, `!` = beyond
  safe or circuit fault.

## Using it

1. Join Wi-Fi **ECU-OBD** (password `ECUdemo2026`) and open http://192.168.4.1/.
2. The page shows ECU ONLINE, the live data, the MIL and the stored DTCs.
   DTCs (stored and pending) are read every 2 s, or press **Read DTC**.
3. Long-press the Set 1 encoder to set a fault: the MIL comes on and the DTC appears.
4. Turn the encoder to remove the fault, then press **Clear DTC**.

**Checking**
- **Set 1 LCD**, last character of line 2: `C` = scan tool talking, `.` = CAN OK but
  no requests, `-` = MCP2515 not found.
- **Set 2 Nano Serial Monitor** (115200): `PID,05,130,0,0,0`, `DTC,1,P0113`, `NORESP,...`.
- **ESP32 Serial Monitor** (115200): one summary line a second, or what is missing.
