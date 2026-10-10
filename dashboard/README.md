# ECU and ABS reference dashboard

The layout follows the supplied sketch: sensor readings on the left, a large numeric speed and semicircular dial in the middle, circular indicators on the right, and reference/battery voltage readings along the bottom. The ABS view shows front wheel speed, rear wheel speed bar, and a duty-cycle placeholder.

All readings are simulated. The established provisional speed limit remains 250 km/h and RPM limit 15,000. The reference sketch's unlabeled LED functions have not been invented. Battery current and duty cycle show placeholders until their input definitions are supplied.

Open dist/index.html for the offline version. Use ECU display / ABS display to switch views. Drag the simulation knob up/down, use arrow keys when focused, or tap +/−. Indicators can be toggled and Reset clears speed and indicators. Sensor values follow the knob using illustrative relationships.

On the flashed ESP32-C6, connect to ECU-Testbench-C6 with password ECUdemo2026 and open http://192.168.4.1/. Refresh after firmware updates. No physical testbench inputs are connected.

## Engine and brake simulation

The dashboard starts with the engine off and RPM at zero. Start Engine brings it to idle. The knob selects a target speed and readings change gradually as the simulation accelerates. Brake is a toggle: it slows to zero, clears the target speed, and prevents knob changes until Release Brake is clicked. Releasing the brake leaves the engine idling. Stop Engine clears speed and target speed. Reset turns the engine off, clears indicators, and restores the initial temperature. All controls affect simulated data only; no real engine or GPIO is controlled. The browser calculates the demonstration dynamics and sends readings to the ESP32 manual telemetry endpoint every 500 ms.

## Guided demo upgrade
Dashboard, Test, Signals and Results tabs provide independent simulated sensor overrides, six guided scenarios, 120-second signal history, timestamped events, CSV recording and printable session reports. AFR uses independently simulated lambda multiplied by 14.7, not raw oxygen-sensor voltage. Results explicitly state insufficient data for a module verdict. Recording is limited to 3600 samples; browser refresh clears the session, so export before closing. Reset clears simulation settings but retains session evidence. All inputs and indicators remain simulated. Updated ESP32 source is provided separately and must be flashed to update the board.
