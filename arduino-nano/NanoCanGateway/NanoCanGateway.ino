// Nano CAN Gateway
//
// Second Nano of the CAN sensor link. Receives the sensor frame (ID 0x100)
// from the Nano Sensor Simulator over CAN (MCP2515, 500 kbps, 8 MHz crystal)
// and forwards it as one text line on the serial TX pin to the ESP32
// (EspCanDashboard), which shows it on a Wi-Fi web page.
//
//   Nano Sensor Simulator --CAN--> Nano CAN Gateway --serial--> ESP32
//
// Pins:
//   D10  OUT  MCP2515 CS
//   D11  OUT  MCP2515 SI  (SPI MOSI)
//   D12  IN   MCP2515 SO  (SPI MISO)
//   D13  OUT  MCP2515 SCK
//   D2   IN   MCP2515 INT (LOW = frame waiting)
//   D1   OUT  Serial TX 115200 -> 10k / 20k divider -> ESP32 GPIO16 (RX2)
//   GND       common ground with the ESP32 and the simulator Nano
//
// Lines sent (also visible in the Serial Monitor at 115200):
//   CAN,<IAT C>,<ECT C>,<O2 V x100>,<MAP kPa>,<TPS %>,<HIGH bits>,<LOW bits>,<counter>
//   NOLINK            no frame for 500 ms (sent every 500 ms)
//   # ...             information, ignored by the ESP32
//
// Board: Arduino Nano (ATmega328P). Library: "mcp_can" by coryjfowler.

#include <Arduino.h>
#include <SPI.h>
#include <mcp_can.h>

constexpr uint8_t PIN_CAN_CS = 10;
constexpr uint8_t PIN_CAN_INT = 2;
constexpr uint32_t CAN_ID_SENSORS = 0x100;
constexpr uint8_t CAN_CRYSTAL = MCP_8MHZ;  // Crystal on the MCP2515 module ("8.000").
constexpr uint16_t LINK_TIMEOUT_MS = 500;
constexpr uint16_t CAN_RETRY_MS = 2000;

MCP_CAN can(PIN_CAN_CS);
bool canReady = false;

void canBegin() {
  canReady = can.begin(MCP_ANY, CAN_500KBPS, CAN_CRYSTAL) == CAN_OK;
  if (canReady) can.setMode(MCP_NORMAL);
  Serial.println(canReady ? F("# CAN MCP2515 OK, 500 kbps, 8 MHz crystal")
                          : F("# CAN MCP2515 not found (check CS D10, SI D11, SO D12, SCK D13, 5V)"));
}

void setup() {
  Serial.begin(115200);
  Serial.println(F("# NANO CAN GATEWAY (CAN ID 0x100 -> serial)"));
  pinMode(PIN_CAN_INT, INPUT);
  canBegin();
}

void loop() {
  static uint32_t lastFrameMs = 0, lastNoLinkMs = 0, lastRetryMs = 0;
  static bool linked = false;
  const uint32_t now = millis();

  if (!canReady) {
    if (now - lastRetryMs >= CAN_RETRY_MS) { lastRetryMs = now; canBegin(); }
  } else {
    while (digitalRead(PIN_CAN_INT) == LOW || can.checkReceive() == CAN_MSGAVAIL) {
      uint32_t id;
      uint8_t len, buf[8];
      if (can.readMsgBuf(&id, &len, buf) != CAN_OK) break;
      if ((id & 0x1FFFFFFF) != CAN_ID_SENSORS || len < 8) continue;
      lastFrameMs = now;
      if (!linked) { linked = true; Serial.println(F("# LINK: frames from the simulator")); }
      Serial.print(F("CAN,"));
      Serial.print(int(buf[0]) - 40); Serial.print(',');
      Serial.print(int(buf[1]) - 40); Serial.print(',');
      for (uint8_t i = 2; i < 8; i++) { Serial.print(buf[i]); Serial.print(i < 7 ? ',' : '\n'); }
    }
  }

  if (now - lastFrameMs >= LINK_TIMEOUT_MS && now - lastNoLinkMs >= LINK_TIMEOUT_MS) {
    lastNoLinkMs = now;
    if (linked) { linked = false; Serial.println(F("# LINK LOST")); }
    Serial.println(F("NOLINK"));
  }
}
