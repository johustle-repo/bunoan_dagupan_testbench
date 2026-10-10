# ESP32-C6 reference dashboard firmware

The dashboard follows the client's sketch with ECU and ABS views: left sensor column, central numeric/analog speed gauge, right circular indicators, RPM or rear-wheel bar, and bottom reference/battery voltages. The on-screen simulation knob is preserved below the instrument display.

All data remains simulated. Battery current and duty cycle are unconfigured placeholders. Speed covers 0–250 km/h and RPM 0–15,000, following the provisional Word specification. Unknown indicator functions in the sketch are not invented. Real inputs and calibration are not implemented.

Connect to Wi-Fi ECU-Testbench-C6, password ECUdemo2026, then open http://192.168.4.1/ and refresh. No internet is required. Use the knob to adjust the demonstration readings; click the indicators to toggle them.

Build TestbenchDashboard/TestbenchDashboard.ino with its accompanying headers using Espressif Arduino core 3.3.12. Board target: esp32:esp32:esp32c6:CDCOnBoot=cdc,FlashSize=16M,FlashMode=dio,FlashFreq=40. The verified board has 16 MB flash and was connected on COM9.

## Engine and brake simulation

The dashboard starts with the engine off and RPM at zero. Start Engine brings it to idle. The knob selects a target speed and readings change gradually as the simulation accelerates. Brake is a toggle: it slows to zero, clears the target speed, and prevents knob changes until Release Brake is clicked. Releasing the brake leaves the engine idling. Stop Engine clears speed and target speed. Reset turns the engine off, clears indicators, and restores the initial temperature. All controls affect simulated data only; no real engine or GPIO is controlled. The browser calculates the demonstration dynamics and sends readings to the ESP32 manual telemetry endpoint every 500 ms.

## Power-on discovery
The visible ECU-Testbench-C6 Wi-Fi access point and dashboard start automatically on power-up. Password: ECUdemo2026. Connect to this network; captive-portal DNS and common device detection routes direct HTTP requests to http://192.168.4.1/. Automatic sign-in prompts depend on the phone/computer; if no prompt appears, open that URL manually. This network serves the local dashboard without internet access. All data remains simulated.

## Live monitor firmware
The updated source adds Live monitor mode and an input-only acquisition API. It defaults to Demo mode and does not sample inputs until POST /api/monitor with enabled=1. IAT/ECT/O2/MAP/TPS use GPIO0/1/2/3/4, RPM rising-edge counting uses GPIO5, and raw switch states use GPIO6/7 with no pull resistors enabled. Confirm conditioned wiring first. Inputs are measured at ESP pins: the interface scaling is not yet known, so simulator voltage, engineering units and RPM are not reconstructed. Near ADC saturation is marked CLIPPED; disconnected inputs can float. Frequency updates over one-second windows. Live CSV includes millivolts, Hz, switch levels and clipping flags, and is separate from simulated session records. Stop acquisition before returning to Demo mode. If stop cannot be confirmed, power-cycle the ESP32. Monitor cannot certify ECU/ABS operation. The board still runs the previously flashed simulation until this source is uploaded.
