#pragma once
#include <Arduino.h>

// Wi-Fi dashboard for the ABS tester, served from flash at http://192.168.4.1/.
// Shows everything the three LCD pages show, on one screen:
//   header: REF1 | ABS TESTER + link pill | BAT;
//   row 1 (LCD page 1): REAR (left, red) and FRONT (right, green) 0-250 km/h
//     speedometers with their speed bars;
//   row 2: SPEED SENSOR (LCD page 2: GPIO34, 0-2000 Hz = 0-200 km/h, red from
//     180, NO SPEED SENSOR READING with no pulses) and DECLOG (LCD page 3:
//     0-100 Hz gauge with the 0-100 % bar);
//   footer: round ABS / TCS lights, KEY ON, the LCD page chip (page, name and
//     the encoder's SELECT / LOCKED mode), the pot generator settings.
// Without live data from the sender a banner says WAITING FOR DATA / NO LINK
// and the sender's cards dim; the speed sensor is measured on the ESP32
// itself, so it keeps working. Opens with a short splash like the LCD, then
// polls /api/live four times a second. No external files or fonts: the
// hotspot has no internet.
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
.ndl{transform-box:view-box;transform-origin:130px 140px;transition:transform .25s linear,opacity .25s}
.lit{transition:stroke-dasharray .25s linear,stroke .25s}
.stale{opacity:.42;filter:saturate(.35)}
.fault{border-color:rgba(255,75,75,.55)}
.bar-row{display:grid;grid-template-columns:minmax(0,1fr) 96px;gap:12px;align-items:center;min-height:42px}
.track{height:10px;border-radius:5px;background:var(--track);overflow:hidden}
.fill{height:100%;width:0;border-radius:5px;background:var(--accent);transition:width .25s linear,background .25s}
.bar-row strong{text-align:right;font-size:20px;font-weight:700}
.bar-row strong small{font-size:12px;font-weight:600;color:var(--muted);margin-left:3px}
.sub{grid-column:1/-1;margin-top:-6px;font-size:12px;color:var(--muted);letter-spacing:.3px}
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
.gens b{color:var(--accent);font-weight:700}
.stamp{margin-left:auto;font-weight:500;font-size:12px;color:var(--muted);letter-spacing:.5px}
.overlay{position:fixed;inset:0;display:flex;align-items:center;justify-content:center;padding:16px;transition:opacity .4s}
.absring{display:block;margin:0 auto;flex:none}
#splash{background:var(--bg);flex-direction:column;z-index:2}
#splash h2{font-size:30px;font-weight:800;letter-spacing:2px;margin:18px 0 18px;animation:fade .8s .2s both}
#splash .ring{stroke-dasharray:0 200;animation:sweep 1s ease-out .05s forwards}
#splash .brk{animation:fade .8s .2s both}
#splash .load{width:140px;height:6px;border-radius:3px;background:var(--track);overflow:hidden}
#splash .load i{display:block;height:100%;width:0;background:var(--accent);border-radius:3px;animation:load 1.5s ease-in-out forwards}
.gone{opacity:0;pointer-events:none}
@keyframes sweep{to{stroke-dasharray:200 0}}@keyframes fade{from{opacity:0}}@keyframes load{to{width:100%}}
[hidden]{display:none!important}
@media(max-width:640px){header{grid-template-columns:1fr 1fr}.brand{grid-column:1/-1;grid-row:1;flex-direction:row;justify-content:space-between}
.tile strong{font-size:28px}.row{grid-template-columns:1fr}.bar-row{grid-template-columns:minmax(0,1fr) 88px}}
@media(prefers-reduced-motion:reduce){*{transition:none!important;animation-duration:.01s!important}}
</style></head><body><main>
<header>
 <div class="panel tile" style="--c:var(--accent)" id="ref1Tile"><span>REF1</span><strong class="num" id="ref1">—</strong></div>
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
 <section class="panel gauge" style="--c:var(--red)" id="rearCard"></section>
 <section class="panel gauge" style="--c:var(--green)" id="frontCard"></section>
</div>
<div class="row">
 <section class="panel gauge" style="--c:var(--accent)" id="sensorCard"></section>
 <section class="panel gauge" style="--c:var(--accent)" id="declogCard"></section>
</div>
<footer class="panel">
 <span class="lamp" id="absLamp"><i></i>ABS</span><span class="lamp" id="tcsLamp"><i></i>TCS</span>
 <span class="key"><i class="dot" id="keyDot"></i><span id="keyText">KEY OFF</span></span>
 <span class="chip" id="pageChip">LCD 1/3 SPEED</span>
 <span class="gens" id="gens"></span><span class="stamp" id="stamp"></span>
</footer>
</main>
<div class="overlay" id="splash">
 <svg class="absring" width="120" height="80" viewBox="-60 -40 120 80" aria-hidden="true">
  <circle r="28" fill="none" stroke="var(--track)" stroke-width="4"/>
  <circle class="ring" r="28" fill="none" stroke="var(--accent)" stroke-width="4" pathLength="200" transform="rotate(-90)"/>
  <path class="brk" d="M-38 -18A42 42 0 0 0 -38 18M38 -18A42 42 0 0 1 38 18" fill="none" stroke="var(--amber)" stroke-width="4" stroke-linecap="round"/>
  <text class="brk" y="5" text-anchor="middle" font-size="15" font-weight="800" fill="var(--amber)">ABS</text></svg>
 <h2>ABS TESTER</h2><div class="load"><i></i></div></div>
<script>
const $=id=>document.getElementById(id),CX=130,CY=140;
// Dials, as on the LCD: wheels 0-250 km/h (red from 210), speed sensor 0-200
// km/h (red from 180), Declog 0-100 Hz.
const D={
 rear:{label:'REAR',sub:'D7',max:250,minor:10,major:50,red:210,unit:'km/h',needle:'var(--red)',speed:true},
 front:{label:'FRONT',sub:'D8',max:250,minor:10,major:50,red:210,unit:'km/h',needle:'var(--green)',speed:true},
 sensor:{label:'SPEED SENSOR',sub:'GPIO34 · 10 Hz = 1 km/h',max:200,minor:10,major:50,red:180,unit:'km/h',needle:'var(--red)'},
 declog:{label:'DECLOG',sub:'D2 · 0-100 Hz',max:100,minor:5,major:25,red:1e9,unit:'Hz',needle:'var(--red)',pct:true}};
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
 $(p+'Card').innerHTML=`<h2>${o.label}<small>${o.sub}</small></h2><svg viewBox="0 0 260 150" aria-label="${o.label}">
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
</svg><div class="bar-row" id="${p}Row"><div class="track"><div class="fill" id="${p}Bar"></div></div><strong class="num"><span id="${p}Text">--</span><small>${o.pct?'%':o.unit}</small></strong>
 <div class="sub" id="${p}Sub"></div></div>
 <div class="err" id="${p}Err" hidden>NO SPEED SENSOR READING<small>Check the sensor and its wire to GPIO34</small></div>`;}
for(const p in D)card(p,D[p]);
// v: reading in the dial's unit; have false: needle parked, "--".
function setDial(p,v,have,sub){
 const o=D[p],f=have?clamp(v,o.max)/o.max:0;
 $(p+'Needle').style.transform=`rotate(${f*180}deg)`;$(p+'Needle').style.opacity=have?1:.25;
 $(p+'Lit').setAttribute('stroke-dasharray',`${f*200} 200`);$(p+'Lit').setAttribute('stroke',litColor(o,v));
 $(p+'Val').textContent=have?Math.round(v):'--';$(p+'Val').setAttribute('fill',have&&v>=o.red?'var(--red)':'var(--text)');
 $(p+'Text').textContent=have?Math.round(o.pct?f*100:v):'--';
 $(p+'Bar').style.width=f*100+'%';$(p+'Bar').style.background=litColor(o,v);
 $(p+'Sub').textContent=sub||'';}
// Speed sensor fault: NO SIGNAL on the plate, red edge, error block instead of the bar.
function sensorFault(on){
 $('sensorCard').classList.toggle('fault',on);$('sensorErr').hidden=!on;$('sensorRow').hidden=on;
 $('sensorUnit').textContent=on?'NO SIGNAL':'km/h';$('sensorUnit').setAttribute('fill',on?'var(--red)':'var(--muted)');
 $('sensorPlate').setAttribute('stroke',on?'var(--red)':'#22303f');}
// Round warning lights: "on" steady, "flash" blinks at 2 Hz.
let lampStates={};
function lamps(){const blink=Math.floor(Date.now()/250)%2;
 for(const id of['absLamp','tcsLamp']){const s=lampStates[id];$(id).classList.toggle('lit',s==='on'||(s==='flash'&&!!blink));}}
setInterval(lamps,125);
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
 for(const id of['rearCard','frontCard','declogCard','ref1Tile','batTile'])$(id).classList.toggle('stale',!on);
 $('keyDot').className='dot'+(on?' on':'');$('keyText').textContent=on?'KEY ON':'KEY OFF';
 // LCD page chip: page, name and the encoder mode.
 const pg=d.page>=1&&d.page<=3?d.page:1,mode=d.page_mode;
 $('pageChip').textContent=`LCD ${pg}/3 ${PAGES[pg-1]}`+(mode==='select'?' · SELECT':mode==='locked'?' · LOCKED':'');
 $('pageChip').className='chip'+(mode==='select'?' sel':mode==='locked'?' lock':'');
 // Pot generator settings: Nano rear A0, front A1, Declog A2; Mega rear A1, front A0, Declog A2.
 const g=[['REAR '+(mega?'A1':'A0'),d.gen_rear_hz],['FRONT '+(mega?'A0':'A1'),d.gen_front_hz],['DECLOG A2',d.gen_declog_hz]];
 $('gens').innerHTML=on&&g.every(x=>typeof x[1]==='number')?'POT '+g.map(([n,v])=>`<span>${n} <b>${Math.round(v)}</b> Hz</span>`).join(''):'';
 lampStates=on?{absLamp:d.abs_lamp,tcsLamp:d.tcs_lamp}:{};lamps();
 // Speed sensor on GPIO34, measured by the ESP32: always shown.
 // (Unreachable ESP32: the card dims instead of claiming a sensor fault.)
 const esp=d.uptime_ms!==undefined,sig=esp&&typeof d.input_kmh==='number';
 $('sensorCard').classList.toggle('stale',!esp);
 sensorFault(esp&&!sig);
 setDial('sensor',sig?d.input_kmh:0,sig,sig?Math.round(d.input_hz)+' Hz on GPIO34':'');
 // The sender's readings.
 const hz=Math.max(+d.declog_hz||0,0);
 setDial('rear',on?d.speed_kmh:0,on,on?(+d.frequency_hz||0).toFixed(0)+' Hz on D7':'');
 setDial('front',on?d.front_speed_kmh:0,on&&!!d.front,on&&d.front?(+d.front_frequency_hz||0).toFixed(0)+' Hz on D8':'');
 setDial('declog',on?hz:0,on,on?hz.toFixed(1)+' Hz on D2':'');
 volts('ref1',on?d.ref1_v:null);volts('battery',on?d.battery_v:null);
 if(d.uptime_ms!==undefined)$('stamp').textContent='Updated '+new Date().toLocaleTimeString();
}
async function poll(){try{const r=await fetch('/api/live',{cache:'no-store'});show(await r.json());}
 catch(e){show({link:'stale'});}setTimeout(poll,250);}
setTimeout(()=>{$('splash').classList.add('gone');setTimeout(()=>$('splash').hidden=true,450);},1700);
poll();
</script></body></html>)rawliteral";
