// ESP32 CAN Dashboard
//
// Last stage of the CAN sensor link:
//   Nano Sensor Simulator --CAN--> Nano CAN Gateway --serial--> this ESP32
// Reads the gateway's "CAN,..." lines on Serial2 and shows IAT, ECT, O2 / AFR,
// MAP and TPS on a Wi-Fi web page, with ERR and the DTC for a faulted sensor.
//
// Wiring (ESP32 WROOM-32 DevKit, 38 pins):
//   GPIO16 (RX2) <- 20k to GND, 10k from the gateway Nano's TX (D1)  (5 V -> 3.3 V)
//   GND          -- gateway Nano GND
//   USB          power + Serial Monitor (115200)
//
// Wi-Fi: join "ECU-CAN" (password ECUdemo2026), open http://192.168.4.1/
// /api/live returns the readings as JSON.
//
// Board: "ESP32 Dev Module" (esp32 core 3.x). No extra libraries.

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>

constexpr int PIN_RX2 = 16;
constexpr int PIN_TX2 = 17;  // Not used (nothing is sent to the gateway).
constexpr uint32_t LINK_BAUD = 115200;
constexpr uint32_t STALE_MS = 1000;  // No CAN line for this long = link down.
const char* AP_SSID = "ECU-CAN";
const char* AP_PASS = "ECUdemo2026";

// DTCs as on the testbench receivers: {low voltage, high voltage}.
const char* const SENSOR_NAME[5] = {"IAT", "ECT", "O2", "MAP", "TPS"};
const char* const DTC_LOW[5] = {"P0112", "P0117", "P0131", "P0107", "P0122"};
const char* const DTC_HIGH[5] = {"P0113", "P0118", "P0132", "P0108", "P0123"};

struct Live {
  int iat = 0, ect = 0, o2x100 = 0, map = 0, tps = 0;
  uint8_t hi = 0, lo = 0, counter = 0;
  uint32_t lastMs = 0, frames = 0, badLines = 0, lost = 0;
  bool gatewaySeen = false, gatewayNoLink = false;
} live;

WebServer server(80);

// "CAN,25,90,45,35,0,0,0,123"
void parseLine(char* line) {
  if (strcmp(line, "NOLINK") == 0) { live.gatewaySeen = true; live.gatewayNoLink = true; return; }
  if (strncmp(line, "CAN,", 4) != 0) return;  // "# ..." info lines and library messages.
  int v[8];
  if (sscanf(line + 4, "%d,%d,%d,%d,%d,%d,%d,%d", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &v[6], &v[7]) != 8) {
    live.badLines++;
    return;
  }
  const uint8_t expected = live.counter + 1;
  if (live.frames && uint8_t(v[7]) != expected) live.lost += uint8_t(v[7] - expected);
  live.iat = v[0]; live.ect = v[1]; live.o2x100 = v[2]; live.map = v[3]; live.tps = v[4];
  live.hi = v[5]; live.lo = v[6]; live.counter = v[7];
  live.lastMs = millis();
  live.frames++;
  live.gatewaySeen = true;
  live.gatewayNoLink = false;
}

void readLink() {
  static char buf[64];
  static uint8_t len = 0;
  while (Serial2.available()) {
    const char c = Serial2.read();
    if (c == '\r') continue;
    if (c == '\n') { buf[len] = '\0'; if (len) parseLine(buf); len = 0; }
    else if (len < sizeof(buf) - 1) buf[len++] = c;
    else len = 0;  // Line too long: drop it.
  }
}

float afr() { return 15.2f + (live.o2x100 / 100.0f - 0.1f) * (14.0f - 15.2f) / 0.8f; }

void handleLive() {
  const bool fresh = live.frames && millis() - live.lastMs < STALE_MS;
  String j;
  j.reserve(400);
  j += F("{\"link\":"); j += fresh ? F("true") : F("false");
  j += F(",\"state\":\"");
  j += fresh ? F("OK") : live.gatewayNoLink ? F("NO CAN FRAMES") : live.gatewaySeen ? F("GATEWAY SILENT") : F("NO GATEWAY");
  j += F("\",\"age\":"); j += live.frames ? millis() - live.lastMs : 0;
  j += F(",\"frames\":"); j += live.frames;
  j += F(",\"lost\":"); j += live.lost;
  j += F(",\"bad\":"); j += live.badLines;
  j += F(",\"iat\":"); j += live.iat;
  j += F(",\"ect\":"); j += live.ect;
  j += F(",\"o2\":"); j += String(live.o2x100 / 100.0f, 2);
  j += F(",\"afr\":"); j += String(afr(), 1);
  j += F(",\"map\":"); j += live.map;
  j += F(",\"tps\":"); j += live.tps;
  j += F(",\"fault\":[");
  for (int i = 0; i < 5; i++) {
    if (i) j += ',';
    j += (live.hi >> i) & 1 ? F("\"HIGH\"") : (live.lo >> i) & 1 ? F("\"LOW\"") : F("\"\"");
  }
  j += F("],\"dtc\":[");
  bool first = true;
  for (int i = 0; i < 5; i++) {
    const char* code = (live.hi >> i) & 1 ? DTC_HIGH[i] : (live.lo >> i) & 1 ? DTC_LOW[i] : nullptr;
    if (!code) continue;
    if (!first) j += ',';
    first = false;
    j += '"'; j += code; j += '"';
  }
  j += F("]}");
  server.send(200, "application/json", j);
}

const char PAGE[] PROGMEM = R"HTML(<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>ECU CAN Dashboard</title>
<style>
:root{--bg:#0f1216;--card:#181d24;--line:#2a313b;--text:#e6e9ee;--dim:#8a94a3;--ok:#3ecf8e;--bad:#ff5d5d;--warn:#f5b942}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--text);font:15px/1.4 system-ui,Segoe UI,Roboto,sans-serif}
header{display:flex;align-items:center;justify-content:space-between;padding:14px 18px;border-bottom:1px solid var(--line)}
h1{font-size:17px;margin:0;letter-spacing:.04em}
#state{font-weight:600;padding:4px 10px;border-radius:999px;background:#222a33}
main{padding:18px;display:grid;gap:14px;grid-template-columns:repeat(auto-fit,minmax(170px,1fr));max-width:1000px;margin:auto}
.card{background:var(--card);border:1px solid var(--line);border-radius:12px;padding:14px 16px}
.name{color:var(--dim);font-size:13px;letter-spacing:.08em}
.val{font-size:38px;font-weight:700;font-variant-numeric:tabular-nums;margin:4px 0 2px}
.unit{font-size:16px;color:var(--dim);font-weight:500;margin-left:4px}
.sub{color:var(--dim);font-size:13px;min-height:18px}
.err .val{color:var(--bad)}.err{border-color:#5a2a2a}
#dtc{grid-column:1/-1}#dtc .val{font-size:22px}
footer{color:var(--dim);font-size:12px;text-align:center;padding:4px 18px 18px}
</style></head><body>
<header><h1>ECU CAN DASHBOARD</h1><span id="state">…</span></header>
<main>
<div class="card" id="c0"><div class="name">IAT · INTAKE AIR</div><div class="val"><span id="v0">–</span><span class="unit">°C</span></div><div class="sub" id="s0"></div></div>
<div class="card" id="c1"><div class="name">ECT · COOLANT</div><div class="val"><span id="v1">–</span><span class="unit">°C</span></div><div class="sub" id="s1"></div></div>
<div class="card" id="c2"><div class="name">O2 · OXYGEN</div><div class="val"><span id="v2">–</span><span class="unit">V</span></div><div class="sub" id="s2"></div></div>
<div class="card" id="c3"><div class="name">MAP · MANIFOLD</div><div class="val"><span id="v3">–</span><span class="unit">kPa</span></div><div class="sub" id="s3"></div></div>
<div class="card" id="c4"><div class="name">TPS · THROTTLE</div><div class="val"><span id="v4">–</span><span class="unit">%</span></div><div class="sub" id="s4"></div></div>
<div class="card" id="dtc"><div class="name">DTC</div><div class="val" id="dtcv">–</div></div>
</main>
<footer id="foot">Nano simulator → CAN 500 kbps → Nano gateway → ESP32</footer>
<script>
const st=document.getElementById('state');
async function tick(){
  try{
    const d=await (await fetch('/api/live',{cache:'no-store'})).json();
    st.textContent=d.state;st.style.color=d.link?'var(--ok)':'var(--bad)';
    const vals=[d.iat,d.ect,d.o2.toFixed(2),d.map,d.tps];
    for(let i=0;i<5;i++){
      const f=d.fault[i];
      document.getElementById('c'+i).classList.toggle('err',!!f);
      document.getElementById('v'+i).textContent=!d.frames?'–':f?'ERR':vals[i];
      document.getElementById('s'+i).textContent=f?('circuit '+f):(i==2&&d.frames?'AFR '+d.afr.toFixed(1)+':1':'');
    }
    const dv=document.getElementById('dtcv');
    dv.textContent=d.dtc.length?d.dtc.join('  '):'none';dv.style.color=d.dtc.length?'var(--warn)':'var(--ok)';
    document.getElementById('foot').textContent='frames '+d.frames+' · lost '+d.lost+' · last '+d.age+' ms ago · Nano simulator → CAN 500 kbps → Nano gateway → ESP32';
  }catch(e){st.textContent='ESP32 OFFLINE';st.style.color='var(--bad)'}
}
setInterval(tick,250);tick();
</script></body></html>)HTML";

void setup() {
  Serial.begin(115200);
  Serial2.begin(LINK_BAUD, SERIAL_8N1, PIN_RX2, PIN_TX2);
  Serial.println("# ESP32 CAN DASHBOARD (gateway on GPIO16)");

  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS);
  Serial.printf("# Wi-Fi: %s  http://%s/\n", AP_SSID, WiFi.softAPIP().toString().c_str());

  server.on("/", [] { server.send_P(200, "text/html", PAGE); });
  server.on("/api/live", handleLive);
  server.begin();
}

void loop() {
  static uint32_t lastPrintMs = 0;
  readLink();
  server.handleClient();

  if (millis() - lastPrintMs >= 1000) {  // Once a second on the USB Serial Monitor.
    lastPrintMs = millis();
    const bool fresh = live.frames && millis() - live.lastMs < STALE_MS;
    if (!fresh) { Serial.println(live.gatewayNoLink ? "# Gateway: NO CAN FRAMES" : "# No data from the gateway on GPIO16"); return; }
    Serial.printf("IAT %d C | ECT %d C | O2 %.2f V (AFR %.1f) | MAP %d kPa | TPS %d %%",
                  live.iat, live.ect, live.o2x100 / 100.0f, afr(), live.map, live.tps);
    for (int i = 0; i < 5; i++) {
      if ((live.hi >> i) & 1) Serial.printf(" | %s ERR %s", SENSOR_NAME[i], DTC_HIGH[i]);
      else if ((live.lo >> i) & 1) Serial.printf(" | %s ERR %s", SENSOR_NAME[i], DTC_LOW[i]);
    }
    Serial.printf(" | frames %lu lost %lu\n", (unsigned long)live.frames, (unsigned long)live.lost);
  }
}
