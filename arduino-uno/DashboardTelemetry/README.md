# Uno dashboard telemetry

Open DashboardTelemetry.ino with SensorConversion.h, SerialLink.h and
Buttons.h and RpmRamp.h in the same folder.
Board: Arduino Uno R3. Serial: 115200 baud. One JSON object per line,
approximately once per second, followed by readable lines prefixed with #.
This replaces the raw-monitor sketch. Both formats use the same sensor snapshot.

Wiring: A0 IAT, A1 ECT, A2 O2, A3 MAP, A4 TPS, D7 frequency,
with common simulator/Uno ground. D7 uses rising-edge pin-change interrupts
and no internal pull-up, matching the latest raw monitor.

D8 smart-key button: normally-open momentary contact to GND, INPUT_PULLUP.
Each debounced press toggles acquisition ON/OFF; startup/reset is OFF.
No analog readings or frequency accumulation take place while locked.
D9 brake polarity corrected to match reported existing wiring: released
LOW, pressed HIGH, with INPUT_PULLUP retained. This matches a normally-closed
contact to GND that opens when pressed. Verify actual switch contact type;
do not change wiring to a normally-open contact without changing polarity.
While held, mapped RPM ramps down at 100 display units/second; release
ramps toward the current frequency mapping at the same rate. Raw frequency
and other sensors remain live when key is ON. Full-scale stop takes 3 seconds.
Both buttons use 30 ms debounce. They change telemetry, not physical motor
or brake outputs. Locked heartbeat packets have sensors=null and explicit
smart_key/acquisition_enabled/brake_pressed states. Uno-to-ESP packets use
U3, including ramped RPM, so update both boards together. Link TX is enabled
as requested. Ramp endpoint and early-release tests pass.

Validation: both targets compiled and flashed. Portable tests of Buttons.h
passed bounce, hold, release, boot-held and clock-wrap cases. Dashboard
locked/brake-held/released states were checked with local QA fixtures.
Physical receiver verification confirmed key OFF, acquisition false,
sensors null, brake true and zero RPM with no rejected packets. Complete
the physical D8 toggle / D9 hold-release test on the connected hardware.

## Measured knob endpoints

| Sensor | Knob zero, raw | Knob maximum, raw |
|---|---:|---:|
| IAT | 708.3 | 343.4 |
| ECT | 747.2 | 61.0 |
| O2 | 0 | 257.8 |
| MAP | 78.5 | 889.0 |
| TPS | 76 | 893 |

Endpoint averages come from the user's zero/maximum logs. These establish
knob travel, not temperature/pressure calibration. No persistent EEPROM
calibration is performed. Readings are averaged over 16 ADC conversions.

## Dashboard fields

Ignore lines starting with #. Parse each complete line starting with { as JSON.
Discard incomplete lines after opening or resetting the serial connection.
Read sensors.IAT, sensors.ECT,
sensors.O2, sensors.MAP and sensors.TPS. Each includes raw, voltage_v,
knob_percent and endpoint_status. Knob percentage is clamped only within
a 1 percentage-point endpoint tolerance; larger excursions produce null
and out_of_range while preserving raw and voltage.

TPS.throttle_percent is an endpoint-based simulator percentage, not a
verified ECU throttle-angle measurement. O2 is voltage only; afr is null.

IAT.temperature_c, ECT.temperature_c and MAP.pressure_kpa are null until
calibrated. Separate estimate_c / estimate_kpa fields use provisional
linear curves from the original client requirements:

- IAT: 3.00 V -> 30 C; 1.72 V -> 50 C.
- ECT: 3.00 V -> 20 C; 0.43 V -> 100 C.
- MAP: 0.78 V -> 20 kPa; 4.00 V -> 101.3 kPa.

These curves disagree with parts of the measured knob ranges. Estimates
outside their voltage bounds are null, with no extrapolation. Temperature
curves may be nonlinear. Display these estimates with a visible PROVISIONAL
label, and null values as unavailable rather than zero.

frequency_hz is measured from D7. At the user's request, engine_rpm is now
a mapped display value: constrain(frequency_hz * 300 / 1639, 0, 300).
Thus 0 Hz -> 0, 819.5 Hz -> 150, and 1639 Hz -> 300. This is not a
physical engine-RPM calibration. rpm_calibration identifies this mapping;
the readable output explicitly says Mapped RPM. Dashboard consumers must
preserve that distinction. Road speed has been removed from both outputs.

Voltages assume nominal 5.000 V ADC reference; voltage_reference explicitly
reports nominal_unverified. Measure Uno VCC and update ADC_REFERENCE_V
for better voltage accuracy. ADC attenuation/scaling is not performed.

This firmware provides serial telemetry, not Wi-Fi or an automatic link
to the existing ESP32 dashboard. A PC serial bridge or a properly
level-shifted Uno-to-ESP32 connection is still needed for that dashboard.
Never connect a 5 V Uno TX directly to an ESP32 GPIO.

On the dashboard, mark data stale if no new line arrives for 3 seconds.
The only process opening COM5 should be the intended serial consumer;
close Serial Monitor before uploading or connecting a serial bridge.
