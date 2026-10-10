# Uno to NodeMCU ESP8266 telemetry

## Wiring first

User reports D5-to-D5, D6-to-D6 and common GND, without a divider.
Disconnect D6 until voltage protection is installed. The Uno transmitter
uses 5 V logic; NodeMCU D6 is a 3.3 V input. Do not activate the sender
on the direct wire. A signal wire should also be disconnected while either
board is unpowered, to avoid feeding an unpowered board through its GPIO.

| Uno | Connection | NodeMCU |
|---|---|---|
| D6 TX | 5 V-to-3.3 V level shifter or divider | D6 / GPIO12 RX |
| GND | Direct common ground | GND |
| D5 | Unused; may disconnect | D5 / GPIO14 unused |

For a resistor divider, connect Uno D6 to a 10 kOhm resistor. Its other
end is the junction. Connect the junction to NodeMCU D6 and to a second
10 kOhm resistor. Connect the other end of the second resistor to common
GND. This produces approximately 2.5 V from a 5 V HIGH, suitable for the
ESP8266 digital input. Do not connect NodeMCU D6 on the Uno side of the
first resistor. A 5 V-to-3.3 V UART-compatible level shifter is another option.
No protection is supplied by firmware or a normal direct jumper wire.

Power each board through USB. Do not join their 5 V/3.3 V power pins.
Keep all existing Uno analog and D7 frequency connections unchanged.

## Firmware

### Smart key and brake buttons

- Connect a normally-open momentary button between Uno D8 and GND.
  INPUT_PULLUP is enabled: pressed = LOW. Each debounced press toggles
  smart-key ON/OFF; release does not change the key state. Boot/reset always
  starts OFF. A button held during boot does not unlock until released and
  pressed again. This is a software enable switch, not secure authentication.
- D9 uses the existing active-high brake wiring after a reported polarity
  reversal: released LOW, pressed HIGH. INPUT_PULLUP is retained, matching
  a normally-closed contact to GND that opens when pressed. A disconnected
  D9 wire therefore reports brake pressed. Verify the switch contact type.
  Hold to ramp mapped RPM toward zero at 100 display units per second;
  300 to zero takes about 3 seconds. Release ramps back toward the current
  frequency mapping at the same rate. Early release reverses smoothly.
  Both inputs use 30 ms software debounce.
- While key OFF, Uno skips analogRead, ignores D7 pulses and sends a locked
  heartbeat (sensors=null) so the receiver distinguishes locked from offline.
  Turning key ON resets the frequency window. First complete frequency
  reading takes approximately one second. Reset/reopening Uno serial may
  reset the board and lock acquisition again.
- While brake is held, other sensors and raw D7 Hz remain live. No motor,
  testbench waveform, injection, ignition or physical brake output is driven.
- Dashboard key/brake indicators reflect hardware states. UI response adds
  roughly one polling interval to debounce/serial time. Stale states clear
  the indicators. CSV records include smart_key and brake_pressed.

Dashboard LED colors: Smart key is green while ON, gray while OFF or data
is unavailable. ABS is orange while brake is held, green while released
with smart key ON, gray while key is OFF or data is stale/unavailable.
The ABS LED is derived from the brake button; it does not measure ABS health,
wheel slip or ABS hydraulic activation. The instrument layout is unchanged.

If using a four-legged tactile button, use contacts on opposite switched
sides; legs on the same side may already be internally connected. No 5 V
wire is needed at either button. A maintained switch has different semantics
from this specified momentary toggle behavior.

- Uno: ../arduino-uno/DashboardTelemetry/DashboardTelemetry.ino plus both
  header files in that folder. ESP_LINK_ENABLED in SerialLink.h is now
  true at the user's explicit request. Direct 5 V wiring risks damage;
  verified level conditioning is still recommended. Enabled firmware was
  uploaded successfully to Uno COM5 on October 7, 2026. NodeMCU COM7
  received live sensor packets with stale=false and bad_frames=0 during
  the verification sample. This confirms data transfer, not voltage safety.
- NodeMCU: UnoReceiver/UnoReceiver.ino and its SensorConversion.h.
- Build target Uno: arduino:avr:uno.
- Build target NodeMCU: esp8266:esp8266:nodemcuv2 (NodeMCU 1.0 ESP-12E).

Receiver uses NodeMCU D6 as RX, not D5. Pin labels need not be crossed
because Uno D6 is assigned TX and NodeMCU D6 is assigned RX. Only this
one-way connection is required. Neither D5 is used to transmit.

Uno USB output remains at 115200 baud with JSON and readable # lines.
NodeMCU USB output is also 115200 baud. The board-to-board UART is
19200 baud, 8N1. Uno Timer2 implements transmit to preserve D7 interrupt
counting; do not use tone() or Timer2 PWM on D3/D11 in the same sketch.

## Packet format

U3,key,brake,iat16,ect16,o216,map16,tps16,hz100,rpm100*XX followed by newline.
key and brake are 0 or 1. Both Uno and receiver must use the U3 firmware.
rpm100 is the ramped mapped RPM multiplied by 100 (0 to 30000); the Uno
owns the ramp so USB telemetry and the ESP8266 API agree. During a transition,
Uno publishes about every 200 ms. The browser animates between packets.
The physical knob indicator follows raw frequency even while braking.
Smart-key OFF/reset still locks acquisition immediately.
Raw ADC averages are multiplied by 16, frequency is multiplied by 100.
XX is a two-digit hexadecimal XOR of the characters before the asterisk.
Receiver rejects bad checksum, invalid fields and overlong frames.
The sender includes only measurements; receiver reconstructs voltage,
knob percentages, provisional estimates and the requested mapped RPM.

## Receiving and dashboard integration

After flashing NodeMCU, join Wi-Fi ECU-Testbench-8266, password ECUdemo2026.
Open http://192.168.4.1/api/live for the latest telemetry JSON.
The root page now serves the latest instrument dashboard adapted to live
Uno data. Its compressed HTML/CSS/JS assets are embedded in firmware.
No external internet connection or CDN is required. At the user's request,
the original instrument layout is restored with a central mapped RPM gauge
and a right-hand vertical sensor column ordered IAT, ECT, O2, MAP, TPS.
Sensor tiles show voltage, except TPS which shows endpoint-based percentage.
Latest user-requested presentation: the IAT label is AIT. All five sensor
tiles now show 0–100% zero-based knob position from measured endpoints,
with a 2% deadband at each endpoint. This is not physical temperature,
pressure or oxygen concentration. Raw ADC and voltage remain in the table.
The central 0–300 display is labeled simulated km/h, not measured road
speed; it retains the previous frequency mapping and brake ramp. Internal
IAT and engine_rpm transport fields are preserved for firmware compatibility.
A separate table
shows raw ADC, knob position and clearly marked provisional estimates.
The central gauge and read-only physical-knob indicator use mapped RPM.
Unwired indicators appear on the left and battery/references below the
gauge, labeled unavailable. Simulation/engine/brake controls and road speed
are removed.

Signals retains up to 120 poll samples with gaps for missing data. Results
records up to 3600 fresh samples in browser memory, exports CSV and prints
a session summary. Export before refreshing; data is not saved on the ESP.
The page polls /api/live every 400 ms plus request time, with a 1.8-second request
timeout. Brief network errors show RECONNECTING while retaining a sample
only until its 3-second source-age deadline. Genuine stale data, deadline
expiry and valid key-OFF packets clear readings. RPM animation avoids
rewriting unchanged values and pauses on hidden pages. History is sampled
once per second; CSV deduplicates received sample_seq values. Receiver USB
logging is limited to once per second; input packets are still all processed.
/api/health exposes uptime, free heap, reset reason and bad-frame count.
Recovery tests cover transient failure, age expiry, fresh recovery, duplicate
recording and immediate key OFF. A short pre-update serial capture found
no restart messages; this does not establish the user's reload root cause.
The receiver never generates simulated samples.

Before any valid packet, sensors is null and stale is true. After 3 seconds
without a valid packet stale becomes true, while retaining the last sample
and its age. Dashboard code must visibly mark stale values and not present
them as fresh data. bad_frames counts rejected frames.

RPM is a display mapping 0-1639 Hz to 0-300, not calibrated engine RPM.
Road speed is removed. O2 is voltage only, AFR remains unavailable.
Temperature/pressure estimates are provisional; true calibrated values
remain null. Voltages assume a nominal 5.000 V Uno ADC reference.

Dashboard source: dashboard/dist. Rebuild embedded DashboardAssets.h using
work/embed-esp8266-dashboard.py from the workspace, then compile/flash the
NodeMCU receiver. The original ESP32 simulation project is preserved.
Local browser QA used a captured actual packet, then a stale-packet case;
recording, history, layout and stale clearing were checked. Direct browser
access over the NodeMCU Wi-Fi must be verified on a connected client.
# Simulated check-engine behavior

The ESP8266 checks fresh, key-enabled sensor packets against measured simulator knob endpoints. A signal more than 5% beyond either endpoint for 2 seconds activates an orange simulated check-engine indicator with the affected sensor names. Active faults use 3% tolerance and require 3 seconds of recovery. Full throttle and normal knob endpoints are valid; missing provisional temperature/pressure conversions alone are not faults. These demonstration limits are not manufacturer DTC criteria. Overheating detection remains pending a verified ECT curve and model-specific threshold.

During a fault the ESP8266 ramps `simulated_speed_kmh` toward zero at 100 units per second, and ramps back to the Uno mapped value after recovery. Raw `frequency_hz` and the Uno `engine_rpm` transport field remain unchanged. The dashboard gauge, history and CSV use the fault-limited speed. Key OFF or stale telemetry resets the fault monitor. No physical engine or ABS actuation occurs.

## Current demo controls (October 10, 2026)
Automatic endpoint faults are disabled. Check engine and gauge limiting respond only to the injected fault set by POST /api/simulate-fault. Clear removes it immediately regardless of measured sensor values. Key OFF, stale telemetry and reboot also clear injection. Mobile screens up to 600px reflow the gauge above indicators and sensors, with full-width touch controls; desktop content is preserved.

