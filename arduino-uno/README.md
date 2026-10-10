# Arduino Uno R3 testbench input monitor

Target: standard 5 V Uno R3 with ATmega328P, not Uno R4.

Open TestbenchUno/TestbenchUno.ino in Arduino IDE, select Arduino Uno and the actual port, upload, then open Serial Monitor at 115200 baud. No special USB CDC setting is needed for Uno R3. This package does not flash the board automatically.

The five analog signals use A0-A4. Their sensor order has not been individually confirmed, so output labels remain A0_V through A4_V. Connect simulator signal ground to Uno GND. Use USB power initially; do not connect the simulator's 12 V supply or its power terminals to analog or digital pins. The measured analog maxima 1.248, 3.385, 3.575, 4.25 and 4.26 V fit a nominal 5 V input range, provided the actual source stays within 0 V and the Uno's actual VCC and shares ground. Previous ESP32 divider requirements do not transfer unchanged to Uno R3. If a divider is fitted, reported values are pin voltage, not reconstructed simulator voltage.

Set ADC_REFERENCE_V to the measured Uno 5V-to-GND supply for a better voltage estimate. The ADC is 10-bit; printing three decimals does not establish millivolt accuracy. Averaging does not correct reference error or wiring faults. A disconnected analog input can float. There is no automatic sensor presence detector.

Terminal 3 / RPM frequency uses D7. D7 needs a pin-change interrupt on Uno R3; normal attachInterrupt does not support D7. This sketch implements PCINT23 without external libraries. RPM_INPUT_VERIFIED defaults to false: keep D7 disconnected until its waveform, amplitude and output type are known. A meter's average voltage is insufficient to establish pulse peaks. For a verified compatible 0-to-VCC pulse waveform, connect it with power off and set the flag true before uploading. Open-collector outputs may need an appropriate pull-up, not supplied by this code. Software does not protect pins from excessive or negative voltage.

The counter reports rising-edge Hz over approximately one second. Low frequencies have coarse count resolution. RPM = 60 × Hz / pulses per revolution only after the relationship is confirmed by the client. No RPM, temperature, pressure or AFR calibration is assumed.

Each serial JSON record contains uptime_ms, A0_V to A4_V, and rpm_hz (null while disabled). The analog channels are sampled sequentially and are not synchronized to the pulse window. The sketch does not control actuators, operate ABS, or prove ECU health. Uno R3 does not host the existing Wi-Fi dashboard by itself; a serial-connected computer or separate networking device is required for that integration.

References:
- https://docs.arduino.cc/hardware/uno-rev3/
- https://github.com/arduino/ArduinoCore-avr/blob/master/variants/standard/pins_arduino.h
