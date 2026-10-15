# Arduino Nano + ESP32 dashboard (sample set 2)

Same receiver, web dashboard and touchscreen as esp32/UnoReceiver, retargeted to the Nano set. The Nano sends the same U3 telemetry, so all sensor models, diagnostics, simulated RPM/gear and the CLASSIC / RACE touchscreen tabs with throttle angle work unchanged.

Differences from the Uno set, so both sets can run side by side:
- Wi-Fi: ECU-Testbench-Nano, password ECUdemo2026, http://192.168.4.1/
- /api/live reports source "nano_via_esp32"; the dashboard text says Arduino Nano → ESP32.

## Board settings
ESP32 Dev Module, Flash Frequency 40MHz, Flash Mode DIO, Upload Speed 115200 (esp32:esp32:esp32:FlashFreq=40,FlashMode=dio,UploadSpeed=115200), ESP32 core 2.0.17. If upload reports "Wrong boot mode", hold BOOT, tap EN, release BOOT and retry. Close Serial Monitor before uploading.

Libraries: EspSoftwareSerial (Dirk Kaar / Peter Lerup), Adafruit GFX Library, Adafruit ILI9341, Adafruit BusIO, XPT2046_Touchscreen (Paul Stoffregen).

## Nano link
| Nano | ESP32 |
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
UI: ../dashboard/dist. After UI edits run `python tools/embed_dashboard.py esp32-nano/dashboard/dist esp32-nano/NanoReceiver/DashboardAssets.h` from the project root.
