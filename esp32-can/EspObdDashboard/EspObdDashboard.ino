// ESP32 OBD-II Dashboard (Set 2: diagnostic tool bench, display side)
//
// The screen of the scan tool. The Nano OBD-II Tester talks CAN to the ECU
// simulator and sends its answers here as text lines; this ESP32 decodes them
// and shows a scan-tool page over Wi-Fi: live data, MIL, stored DTCs and the
// Read DTC / Clear DTC buttons (sent back to the Nano as READ / CLEAR).
//
//   ECU simulator (Set 1) --CAN--> Nano OBD-II Tester --38400--> this ESP32
//
// Wiring (ESP32 WROOM-32 DevKit, 38 pins):
//   GPIO16 (RX2) <- 20k to GND, 10k from the Nano's D9  (5 V -> 3.3 V)
//   GPIO17 (TX2) -> Nano D8 (direct; 3.3 V is a valid HIGH for the Nano)
//   GND          -- Nano GND
//   USB          power + Serial Monitor (115200)
//
// Wi-Fi: join "ECU-OBD" (password ECUdemo2026), open http://192.168.4.1/
//   /api/live       readings as JSON
//   /api/cmd?c=read | clear
//
// Board: "ESP32 Dev Module" (esp32 core 3.x). No extra libraries.

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>

constexpr int PIN_RX2 = 16;
constexpr int PIN_TX2 = 17;
constexpr uint32_t LINK_BAUD = 38400;
constexpr uint32_t ECU_STALE_MS = 1500;    // No OBD answer for this long = ECU offline.
constexpr uint32_t TESTER_STALE_MS = 1500; // No line at all for this long = tester offline.
const char* AP_SSID = "ECU-OBD";
const char* AP_PASS = "ECUdemo2026";

struct Live {
  // Mode 01 values, decoded. valid* = seen at least once.
  int ect = 0, iat = 0, map = 0;
  float tps = 0, o2 = 0;
  bool validEct = false, validIat = false, validMap = false, validTps = false, validO2 = false;
  bool mil = false;
  int dtcCount = 0;
  uint32_t supported = 0;
  // Mode 03.
  String dtcs = "";        // "P0113,P0122"
  String pend = "";        // Mode 07 pending DTCs, same format.
  bool dtcRead = false;
  uint32_t dtcMs = 0;
  // Status.
  uint32_t lastAnswerMs = 0, lastLineMs = 0, answers = 0, noResponse = 0;
  String lastEvent = "";
  uint32_t lastEventMs = 0;
} live;

WebServer server(80);

void event(const String& text) {
  live.lastEvent = text;
  live.lastEventMs = millis();
  Serial.println("# " + text);
}

// Splits "A,B,C" into fields (in place).
int splitFields(char* s, char** out, int max) {
  int n = 0;
  out[n++] = s;
  for (char* p = s; *p && n < max; p++)
    if (*p == ',') { *p = '\0'; out[n++] = p + 1; }
  return n;
}

void parseLine(char* line) {
  live.lastLineMs = millis();
  if (line[0] == '#') return;  // Information from the Nano.
  char* f[12];
  const int n = splitFields(line, f, 12);

  if (strcmp(f[0], "PID") == 0 && n >= 6) {
    const int pid = strtol(f[1], nullptr, 16);
    const int a = atoi(f[2]), b = atoi(f[3]), c = atoi(f[4]), d = atoi(f[5]);
    live.lastAnswerMs = millis();
    live.answers++;
    switch (pid) {
      case 0x00: live.supported = (uint32_t(a) << 24) | (uint32_t(b) << 16) | (uint32_t(c) << 8) | d; break;
      case 0x01: live.mil = a & 0x80; live.dtcCount = a & 0x7F; break;
      case 0x05: live.ect = a - 40; live.validEct = true; break;
      case 0x0B: live.map = a; live.validMap = true; break;
      case 0x0F: live.iat = a - 40; live.validIat = true; break;
      case 0x11: live.tps = a * 100.0f / 255.0f; live.validTps = true; break;
      case 0x14: live.o2 = a / 200.0f; live.validO2 = true; break;
    }
  } else if (strcmp(f[0], "DTC") == 0 && n >= 2) {
    live.lastAnswerMs = millis();
    live.answers++;
    String list;
    for (int i = 2; i < n; i++) { if (i > 2) list += ','; list += f[i]; }
    if (list != live.dtcs || !live.dtcRead) event(list.length() ? "DTCs: " + list : String("No DTCs stored"));
    live.dtcs = list;
    live.dtcRead = true;
    live.dtcMs = millis();
  } else if (strcmp(f[0], "PEND") == 0 && n >= 2) {
    live.lastAnswerMs = millis();
    String list;
    for (int i = 2; i < n; i++) { if (i > 2) list += ','; list += f[i]; }
    if (list != live.pend && list.length()) event("Pending: " + list);
    live.pend = list;
  } else if (strcmp(f[0], "CLR") == 0) {
    live.lastAnswerMs = millis();
    event("DTCs cleared");
  } else if (strcmp(f[0], "NEG") == 0 && n >= 3) {
    event(String("ECU refused mode ") + f[1] + " (code " + f[2] + ")");
  } else if (strcmp(f[0], "NORESP") == 0) {
    live.noResponse++;
  }
}

void readLink() {
  static char buf[96];
  static uint8_t len = 0;
  while (Serial2.available()) {
    const char c = Serial2.read();
    if (c == '\r') continue;
    if (c == '\n') { buf[len] = '\0'; if (len) parseLine(buf); len = 0; }
    else if (len < sizeof(buf) - 1) buf[len++] = c;
    else len = 0;  // Line too long: drop it.
  }
}

float afr() { return 15.2f + (live.o2 - 0.1f) * (14.0f - 15.2f) / 0.8f; }

String jsonNum(bool valid, float v, int decimals) { return valid ? String(v, decimals) : String("null"); }

void handleLive() {
  const uint32_t now = millis();
  const bool ecu = live.answers && now - live.lastAnswerMs < ECU_STALE_MS;
  const bool tester = live.lastLineMs && now - live.lastLineMs < TESTER_STALE_MS;
  String j;
  j.reserve(600);
  j += "{\"state\":\"";
  j += ecu ? "ECU ONLINE" : tester ? "NO ECU ANSWER" : "NO TESTER";
  j += "\",\"ecu\":"; j += ecu ? "true" : "false";
  j += ",\"ect\":"; j += jsonNum(live.validEct, live.ect, 0);
  j += ",\"iat\":"; j += jsonNum(live.validIat, live.iat, 0);
  j += ",\"map\":"; j += jsonNum(live.validMap, live.map, 0);
  j += ",\"tps\":"; j += jsonNum(live.validTps, live.tps, 0);
  j += ",\"o2\":"; j += jsonNum(live.validO2, live.o2, 3);
  j += ",\"afr\":"; j += jsonNum(live.validO2, afr(), 1);
  j += ",\"mil\":"; j += live.mil ? "true" : "false";
  j += ",\"dtcCount\":"; j += live.dtcCount;
  j += ",\"dtcRead\":"; j += live.dtcRead ? "true" : "false";
  j += ",\"dtcAge\":"; j += live.dtcRead ? now - live.dtcMs : 0;
  j += ",\"dtcs\":[";
  if (live.dtcs.length()) { j += '"'; String s = live.dtcs; s.replace(",", "\",\""); j += s; j += '"'; }
  j += "],\"pend\":[";
  if (live.pend.length()) { j += '"'; String s = live.pend; s.replace(",", "\",\""); j += s; j += '"'; }
  j += "],\"supported\":\""; j += String(live.supported, HEX);
  j += "\",\"answers\":"; j += live.answers;
  j += ",\"noResp\":"; j += live.noResponse;
  j += ",\"event\":\""; j += live.lastEvent;
  j += "\",\"eventAge\":"; j += live.lastEventMs ? now - live.lastEventMs : 0;
  j += "}";
  server.send(200, "application/json", j);
}

void handleCmd() {
  const String c = server.arg("c");
  if (c == "read") { Serial2.print("READ\n"); event("Reading DTCs..."); }
  else if (c == "clear") { Serial2.print("CLEAR\n"); event("Clearing DTCs..."); }
  else { server.send(400, "text/plain", "use c=read or c=clear"); return; }
  server.send(200, "text/plain", "OK");
}

const char PAGE[] PROGMEM = R"HTML(<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>OBD-II Diagnostic Tool</title>
<style>
:root{--bg:#0f1216;--card:#181d24;--line:#2a313b;--text:#e6e9ee;--dim:#8a94a3;--ok:#3ecf8e;--bad:#ff5d5d;--warn:#f5b942}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--text);font:15px/1.4 system-ui,Segoe UI,Roboto,sans-serif}
header{display:flex;align-items:center;justify-content:space-between;gap:10px;padding:14px 18px;border-bottom:1px solid var(--line)}
h1{font-size:17px;margin:0;letter-spacing:.04em}
.pill{font-weight:600;padding:4px 10px;border-radius:999px;background:#222a33;font-size:13px}
main{padding:18px;max-width:1000px;margin:auto;display:grid;gap:14px}
.grid{display:grid;gap:14px;grid-template-columns:repeat(auto-fit,minmax(170px,1fr))}
.card{background:var(--card);border:1px solid var(--line);border-radius:12px;padding:14px 16px}
.name{color:var(--dim);font-size:13px;letter-spacing:.08em}
.val{font-size:36px;font-weight:700;font-variant-numeric:tabular-nums;margin:4px 0 2px}
.unit{font-size:16px;color:var(--dim);font-weight:500;margin-left:4px}
.sub{color:var(--dim);font-size:13px;min-height:18px}
.dtcbar{display:flex;align-items:center;gap:14px;flex-wrap:wrap}
.mil{display:flex;align-items:center;gap:8px;font-weight:700}
.lamp{width:22px;height:22px;border-radius:50%;background:#333;border:2px solid #444}
.lamp.on{background:var(--warn);border-color:#ffd88a;box-shadow:0 0 14px var(--warn)}
.codes{display:flex;gap:8px;flex-wrap:wrap;min-height:34px;margin-top:10px}
.code{font:600 18px ui-monospace,Consolas,monospace;padding:4px 10px;border-radius:8px;background:#3a2a12;color:var(--warn)}
.code.pend{background:transparent;border:1px dashed var(--warn);color:#d9b56a}
.code small{font:500 11px system-ui;margin-left:6px;opacity:.8}
.none{color:var(--ok);font-weight:600;padding-top:6px}
button{font:600 14px system-ui;padding:9px 16px;border-radius:8px;border:1px solid var(--line);background:#222a33;color:var(--text);cursor:pointer}
button:hover{background:#2b3540}button.danger{border-color:#5a2a2a}
.spacer{flex:1}
#desc{color:var(--dim);font-size:13px;margin-top:8px}
footer{color:var(--dim);font-size:12px;text-align:center;padding:4px 18px 18px}
</style></head><body>
<header><h1>OBD-II DIAGNOSTIC TOOL</h1><span class="pill" id="state">…</span></header>
<main>
<div class="card">
  <div class="dtcbar">
    <div class="mil"><span class="lamp" id="lamp"></span><span id="miltext">MIL OFF</span></div>
    <div class="name" id="count"></div>
    <span class="spacer"></span>
    <button onclick="cmd('read')">Read DTC</button>
    <button class="danger" onclick="cmd('clear')">Clear DTC</button>
  </div>
  <div class="codes" id="codes"></div>
  <div id="desc"></div>
</div>
<div class="grid">
<div class="card"><div class="name">ECT · PID 05</div><div class="val"><span id="ect">–</span><span class="unit">°C</span></div><div class="sub">coolant temperature</div></div>
<div class="card"><div class="name">IAT · PID 0F</div><div class="val"><span id="iat">–</span><span class="unit">°C</span></div><div class="sub">intake air temperature</div></div>
<div class="card"><div class="name">MAP · PID 0B</div><div class="val"><span id="map">–</span><span class="unit">kPa</span></div><div class="sub">manifold pressure</div></div>
<div class="card"><div class="name">TPS · PID 11</div><div class="val"><span id="tps">–</span><span class="unit">%</span></div><div class="sub">throttle position</div></div>
<div class="card"><div class="name">O2 B1S1 · PID 14</div><div class="val"><span id="o2">–</span><span class="unit">V</span></div><div class="sub" id="afr"></div></div>
</div>
</main>
<footer id="foot">ECU simulator → CAN 500 kbps OBD-II → Nano tester → ESP32</footer>
<script>
const DESC={P0107:'MAP circuit low',P0108:'MAP circuit high',P0112:'IAT circuit low',P0113:'IAT circuit high',
P0117:'ECT circuit low',P0118:'ECT circuit high',P0122:'TPS circuit low',P0123:'TPS circuit high',
P0131:'O2 B1S1 circuit low voltage',P0132:'O2 B1S1 circuit high voltage',
P0217:'Engine coolant over temperature',P0111:'IAT circuit range/performance',P0171:'System too lean (bank 1)',
P0172:'System too rich (bank 1)',P0106:'MAP circuit range/performance',P0121:'TPS circuit range/performance'};
const $=id=>document.getElementById(id);
function show(id,v,d){$(id).textContent=v===null?'–':Number(v).toFixed(d)}
async function cmd(c){try{await fetch('/api/cmd?c='+c,{method:'POST'})}catch(e){}}
async function tick(){
  try{
    const d=await (await fetch('/api/live',{cache:'no-store'})).json();
    $('state').textContent=d.state;$('state').style.color=d.ecu?'var(--ok)':'var(--bad)';
    show('ect',d.ect,0);show('iat',d.iat,0);show('map',d.map,0);show('tps',d.tps,0);show('o2',d.o2,2);
    $('afr').textContent=d.afr===null?'oxygen sensor':'AFR '+d.afr.toFixed(1)+':1';
    $('lamp').classList.toggle('on',d.mil);$('miltext').textContent=d.mil?'MIL ON':'MIL OFF';
    $('count').textContent=d.dtcCount+' DTC'+(d.dtcCount==1?'':'s')+' (PID 01)';
    const c=$('codes');
    const pend=d.pend.filter(x=>!d.dtcs.includes(x));
    const tag=(x,p)=>'<span class="code'+(p?' pend':'')+'" title="'+(DESC[x]||'')+'">'+x+'<small>'+(p?'pending':'stored')+'</small></span>';
    c.innerHTML=!d.dtcRead?'<span class="sub">not read yet</span>':(d.dtcs.length||pend.length)?d.dtcs.map(x=>tag(x,0)).join('')+pend.map(x=>tag(x,1)).join(''):'<span class="none">No DTCs stored</span>';
    $('desc').textContent=d.dtcs.concat(pend).map(x=>x+' '+(DESC[x]||'')).join(' · ')||(d.event&&d.eventAge<5000?d.event:'');
    $('foot').textContent='answers '+d.answers+' · no answer '+d.noResp+' · PIDs 01-20: '+d.supported+(d.event?' · '+d.event:'');
  }catch(e){$('state').textContent='ESP32 OFFLINE';$('state').style.color='var(--bad)'}
}
setInterval(tick,300);tick();
</script></body></html>)HTML";

void setup() {
  Serial.begin(115200);
  Serial2.begin(LINK_BAUD, SERIAL_8N1, PIN_RX2, PIN_TX2);
  Serial.println("# ESP32 OBD-II DASHBOARD (Nano tester on GPIO16 / GPIO17)");

  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS);
  Serial.printf("# Wi-Fi: %s  http://%s/\n", AP_SSID, WiFi.softAPIP().toString().c_str());

  server.on("/", [] { server.send_P(200, "text/html", PAGE); });
  server.on("/api/live", handleLive);
  server.on("/api/cmd", handleCmd);
  server.begin();
}

void loop() {
  static uint32_t lastPrintMs = 0;
  readLink();
  server.handleClient();

  if (millis() - lastPrintMs >= 1000) {  // Once a second on the USB Serial Monitor.
    lastPrintMs = millis();
    const uint32_t now = millis();
    if (!live.lastLineMs || now - live.lastLineMs >= TESTER_STALE_MS) { Serial.println("# No data from the Nano tester on GPIO16"); return; }
    if (!live.answers || now - live.lastAnswerMs >= ECU_STALE_MS) { Serial.println("# Tester OK, NO ANSWER from the ECU over CAN"); return; }
    Serial.printf("ECT %d C | IAT %d C | MAP %d kPa | TPS %.0f %% | O2 %.3f V (AFR %.1f) | MIL %s | DTCs %s\n",
                  live.ect, live.iat, live.map, live.tps, live.o2, afr(), live.mil ? "ON" : "off",
                  live.dtcs.length() ? live.dtcs.c_str() : "none");
  }
}
