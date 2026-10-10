# Complete motorcycle monitor for Uno R3

Open MotorcycleMonitor/MotorcycleMonitor.ino, select Arduino Uno and the correct port, upload, then use Serial Monitor at 115200 baud. This sketch does not host a Wi-Fi dashboard and has not been automatically flashed.

Frequency defaults to D7 to match the client's stated wiring. If rewired to D2, set FREQUENCY_PIN = 2; both paths are implemented. Keep the physical wire and code setting consistent. Confirm the pulse stays within ground and actual Uno VCC, without negative excursions, before connecting. The internal pull-up defaults on to match the user's working sketch; verify the source is compatible with a pull-up to Uno VCC. A frequency reading does not verify pulse voltage. Do not connect 12 V to GPIOs. Connect simulator ground to Uno GND and use USB power initially.

The user confirmed A0=IAT, A1=ECT, A2=O2, A3=MAP and A4=TPS. Voltages assume ADC_REFERENCE_V, initially 5.000 V; set it to measured Uno supply voltage. Averages do not establish millivolt accuracy. INPUT_SCALE defaults to 1; use 2 only on a channel with a verified equal divider. Disconnected inputs float.

Provisional linear conversions from the original client document are now printed: IAT 3.0V=30C / 1.72V=50C; ECT 3.0V=20C / 0.43V=100C; MAP 0.78V=20kPa / 4.0V=101.3kPa; TPS 0.6V=0% / 3.5V=100%. Every converted result is labeled PROVISIONAL. These endpoints do not establish actual temperature curves, and the TPS percent mapping is a provisional normalization of the document's voltage range. Input values outside each range are explicitly flagged, not extrapolated or clamped. All raw and voltage data is preserved. O2 remains voltage only; no AFR conversion is performed.

The latest all-knobs-maximum snapshot (A0=.084V, A1=.053V, A2=1.246V, A3=.738V, A4=.734V, frequency=1632.99Hz) is retained as observed data, not new calibration endpoints. With the provisional client conversions, IAT/ECT/MAP in that snapshot are out of range, O2=1.246V, and TPS is about 4.6%. This discrepancy remains visible and cannot be corrected simply by naming that snapshot 'maximum'.

SIGNAL_MODE choices:
- ENGINE_SIGNAL (default): Hz and engine RPM if ENGINE_PULSES_PER_REVOLUTION is supplied; no road-speed conversion.
- WHEEL_SIGNAL: use only for an identified wheel signal with confirmed WHEEL_PULSES_PER_REVOLUTION. Output is an estimate based on nominal tire geometry unless actual rolling circumference is supplied.
- WHEEL_DEMO: treat the bench pulse stream as a hypothetical wheel signal. Uses an explicitly illustrative assumption of 48 pulses per wheel revolution, not a verified NMAX/ADV specification. Outputs are labeled DEMO and cannot be used as calibrated road speed or ECU/ABS validation.

Profiles: NMAX_FRONT 110/70-13, NMAX_REAR 130/70-13, ADV160_FRONT 110/80-14, ADV160_REAR 130/70-13. The rear profiles share the same nominal geometry. Confirm model/year and installed tires. Actual loaded rolling circumference varies; set MEASURED_WHEEL_CIRCUMFERENCE_M if measured.

Sources for listed tire sizes:
- https://yamaha-motor.com.my/wp-content/uploads/2020/11/BBM1-NMAX-155-All-2.pdf
- https://powersports.honda.com/motorcycle/scooter/adv160/2026/adv160

The client identified terminal 3 as RPM, so default mode remains engine signal. The original 300 Hz to 15000 RPM display request is not enough to establish a genuine crank pulse relationship. Obtain a mapping from the client instead of inventing one. Simple edge counting over about one second has coarse low-frequency resolution and may count noise as pulses. Sampling is sequential, not simultaneous. No actuators are driven by this sketch.
