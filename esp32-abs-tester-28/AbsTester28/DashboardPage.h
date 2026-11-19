#pragma once
#include <Arduino.h>

// Wi-Fi dashboard for the ABS tester, served from flash at http://192.168.4.1/.
// Shows everything the three LCD pages show, on one screen:
//   header: SEN VCC (REF1) | ABS TESTER + link pill | BAT;
//   row 1 (LCD page 1): the FRONT 0-250 km/h speedometer (green) with the
//     REAR speed bar below it;
//   row 2: SPEED SENSOR (LCD page 2: 0-120 Hz = 0-20 km/h, numbers every 4,
//     the speed text without a bar, the LOW (yellow) / HIGH (green) logic
//     lamps, and the sensor fault messages) and DECLOG (LCD page 3: 0-100 Hz
//     gauge with the 0-100 % bar);
//   footer: round ABS / TCS / BRAKE / ECU RESET lights, KEY ON, the LCD page
//     chip, the generator settings (Nano: REAR / FRONT from its EC11 knob,
//     DECLOG from its pot).
// Without live data from the sender a banner says WAITING FOR DATA / NO LINK
// and the sender's cards dim; the speed sensor keeps its own card. Opens with
// the TRN TECHRONIKS logo and a glitch effect, like the LCD, then polls
// /api/live four times a second. No external files or fonts: the hotspot has
// no internet.
const char DASHBOARD_HTML[] PROGMEM = R"rawliteral(<!doctype html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">
<meta name="theme-color" content="#05080d">
<title>ABS Tester</title>
<style>
:root{--bg:#05080d;--panel:#0b131d;--panel2:#0e1824;--edge:#1d2a3a;--face:#0e1825;--plate:#060b12;
--text:#eef4fa;--muted:#8b9bb0;--dim:#3b4859;--accent:#4fd2f5;--amber:#ffb547;--red:#ff4b4b;--green:#3ddc84;--track:#172231;
--off:#0f1722;--offEdge:#1f2b3a;--offText:#435167}
*{box-sizing:border-box}
html,body{height:100%}
body{margin:0;background:radial-gradient(1200px 600px at 30% -10%,#0d1826 0,var(--bg) 60%) fixed,var(--bg);color:var(--text);
font:15px/1.4 "Barlow Semi Condensed","Roboto Condensed","Arial Narrow",system-ui,sans-serif;-webkit-font-smoothing:antialiased}
main{max-width:1000px;margin:0 auto;padding:max(14px,env(safe-area-inset-top)) 16px 20px;display:grid;gap:12px}
.panel{background:linear-gradient(180deg,var(--panel2),var(--panel));border:1px solid var(--edge);border-radius:14px;
box-shadow:0 8px 24px rgba(0,0,0,.35),inset 0 1px 0 rgba(255,255,255,.03);transition:opacity .3s,filter .3s,border-color .3s}
.num{font-variant-numeric:tabular-nums}
header{display:grid;grid-template-columns:1fr auto 1fr;gap:12px;align-items:stretch}
.brand{display:flex;flex-direction:column;align-items:center;justify-content:center;gap:6px;padding:8px 16px}
.brand h1{margin:0;font-size:20px;font-weight:800;letter-spacing:1.5px;white-space:nowrap}
.brand h1 b{color:var(--accent)}
.pill{padding:3px 12px;border-radius:999px;font-weight:800;font-size:12px;letter-spacing:1px;color:var(--bg);
background:var(--red);transition:background .3s}
.pill.live{background:var(--green);box-shadow:0 0 14px rgba(61,220,132,.45)}.pill.off{background:var(--amber)}
.tile{position:relative;padding:10px 16px 10px 26px;display:flex;align-items:center;justify-content:space-between;gap:10px}
.tile:before{content:'';position:absolute;left:12px;top:50%;margin-top:-7px;height:14px;width:4px;border-radius:2px;background:var(--c)}
.tile span{color:var(--c);font-size:15px;font-weight:700;letter-spacing:1px}
.tile strong{font-size:34px;font-weight:800;line-height:1}
.tile strong small{font-size:.55em;font-weight:700}
.tile strong em{font-style:normal;font-size:15px;font-weight:600;color:var(--muted);margin-left:4px}
.banner{display:flex;align-items:center;gap:14px;padding:10px 16px;border-color:rgba(255,75,75,.45)}
.banner b{display:block;font-size:17px;letter-spacing:1.2px;color:var(--c)}
.banner span{color:var(--muted);font-size:13px}
.row{display:grid;grid-template-columns:1fr 1fr;gap:12px}
.gauge{padding:10px 12px 12px;display:grid;gap:8px;align-content:start}
.gauge h2{margin:0;display:flex;justify-content:space-between;align-items:baseline;gap:8px;font-size:15px;font-weight:700;letter-spacing:1px;color:var(--c)}
.gauge h2 small{font-size:12px;font-weight:600;letter-spacing:.5px;color:var(--muted)}
.gauge svg{width:100%;display:block;overflow:visible}
.wide{grid-column:1/-1}.wide svg{max-width:480px;margin:0 auto}
.ndl{transform-box:view-box;transform-origin:130px 140px;transition:transform .25s linear,opacity .25s}
.lit{transition:stroke-dasharray .25s linear,stroke .25s}
.stale{opacity:.42;filter:saturate(.35)}
.fault{border-color:rgba(255,75,75,.55)}
.bar-row{display:grid;grid-template-columns:auto minmax(0,1fr) 96px;gap:12px;align-items:center;min-height:42px}
.bar-row b{font-size:15px;font-weight:700;letter-spacing:1px;min-width:56px}
.track{height:10px;border-radius:5px;background:var(--track);overflow:hidden}
.fill{height:100%;width:0;border-radius:5px;background:var(--accent);transition:width .25s linear,background .25s}
.bar-row strong{text-align:right;font-size:20px;font-weight:700}
.bar-row strong small{font-size:12px;font-weight:600;color:var(--muted);margin-left:3px}
.err{display:flex;flex-direction:column;align-items:center;justify-content:center;gap:1px;min-height:42px;padding:5px 8px;border-radius:10px;
background:rgba(255,75,75,.08);border:1px solid rgba(255,75,75,.35);color:var(--red);font-weight:800;font-size:15px;letter-spacing:1px;text-align:center}
.err small{color:var(--muted);font-weight:500;font-size:12px;letter-spacing:.3px}
footer{display:flex;flex-wrap:wrap;align-items:center;gap:10px 20px;padding:10px 16px;font-size:15px;font-weight:700;letter-spacing:1px}
.lamp{display:inline-flex;align-items:center;gap:8px;font-size:17px;font-weight:800;letter-spacing:1.5px;color:var(--offText);transition:color .12s}
.lamp i{width:18px;height:18px;border-radius:50%;background:var(--off);border:2px solid var(--offEdge);transition:background .12s,box-shadow .12s}
.lamp.lit{color:var(--text)}
.lamp.lit i{background:radial-gradient(circle at 35% 35%,#ffe3b3 0,var(--amber) 55%);border-color:transparent;box-shadow:0 0 14px rgba(255,181,71,.75)}
.key{display:inline-flex;align-items:center;gap:8px}
.dot{width:10px;height:10px;border-radius:50%;background:var(--dim)}
.dot.on{background:var(--green);box-shadow:0 0 10px rgba(61,220,132,.7)}
.chip{padding:2px 10px;border-radius:999px;border:1px solid var(--edge);color:var(--muted);font-size:12px;font-weight:700;letter-spacing:.8px}
.chip.sel{color:var(--amber);border-color:var(--amber)}.chip.lock{color:var(--accent);border-color:var(--accent)}
.gens{display:flex;flex-wrap:wrap;gap:6px 16px;font-weight:600;color:var(--muted)}
.gens b{color:var(--accent);font-weight:700}.gens .adj,.gens .adj b{color:var(--amber)}
.gauge h2 .adjTag{color:var(--amber);font-weight:800;letter-spacing:1px}
.stamp{margin-left:auto;font-weight:500;font-size:12px;color:var(--muted);letter-spacing:.5px}
.lamp.y.lit i{background:radial-gradient(circle at 35% 35%,#fff6b3 0,#ffd82a 55%);box-shadow:0 0 14px rgba(255,216,42,.75)}
.lamp.g.lit i{background:radial-gradient(circle at 35% 35%,#c9ffe0 0,var(--green) 55%);box-shadow:0 0 14px rgba(61,220,132,.75)}
.lamp.r.lit i{background:radial-gradient(circle at 35% 35%,#ffc9c9 0,var(--red) 55%);box-shadow:0 0 14px rgba(255,75,75,.75)}
.logic{display:flex;justify-content:center;gap:24px;padding-top:2px}
.overlay{position:fixed;inset:0;display:flex;align-items:center;justify-content:center;padding:16px;transition:opacity .4s}
.absring{display:block;margin:0 auto;flex:none}
#splash{background:#000;flex-direction:column;z-index:2}
/* TRN TECHRONIKS logo (the same artwork as the LCD, an embedded 480 x 221 JPEG). */
.logo{animation:glitch 2.4s steps(1) both}
.logo img{display:block;width:min(78vw,480px);height:auto}
/* Glitch: slices jump sideways with a red / blue colour split and flicker,
   settling to a clean logo, two short bursts, then the overlay fades. */
@keyframes glitch{
0%{opacity:0}4%{opacity:1;transform:translate(-8px,0);clip-path:inset(10% 0 55% 0);filter:drop-shadow(-5px 0 #ff2a55) drop-shadow(5px 0 #00c8ff)}
8%{transform:translate(7px,0);clip-path:inset(50% 0 10% 0)}
12%{opacity:.3;transform:translate(-4px,0);clip-path:inset(0 0 70% 0)}
16%{opacity:1;transform:translate(5px,0);clip-path:inset(30% 0 30% 0);filter:drop-shadow(-3px 0 #ff2a55) drop-shadow(3px 0 #00c8ff)}
20%{transform:translate(-2px,0);clip-path:inset(0 0 0 0)}
24%{transform:translate(2px,0);filter:drop-shadow(-2px 0 #ff2a55) drop-shadow(2px 0 #00c8ff)}
30%{transform:none;filter:none;clip-path:none}
62%{transform:translate(-6px,0);clip-path:inset(40% 0 35% 0);filter:drop-shadow(-4px 0 #ff2a55) drop-shadow(4px 0 #00c8ff)}
65%{transform:none;filter:none;clip-path:none}
76%{transform:translate(4px,0);clip-path:inset(15% 0 60% 0);filter:drop-shadow(-3px 0 #ff2a55) drop-shadow(3px 0 #00c8ff)}
78%{transform:none;filter:none;clip-path:none}
100%{transform:none;filter:none;clip-path:none}}
.gone{opacity:0;pointer-events:none}
@keyframes fade{from{opacity:0}}
[hidden]{display:none!important}
/* ---- Responsive layout ----
   phone  < 640 px : one column, compact;
   tablet 640-1099 : two columns (FRONT across the top), bigger FRONT dial;
   laptop >= 1100  : FRONT on the left, SPEED SENSOR / DECLOG stacked on the
                     right, dials sized to fit the screen height (no scrolling);
   PC     >= 1600  : the same, wider, with bigger text. */
@media(max-width:640px){header{grid-template-columns:1fr 1fr}.brand{grid-column:1/-1;grid-row:1;flex-direction:row;justify-content:space-between}
.tile strong{font-size:28px}.row{grid-template-columns:1fr}.bar-row{grid-template-columns:auto minmax(0,1fr) 88px}
main{padding-left:max(10px,env(safe-area-inset-left));padding-right:max(10px,env(safe-area-inset-right));gap:10px}
footer{gap:10px 14px;font-size:14px}.lamp{font-size:15px}.gauge h2{font-size:14px}}
@media(max-width:380px){.tile{padding:8px 10px 8px 22px}.tile strong{font-size:24px}.tile span{font-size:13px}.lamp{font-size:14px}.lamp i{width:15px;height:15px}}
@media(min-width:641px) and (max-width:1099px){.wide svg{max-width:600px}}
@media(min-width:1100px){
 main{max-width:1500px;padding:18px 24px 22px;gap:14px;
  grid-template-columns:minmax(0,1.35fr) minmax(0,1fr);grid-template-rows:auto auto minmax(0,1fr) minmax(0,1fr) auto;
  grid-template-areas:"head head" "banner banner" "front sensor" "front declog" "foot foot"}
 .row{display:contents}
 header{grid-area:head}#banner{grid-area:banner}#frontCard{grid-area:front}#sensorCard{grid-area:sensor}#declogCard{grid-area:declog}footer{grid-area:foot}
 /* Each dial takes the space left in its card and scales to fit it. */
 .gauge{display:flex;flex-direction:column;min-height:0}
 .gauge>*{flex:none}
 .gauge svg{flex:1 1 0;min-height:80px;width:100%;height:100%;max-width:none}
 .tile strong{font-size:40px}.tile span{font-size:16px}.brand h1{font-size:24px}
 .gauge h2{font-size:17px}.bar-row strong{font-size:24px}.bar-row b{font-size:17px}
 .lamp{font-size:18px}.lamp i{width:20px;height:20px}footer{font-size:16px}}
@media(min-width:1600px){
 main{max-width:1840px;padding:24px 32px 26px;gap:18px}
 .tile strong{font-size:48px}.tile span{font-size:18px}.brand h1{font-size:28px}.pill{font-size:14px}
 .gauge h2{font-size:20px}.bar-row strong{font-size:28px}.bar-row b{font-size:19px}
 .lamp{font-size:20px}.lamp i{width:22px;height:22px}footer{font-size:18px;padding:12px 20px}}
/* Laptop / PC tall enough: the whole dashboard fits the screen (no scrolling). */
@media(min-width:1100px) and (min-height:620px){main{height:100vh;height:100dvh}}
@media(prefers-reduced-motion:reduce){*{transition:none!important;animation-duration:.01s!important}}
</style></head><body><main>
<header>
 <div class="panel tile" style="--c:var(--accent)" id="ref1Tile"><span>SEN VCC</span><strong class="num" id="ref1">—</strong></div>
 <div class="panel brand"><h1><b>ABS</b> TESTER</h1><span class="pill" id="pill">WAITING</span></div>
 <div class="panel tile" style="--c:var(--amber)" id="batTile"><span>BAT</span><strong class="num" id="battery">—</strong></div>
</header>
<div class="panel banner" id="banner" style="--c:var(--red)">
 <svg class="absring" width="54" height="36" viewBox="-48 -32 96 64" aria-hidden="true" style="color:var(--c)" id="bannerIcon">
  <circle r="22" fill="none" stroke="currentColor" stroke-width="5"/>
  <path d="M-30 -14A33 33 0 0 0 -30 14M30 -14A33 33 0 0 1 30 14" fill="none" stroke="currentColor" stroke-width="5" stroke-linecap="round"/>
  <text y="6" text-anchor="middle" font-size="17" font-weight="800" fill="currentColor">ABS</text></svg>
 <div><b id="bannerTitle">WAITING FOR DATA</b><span id="bannerText">Connecting to the ABS tester…</span></div>
</div>
<div class="row">
 <section class="panel gauge wide" style="--c:var(--green)" id="frontCard"></section>
</div>
<div class="row">
 <section class="panel gauge" style="--c:var(--accent)" id="sensorCard"></section>
 <section class="panel gauge" style="--c:var(--accent)" id="declogCard"></section>
</div>
<footer class="panel">
 <span class="lamp" id="absLamp"><i></i>ABS</span><span class="lamp" id="tcsLamp"><i></i>TCS</span>
 <span class="lamp" id="brakeLamp"><i></i>BRAKE</span><span class="lamp r" id="ecuLamp"><i></i>ECU RESET</span>
 <span class="key"><i class="dot" id="keyDot"></i><span id="keyText">KEY OFF</span></span>
 <span class="chip" id="pageChip">LCD 1/3 SPEED</span>
 <span class="gens" id="gens"></span><span class="stamp" id="stamp"></span>
</footer>
</main>
<div class="overlay" id="splash">
 <div class="logo"><img alt="TRN Techroniks" width="480" height="221" src="data:image/jpeg;base64,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"></div></div>
<script>
const $=id=>document.getElementById(id),CX=130,CY=140;
// Dials, as on the LCD: wheels 0-250 km/h (red from 210), speed sensor 0-20
// km/h (0-120 Hz, numbers every 4, no red, speed text without a bar, LOW /
// HIGH logic lamps), Declog 0-100 Hz. bar: the name on the bar row (the
// FRONT card's bar shows the REAR speed, as on LCD page 1).
const D={
 front:{label:'FRONT',bar:'REAR',max:250,minor:10,major:50,red:210,unit:'km/h',needle:'var(--green)',speed:true},
 sensor:{label:'SPEED SENSOR',bar:'SPEED',max:20,minor:1,major:4,red:21,unit:'km/h',needle:'var(--red)',noBar:true,logic:true,dec:1},
 declog:{label:'DECLOG',bar:'DECLOG',max:100,minor:5,major:25,red:1e9,unit:'Hz',needle:'var(--red)',pct:true}};
const clamp=(v,m)=>Math.min(Math.max(+v||0,0),m);
// Speed colour, as on the LCD: cyan to 140, to amber by 170, amber to red from 200 to 215.
const mix=(a,b,f)=>a.map((x,i)=>Math.round(x+(b[i]-x)*f));
const C={a:[79,210,245],m:[255,181,71],r:[255,75,75]};
function speedColor(v){let c=v<140?C.a:v<170?mix(C.a,C.m,(v-140)/30):v<200?C.m:v<215?mix(C.m,C.r,(v-200)/15):C.r;return`rgb(${c})`;}
function litColor(o,v){return o.speed?speedColor(v):v>=o.red?'var(--red)':'var(--accent)';}
// One dial card: header, face with ticks every minor, numbers every major,
// needle, centre plate, then the bar row (and the error block for the sensor).
function card(p,o){
 const pt=(v,r)=>{const a=Math.PI+clamp(v,o.max)/o.max*Math.PI;return[CX+Math.cos(a)*r,CY+Math.sin(a)*r];};
 let t='';for(let i=0;i*o.minor<=o.max;i++){const v=i*o.minor,major=v%o.major==0,c=v>=o.red?'var(--red)':major?'var(--text)':'var(--muted)';
  const[x1,y1]=pt(v,102),[x2,y2]=pt(v,major?90:96);
  t+=`<line x1="${x1}" y1="${y1}" x2="${x2}" y2="${y2}" stroke="${c}" stroke-width="${major?2.4:1.2}" stroke-linecap="round"/>`;
  if(major){const a=v==0?-.17:v==o.max?.17:0,[tx,ty]=pt(v-a*o.max/Math.PI,77);
   t+=`<text x="${tx}" y="${ty+4}" text-anchor="middle" fill="${c}" font-size="11" font-weight="600">${v}</text>`;}}
 const n=o.needle;
 $(p+'Card').innerHTML=`<h2>${o.label}<small class="adjTag" id="${p}Adj"></small></h2><svg viewBox="0 0 260 150" aria-label="${o.label}">
 <path d="M14 140A116 116 0 0 1 246 140Z" fill="var(--face)"/>
 <path d="M10 140A120 120 0 0 1 250 140" fill="none" stroke="var(--edge)" stroke-width="1.2"/>
 <path d="M22 140A108 108 0 0 1 238 140" fill="none" stroke="var(--track)" stroke-width="9"/>
 <path class="lit" id="${p}Lit" d="M22 140A108 108 0 0 1 238 140" fill="none" stroke="var(--accent)" stroke-width="9" pathLength="200" stroke-dasharray="0 200"/>
 <g>${t}</g>
 <g class="ndl" id="${p}Needle"><line x1="100" y1="140" x2="26" y2="140" stroke="${n}" stroke-width="7" stroke-linecap="round" opacity=".22"/>
  <polygon points="100,137 100,143 25,140.6 25,139.4" fill="${n}"/><line x1="100" y1="140" x2="30" y2="140" stroke="#fff" stroke-opacity=".55" stroke-width=".9"/></g>
 <rect x="91" y="98" width="78" height="48" rx="9" fill="var(--face)"/>
 <rect x="94" y="101" width="72" height="42" rx="8" fill="var(--plate)" stroke="#22303f" id="${p}Plate"/>
 <text x="130" y="113" text-anchor="middle" font-size="8" fill="var(--muted)" letter-spacing=".5" id="${p}Unit">${o.unit}</text>
 <text x="130" y="138" text-anchor="middle" font-size="26" font-weight="800" fill="var(--text)" class="num" id="${p}Val">--</text>
</svg><div class="bar-row" id="${p}Row"><b id="${p}BarName">${o.bar}</b>${o.noBar?'<span></span>':`<div class="track"><div class="fill" id="${p}Bar"></div></div>`}<strong class="num"><span id="${p}Text">--</span><small>${o.pct?'%':o.unit}</small></strong></div>
 <div class="err" id="${p}Err" hidden><span id="${p}ErrTitle">NO SPEED SENSOR READING</span><small>Check the sensor and its wire</small></div>
 ${o.logic?'<div class="logic"><span class="lamp y" id="loLamp"><i></i>LOW</span><span class="lamp g" id="hiLamp"><i></i>HIGH</span></div>':''}`;}
for(const p in D)card(p,D[p]);
// v: reading in the dial's unit; have false: needle parked, "--".
// bv / bhave: the bar's own reading (defaults to the dial's).
function setDial(p,v,have,bv=v,bhave=have){
 const o=D[p],f=have?clamp(v,o.max)/o.max:0;
 $(p+'Needle').style.transform=`rotate(${f*180}deg)`;$(p+'Needle').style.opacity=have?1:.25;
 $(p+'Lit').setAttribute('stroke-dasharray',`${f*200} 200`);$(p+'Lit').setAttribute('stroke',litColor(o,v));
 $(p+'Val').textContent=have?Math.round(v):'--';$(p+'Val').setAttribute('fill',have&&v>=o.red?'var(--red)':'var(--text)');
 const bf=bhave?clamp(bv,o.max)/o.max:0;
 $(p+'Text').textContent=bhave?(o.dec?clamp(bv,o.max).toFixed(o.dec):Math.round(o.pct?bf*100:bv)):'--';
 if($(p+'Bar')){$(p+'Bar').style.width=bf*100+'%';$(p+'Bar').style.background=litColor(o,bv);}}
// Speed sensor fault: NO SIGNAL on the plate, red edge, the error block
// instead of the speed row (Nano D6 check: open / short or stuck HIGH).
function sensorFault(on,fault){
 $('sensorErrTitle').textContent=fault==='open_short'?'OPEN OR SHORT SENSOR':fault==='stuck_high'?'NO PULSE (STUCK HIGH)':'NO SPEED SENSOR READING';
 $('sensorCard').classList.toggle('fault',on);$('sensorErr').hidden=!on;$('sensorRow').hidden=on;
 $('sensorUnit').textContent=on?'NO SIGNAL':'km/h';$('sensorUnit').setAttribute('fill',on?'var(--red)':'var(--muted)');
 $('sensorPlate').setAttribute('stroke',on?'var(--red)':'#22303f');}
// Round warning lights: "on" steady, "flash" blinks at 2 Hz.
let lampStates={};
function lamps(){const blink=Math.floor(Date.now()/250)%2;
 for(const id of['absLamp','tcsLamp','brakeLamp','ecuLamp']){const s=lampStates[id];$(id).classList.toggle('lit',s==='on'||(s==='flash'&&!!blink));}}
setInterval(lamps,125);
// Speed sensor logic lamps, as on LCD page 2: LOW (yellow, logic 0) and HIGH
// (green, logic 1) alternate at a visible rate that rises with the input:
// the real rate up to 2 Hz, then 2 Hz rising to 8 Hz at 120 Hz. No pulses:
// stuck LOW (open / short) = LOW steady, stuck HIGH = HIGH steady.
let logic={hz:0,fault:null,live:false},phase=0,lastT=0;
function logicFrame(t){const dt=Math.min((t-lastT)/1000,.2);lastT=t;
 let lvl=-1;const f=logic.hz;
 if(logic.live&&f>0){const r=f<=2?f:2+(Math.min(f,120)-2)*6/118;phase=(phase+r*dt)%1;lvl=phase<.5?1:0;}
 else if(logic.live&&logic.fault==='open_short')lvl=0;else if(logic.live&&logic.fault==='stuck_high')lvl=1;
 $('loLamp').classList.toggle('lit',lvl===0);$('hiLamp').classList.toggle('lit',lvl===1);
 requestAnimationFrame(logicFrame);}
requestAnimationFrame(logicFrame);
function volts(id,v){if(typeof v!=='number'){$(id).textContent='—';return;}
 const [w,f]=v.toFixed(2).split('.');$(id).innerHTML=`${w}<small>.${f}</small><em>V</em>`;}
const PAGES=['SPEED','SENSOR','DECLOG'];
function show(d){
 const live=d.link==='live',on=live&&d.key;
 $('pill').textContent=!live?(d.link==='stale'?'NO LINK':'WAITING'):d.key?'LIVE':'KEY OFF';
 $('pill').className='pill'+(on?' live':live?' off':'');
 // Banner: why the sender's readings are missing (the speed sensor keeps going).
 const mega=d.sender==='mega',name=mega?'Mega':'Nano';
 $('banner').hidden=on;
 $('bannerTitle').textContent=!live?(d.link==='stale'?'NO LINK':'WAITING FOR DATA'):'UPDATE '+name.toUpperCase();
 $('bannerText').textContent=live?'Flash the new '+name+' firmware':d.link==='stale'
  ?(d.sender?'Check '+(mega?'Mega D18':'Nano D5')+' → GPIO16 and GND':'Check the link to GPIO16'):'Connecting to the ABS tester…';
 $('banner').style.setProperty('--c',live?'var(--amber)':'var(--red)');
 $('banner').style.borderColor=live?'rgba(255,181,71,.45)':'rgba(255,75,75,.45)';
 for(const id of['frontCard','declogCard','ref1Tile','batTile'])$(id).classList.toggle('stale',!on);
 $('keyDot').className='dot'+(on?' on':'');$('keyText').textContent=on?'KEY ON':'KEY OFF';
 // LCD page chip: the page shown (the page push button on GPIO4 cycles them).
 const pg=d.page>=1&&d.page<=3?d.page:1;
 $('pageChip').textContent=`LCD ${pg}/3 ${PAGES[pg-1]}`;$('pageChip').className='chip';
 // Generator settings (no pin names, as on the LCD). With the KY-040 speed
 // encoder (speed_encoder "rear" / "front") REAR / FRONT come from it and the
 // wheel it adjusts is amber, with ADJUST on its gauge (FRONT) or bar (REAR);
 // DECLOG is always the pot. Older sender firmware: all three are pots.
 const enc=on&&(d.speed_encoder==='rear'||d.speed_encoder==='front')?d.speed_encoder:null;
 const g=[['REAR',d.gen_rear_hz,'rear'],['FRONT',d.gen_front_hz,'front'],['DECLOG',d.gen_declog_hz]];
 const sp=([n,v,w])=>`<span${enc&&w===enc?' class="adj"':''}>${n} <b>${Math.round(v)}</b> Hz</span>`;
 // The Nano's EC11 knob drives REAR and FRONT together; DECLOG is its pot.
 $('gens').innerHTML=!on||!g.every(x=>typeof x[1]==='number')?'':enc||d.sender==='nano'
  ?(d.sender==='nano'?'KNOB ':'ENCODER ')+g.slice(0,2).map(sp).join('')+' POT '+sp(g[2]):'POT '+g.map(sp).join('');
 $('frontAdj').textContent=enc==='front'?' · ADJUST':'';
 $('frontBarName').textContent=enc==='rear'?'REAR · ADJUST':'REAR';
 $('frontBarName').style.color=enc==='rear'?'var(--amber)':'';
 // BRAKE: the Nano knob hold or the GPIO15 button; ECU RESET: the GPIO13
 // relay is cutting the ABS ECU's power (2 s after a knob click).
 lampStates=on?{absLamp:d.abs_lamp,tcsLamp:d.tcs_lamp,brakeLamp:d.brake?'on':'off',ecuLamp:d.ecu_reset?'on':'off'}
  :{ecuLamp:d.ecu_reset?'on':'off'};lamps();
 // Speed sensor (Nano D6, or GPIO34 on the ESP32): always shown.
 // (Unreachable ESP32: the card dims instead of claiming a sensor fault.)
 const esp=d.uptime_ms!==undefined,sig=esp&&typeof d.input_kmh==='number';
 logic={hz:sig?+d.input_hz||0:0,fault:d.input_fault||null,live:esp};
 $('sensorCard').classList.toggle('stale',!esp);
 sensorFault(esp&&!sig,d.input_fault);
 setDial('sensor',sig?d.input_kmh:0,sig);
 // The sender's readings: FRONT on the dial, REAR on its bar.
 const hz=Math.max(+d.declog_hz||0,0);
 setDial('front',on?d.front_speed_kmh:0,on&&!!d.front,on?d.speed_kmh:0,on);
 setDial('declog',on?hz:0,on);
 volts('ref1',on?d.ref1_v:null);volts('battery',on?d.battery_v:null);
 if(d.uptime_ms!==undefined)$('stamp').textContent='Updated '+new Date().toLocaleTimeString();
}
async function poll(){try{const r=await fetch('/api/live',{cache:'no-store'});show(await r.json());}
 catch(e){show({link:'stale'});}setTimeout(poll,250);}
setTimeout(()=>{$('splash').classList.add('gone');setTimeout(()=>$('splash').hidden=true,450);},2500);
poll();
</script></body></html>)rawliteral";
