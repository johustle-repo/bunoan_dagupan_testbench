# CAN sensor link

```
Nano 1: Sensor Simulator  --CAN 500 kbps-->  Nano 2: CAN Gateway  --serial 115200-->  ESP32 CAN Dashboard
  encoder + 16x2 LCD                           MCP2515                                 Wi-Fi web page
  MCP2515                                      TX D1 -> divider -> GPIO16
```

| Sketch | Board | Folder |
|---|---|---|
| NanoSensorSimulator | Nano 1 | `arduino-nano/NanoSensorSimulator` |
| NanoCanGateway | Nano 2 | `arduino-nano/NanoCanGateway` |
| EspCanDashboard | ESP32 WROOM-32 DevKit (38 pin), "ESP32 Dev Module" | `esp32-can/EspCanDashboard` |

Library for both Nanos: **mcp_can** (coryjfowler). Both MCP2515 modules have
8 MHz crystals (`MCP_8MHZ` in each Nano sketch).

## Wiring

**MCP2515 → Nano**

| MCP2515 | Nano 1 (simulator) | Nano 2 (gateway) |
|---|---|---|
| VCC | 5V | 5V |
| GND | GND | GND |
| CS | **D8** | **D10** |
| SI | D11 | D11 |
| SO | D12 | D12 |
| SCK | D13 | D13 |
| INT | D2 | D2 |

On Nano 1, TPS moved from D11 to **D3**, because D11 is the MCP2515's SI.

**CAN bus:** CANH ↔ CANH, CANL ↔ CANL, a twisted pair if the wires are long.
Fit the **J1** 120 Ω jumper on **both** modules, since each one is an end of the bus.

**Gateway → ESP32**

| Nano 2 | ESP32 |
|---|---|
| D1 (TX) | 10 kΩ → **GPIO16**, and 20 kΩ from GPIO16 to GND (5 V → 3.3 V) |
| GND | GND |

Never connect the Nano TX straight to the ESP32. Leave Nano 2 D0 (RX)
unconnected so uploads over USB keep working.

## Checking it

- **Nano 1 LCD**, last character of line 2: `C` = gateway acknowledges, `x` = no
  ACK (check CANH/CANL, J1, gateway powered), `-` = MCP2515 not found.
- **Nano 2 Serial Monitor** (115200): `CAN,25,90,45,35,0,0,0,17` lines, or `NOLINK`.
- **ESP32**: join `ECU-CAN` (password `ECUdemo2026`), open http://192.168.4.1/.
  A faulted sensor shows ERR and its DTC (P0113 / P0112 etc.). `/api/live` gives JSON.

## CAN frame 0x100 (8 bytes)

| Byte | Content |
|---|---|
| 0 | IAT °C + 40 |
| 1 | ECT °C + 40 |
| 2 | O2 volts × 100 |
| 3 | MAP kPa |
| 4 | TPS % |
| 5 | HIGH-fault bits (bit 0 IAT, 1 ECT, 2 O2, 3 MAP, 4 TPS) |
| 6 | LOW-fault bits |
| 7 | Counter, +1 per frame (the ESP32 counts lost frames from it) |
