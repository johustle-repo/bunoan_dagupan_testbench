# Universal ESP32 receiver (Arduino Uno or Nano)

One ESP32 sketch for both testbench sets. Plug in the Uno or the Nano; the board is detected automatically from each packet:
- Uno sends U3 frames → dashboard shows "Arduino Uno → ESP32"; Reference 1 / 2 boxes are hidden (the Uno has no supply-reference inputs).
- Nano sends U4 frames → dashboard shows "Arduino Nano → ESP32" with Reference 1 (A5, 5 V) and Reference 2 (A6, 3.3 V).

Use the unchanged sender sketches: Arduino-Uno-Testbench-Firmware/DashboardTelemetry (Uno) or arduino-nano/NanoTelemetry (Nano). Sensor pins are the same on both: A0 IAT, A1 ECT, A2 O2, A3 MAP, A4 TPS, D7 frequency, D8 smart key, D9 brake, D6 → ESP32.

- Wi-Fi: ECU-Testbench, password ECUdemo2026, http://192.168.4.1/
- /api/live reports "board": "uno" or "nano" and source "uno_via_esp32" / "nano_via_esp32".
- Battery (INA219) and the touchscreen work with either board.

## Board settings
ESP32 Dev Module, Flash Frequency 40MHz, Flash Mode DIO, Upload Speed 115200 (esp32:esp32:esp32:FlashFreq=40,FlashMode=dio,UploadSpeed=115200), ESP32 core 2.0.17. If upload reports "Wrong boot mode", hold BOOT, tap EN, release BOOT and retry. Close Serial Monitor before uploading.

Libraries: EspSoftwareSerial (Dirk Kaar / Peter Lerup), Adafruit GFX Library, Adafruit ILI9341, Adafruit BusIO, XPT2046_Touchscreen (Paul Stoffregen).

## Uno / Nano link
| Uno or Nano | ESP32 |
|---|---|
| D6 (TX) → 10 kΩ / 20 kΩ divider or level shifter | GPIO17 (RX) |
| D5 | GPIO16 optional, unused |
| GND | GND |

19200 baud 8N1, one-way. Power each board from its own USB.

## Touchscreen (2.8" ILI9341 + XPT2046)
| TFT / touch label | ESP32 |
|---|---|
| VCC, LED | 3V3 |
| GND | GND |
| CS | GPIO27 |
| RESET | GPIO26 |
| DC | GPIO25 |
| SDI (MOSI) + T_DIN | GPIO23 |
| SCK + T_CLK | GPIO18 |
| SDO (MISO) + T_DO | GPIO19 |
| T_CS | GPIO33 |
| T_IRQ, SD_* | not connected |

Tap anywhere on the screen to switch between the CLASSIC and RACE tabs.

## Battery (INA219, address 0x40)
| INA219 | Connection |
|---|---|
| VCC | ESP32 3V3 |
| GND | ESP32 GND and 12 V battery − |
| SDA | GPIO21 |
| SCL | GPIO22 |
| VIN+ | 12 V battery + |
| VIN− | + of the load (lamp, fan…), or leave open to read voltage only |

Up to 26 V and ±3.2 A with the module's 0.1 Ω shunt. The dashboard battery box and the TFT RACE "Bat" tile show it even with the smart key off: below 11.8 V low, 13.2 V and above charging, above 15 V overcharging. No library needed (BatteryMonitor.h uses Wire).

## Source
UI: ../dashboard/dist. After UI edits run `python tools/embed_dashboard.py esp32-universal/dashboard/dist esp32-universal/Receiver/DashboardAssets.h` from the project root.
