#pragma once
#include <Arduino.h>

// Wi-Fi dashboard for the ABS tester, served from flash at http://192.168.4.1/.
// Same layout as the 2.8" LCD: 0-250 km/h speedometer (red rear needle and
// readout, green front needle), ABS / TCS / SENSOR warning lights, the REAR
// speed bar with the DECLOG bar below it, REF1 and BATTERY tiles and KEY ON.
// Opens with a short splash like the LCD, then polls /api/live four times a
// second; until live data arrives it shows the WAITING FOR NANO / NO LINK panel.
// No external files or fonts: the hotspot has no internet.
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
main{max-width:1000px;margin:0 auto;padding:max(14px,env(safe-area-inset-top)) 16px 20px}
.panel{background:linear-gradient(180deg,var(--panel2),var(--panel));border:1px solid var(--edge);border-radius:14px;
box-shadow:0 8px 24px rgba(0,0,0,.35),inset 0 1px 0 rgba(255,255,255,.03)}
.num{font-variant-numeric:tabular-nums}
header{display:flex;align-items:center;gap:12px;padding:10px 16px;margin-bottom:12px}
header h1{margin:0;font-size:22px;font-weight:800;letter-spacing:1.5px}
header h1 b{color:var(--accent)}
.pill{margin-left:auto;padding:4px 14px;border-radius:999px;font-weight:800;font-size:13px;letter-spacing:1px;color:var(--bg);
background:var(--red);transition:background .3s}
.pill.live{background:var(--green);box-shadow:0 0 14px rgba(61,220,132,.45)}.pill.off{background:var(--amber)}
.grid{display:grid;grid-template-columns:minmax(0,2.3fr) minmax(0,1fr);gap:12px}
.left{display:grid;gap:12px;align-content:start}
.gauge{padding:12px 12px 6px}
.gauge svg{width:100%;display:block;overflow:visible}
.ndl{transform-box:view-box;transform-origin:130px 140px;transition:transform .25s linear}
#lit{transition:stroke-dasharray .25s linear,stroke .25s}
.legend{display:flex;justify-content:center;gap:18px;font-size:12px;color:var(--muted);letter-spacing:.5px;padding:2px 0 4px}
.legend i{display:inline-block;width:14px;height:3px;border-radius:2px;margin-right:6px;vertical-align:middle}
.lamps{display:grid;grid-template-columns:repeat(3,1fr);gap:10px}
.tt{text-align:center;padding:12px 0;border-radius:12px;font-weight:800;font-size:17px;letter-spacing:1.5px;
background:var(--off);border:1px solid var(--offEdge);color:var(--offText);transition:background .12s,color .12s,box-shadow .12s}
.tt.lit{background:var(--amber);border-color:transparent;color:var(--bg);box-shadow:0 0 18px rgba(255,181,71,.45)}
.tt.lit.ok{background:var(--green);box-shadow:0 0 18px rgba(61,220,132,.45)}
.bars{padding:14px 16px;display:grid;gap:14px}
.bar-row{display:grid;grid-template-columns:70px minmax(0,1fr) 96px;gap:12px;align-items:center}
.bar-row>span{font-weight:700;font-size:14px;letter-spacing:1px}
.track{height:10px;border-radius:5px;background:var(--track);overflow:hidden}
.fill{height:100%;width:0;border-radius:5px;transition:width .25s linear,background .25s}
.bar-row strong{text-align:right;font-size:20px;font-weight:700}
.bar-row strong small{font-size:12px;font-weight:600;color:var(--muted);margin-left:3px}
.tiles{display:grid;gap:12px;grid-template-rows:1fr 1fr}
.tile{position:relative;padding:14px 16px 14px 26px;display:flex;flex-direction:column;min-height:120px}
.tile:before{content:'';position:absolute;left:12px;top:16px;height:14px;width:4px;border-radius:2px;background:var(--c)}
.tile span{color:var(--muted);font-size:14px;font-weight:600;letter-spacing:1px}
.tile strong{margin:auto 0;padding-bottom:10px;text-align:center;font-size:48px;font-weight:800;line-height:1}
.tile strong small{font-size:.5em;font-weight:700}
.tile strong em{font-style:normal;font-size:16px;font-weight:600;color:var(--muted);margin-left:4px}
footer{display:flex;align-items:center;gap:10px;margin-top:12px;padding:10px 16px;font-size:15px;font-weight:700;letter-spacing:1px}
.dot{width:10px;height:10px;border-radius:50%;background:var(--dim)}
.dot.on{background:var(--green);box-shadow:0 0 10px rgba(61,220,132,.7)}
.stamp{margin-left:auto;font-weight:500;font-size:12px;color:var(--muted);letter-spacing:.5px}
.overlay{position:fixed;inset:0;display:flex;align-items:center;justify-content:center;padding:16px;
background:rgba(5,8,13,.9);backdrop-filter:blur(4px);-webkit-backdrop-filter:blur(4px);transition:opacity .4s}
.overlay .panel{padding:30px 34px;text-align:center;max-width:380px;width:100%}
.overlay h2{margin:14px 0 6px;font-size:26px;letter-spacing:1.5px}
.overlay p{margin:0;color:var(--muted)}
.absring{display:block;margin:0 auto}
#splash{background:var(--bg);flex-direction:column;z-index:2}
#splash h2{font-size:30px;font-weight:800;letter-spacing:2px;margin:18px 0 18px;animation:fade .8s .2s both}
#splash .ring{stroke-dasharray:0 200;animation:sweep 1s ease-out .05s forwards}
#splash .brk{animation:fade .8s .2s both}
#splash .load{width:140px;height:6px;border-radius:3px;background:var(--track);overflow:hidden}
#splash .load i{display:block;height:100%;width:0;background:var(--accent);border-radius:3px;animation:load 1.5s ease-in-out forwards}
.gone{opacity:0;pointer-events:none}
@keyframes sweep{to{stroke-dasharray:200 0}}@keyframes fade{from{opacity:0}}@keyframes load{to{width:100%}}
[hidden]{display:none!important}
@media(max-width:720px){.grid{grid-template-columns:1fr}.tiles{grid-template-columns:1fr 1fr;grid-template-rows:none}
.tile{min-height:104px}.tile strong{font-size:36px;padding:10px 0 0}.tt{font-size:15px;padding:10px 0}.bar-row{grid-template-columns:62px minmax(0,1fr) 84px}}
@media(prefers-reduced-motion:reduce){*{transition:none!important;animation-duration:.01s!important}}
</style></head><body><main>
<header class="panel"><h1><b>ABS</b> TESTER</h1><span class="pill" id="pill">WAITING</span></header>
<div class="grid">
 <div class="left">
  <section class="panel gauge"><svg viewBox="0 0 260 150" aria-label="Speedometer">
   <defs><radialGradient id="faceG" cx="50%" cy="100%" r="100%"><stop offset="0" stop-color="#132132"/><stop offset="1" stop-color="var(--face)"/></radialGradient></defs>
   <path d="M14 140A116 116 0 0 1 246 140Z" fill="url(#faceG)"/>
   <path d="M10 140A120 120 0 0 1 250 140" fill="none" stroke="var(--edge)" stroke-width="1.2"/>
   <path d="M22 140A108 108 0 0 1 238 140" fill="none" stroke="var(--track)" stroke-width="9"/>
   <path id="lit" d="M22 140A108 108 0 0 1 238 140" fill="none" stroke="var(--accent)" stroke-width="9" pathLength="200" stroke-dasharray="0 200"/>
   <g id="ticks"></g>
   <g class="ndl" id="frontNeedle"><line x1="96" y1="140" x2="27" y2="140" stroke="var(--green)" stroke-width="3" stroke-linecap="round" opacity=".25"/>
    <line x1="96" y1="140" x2="27" y2="140" stroke="var(--green)" stroke-width="1.8" stroke-linecap="round"/></g>
   <g class="ndl" id="needle"><line x1="100" y1="140" x2="26" y2="140" stroke="var(--red)" stroke-width="7" stroke-linecap="round" opacity=".22"/>
    <polygon points="100,137 100,143 25,140.6 25,139.4" fill="var(--red)"/><line x1="100" y1="140" x2="30" y2="140" stroke="#ffb4b4" stroke-width=".9"/></g>
   <rect x="91" y="98" width="78" height="48" rx="9" fill="var(--face)"/>
   <rect x="94" y="101" width="72" height="42" rx="8" fill="var(--plate)" stroke="#22303f"/>
   <text x="130" y="113" text-anchor="middle" font-size="8" fill="var(--muted)" letter-spacing=".5">km/h</text>
   <text x="130" y="138" text-anchor="middle" font-size="26" font-weight="800" fill="var(--text)" class="num" id="speedText">0</text>
  </svg>
  <div class="legend"><span><i style="background:var(--red)"></i>REAR</span><span id="frontLegend"><i style="background:var(--green)"></i>FRONT</span></div></section>
  <section class="lamps">
   <div class="tt" id="absLamp">(ABS)</div><div class="tt" id="tcsLamp">TCS</div><div class="tt" id="sensorLamp">SENSOR</div>
  </section>
  <section class="panel bars">
   <div class="bar-row"><span style="color:var(--red)">REAR</span><div class="track"><div class="fill" id="rearBar"></div></div><strong class="num"><span id="rearText">0</span><small>km/h</small></strong></div>
   <div class="bar-row"><span style="color:var(--muted)">DECLOG</span><div class="track"><div class="fill" id="declogBar" style="background:var(--accent)"></div></div><strong class="num"><span id="declogText">0</span><small>%</small></strong></div>
  </section>
 </div>
 <div class="tiles">
  <div class="panel tile" style="--c:var(--accent)"><span>REF1</span><strong class="num" id="ref1">—</strong></div>
  <div class="panel tile" style="--c:var(--amber)"><span>BATTERY</span><strong class="num" id="battery">—</strong></div>
 </div>
</div>
<footer class="panel"><i class="dot" id="keyDot"></i><span id="keyText">KEY OFF</span><span class="stamp" id="stamp"></span></footer>
</main>
<div class="overlay" id="locked"><div class="panel">
 <svg class="absring" width="96" height="64" viewBox="-48 -32 96 64" aria-hidden="true" id="lockIcon" style="color:var(--amber)">
  <circle r="22" fill="none" stroke="currentColor" stroke-width="4"/>
  <path d="M-30 -14A33 33 0 0 0 -30 14M30 -14A33 33 0 0 1 30 14" fill="none" stroke="currentColor" stroke-width="4" stroke-linecap="round"/>
  <text y="5" text-anchor="middle" font-size="14" font-weight="800" fill="currentColor">ABS</text></svg>
 <h2 id="lockTitle">WAITING FOR NANO</h2><p id="lockText">Connecting to the ABS tester…</p></div></div>
<div class="overlay" id="splash">
 <svg class="absring" width="120" height="80" viewBox="-60 -40 120 80" aria-hidden="true">
  <circle r="28" fill="none" stroke="var(--track)" stroke-width="4"/>
  <circle class="ring" r="28" fill="none" stroke="var(--accent)" stroke-width="4" pathLength="200" transform="rotate(-90)"/>
  <path class="brk" d="M-38 -18A42 42 0 0 0 -38 18M38 -18A42 42 0 0 1 38 18" fill="none" stroke="var(--amber)" stroke-width="4" stroke-linecap="round"/>
  <text class="brk" y="5" text-anchor="middle" font-size="15" font-weight="800" fill="var(--amber)">ABS</text></svg>
 <h2>ABS TESTER</h2><div class="load"><i></i></div></div>
<script>
const $=id=>document.getElementById(id),CX=130,CY=140,MAX=250,RED=210;
const clamp=v=>Math.min(Math.max(+v||0,0),MAX);
const pt=(v,r)=>{const a=Math.PI+clamp(v)/MAX*Math.PI;return[CX+Math.cos(a)*r,CY+Math.sin(a)*r];};
// Ticks every 10 km/h, majors with numbers every 50; red from 210 (same as the LCD).
let t='';for(let v=0;v<=MAX;v+=10){const major=v%50==0,c=v>=RED?'var(--red)':major?'var(--text)':'var(--muted)';
 const[x1,y1]=pt(v,102),[x2,y2]=pt(v,major?90:96);
 t+=`<line x1="${x1}" y1="${y1}" x2="${x2}" y2="${y2}" stroke="${c}" stroke-width="${major?2.4:1.2}" stroke-linecap="round"/>`;
 if(major){const a=v==0?-.17:v==MAX?.17:0,[tx,ty]=pt(v-a*MAX/Math.PI,77);
  t+=`<text x="${tx}" y="${ty+4}" text-anchor="middle" fill="${c}" font-size="11" font-weight="600">${v}</text>`;}}
$('ticks').innerHTML=t;
// Speed colour, as on the LCD: cyan to 140, to amber by 170, amber to red from 200 to 215.
const mix=(a,b,f)=>a.map((x,i)=>Math.round(x+(b[i]-x)*f));
const C={a:[79,210,245],m:[255,181,71],r:[255,75,75]};
function speedColor(v){let c=v<140?C.a:v<170?mix(C.a,C.m,(v-140)/30):v<200?C.m:v<215?mix(C.m,C.r,(v-200)/15):C.r;return`rgb(${c})`;}
const deg=v=>`rotate(${clamp(v)/MAX*180}deg)`;
// Warning lights: "on" steady, "flash" blinks at 2 Hz, "ok" is the green SENSOR.
let lampStates={};
function lamps(){const blink=Math.floor(Date.now()/250)%2;
 for(const[id,s]of Object.entries(lampStates)){const lit=s==='on'||s==='ok'||(s==='flash'&&blink);
  $(id).className='tt'+(lit?' lit':'')+(s==='ok'?' ok':'');}}
setInterval(lamps,125);
function volts(id,v){if(typeof v!=='number'){$(id).textContent='—';return;}
 const [w,f]=v.toFixed(2).split('.');$(id).innerHTML=`${w}<small>.${f}</small><em>V</em>`;}
function show(d){
 const live=d.link==='live',on=live&&d.key;
 $('pill').textContent=!live?(d.link==='stale'?'NO LINK':'WAITING'):d.key?'LIVE':'KEY OFF';
 $('pill').className='pill'+(on?' live':live?' off':'');
 $('locked').classList.toggle('gone',on);
 $('lockTitle').textContent=!live?(d.link==='stale'?'NO LINK':'WAITING FOR NANO'):'UPDATE NANO';
 $('lockText').textContent=live?'Flash the new Nano firmware':d.link==='stale'?'Check the Nano link':'Connecting to the ABS tester…';
 const c=live?'var(--amber)':'var(--red)';$('lockIcon').style.color=c;$('lockTitle').style.color=c;
 $('keyDot').className='dot'+(on?' on':'');$('keyText').textContent=on?'KEY ON':'KEY OFF';
 if(!on)return;
 const v=clamp(d.speed_kmh),fv=d.front?clamp(d.front_speed_kmh):0;
 $('needle').style.transform=deg(v);
 $('frontNeedle').style.transform=deg(fv);$('frontNeedle').style.display=d.front?'':'none';
 $('frontLegend').style.opacity=d.front?1:.35;
 $('lit').setAttribute('stroke-dasharray',`${v/MAX*200} 200`);$('lit').setAttribute('stroke',speedColor(v));
 $('speedText').textContent=Math.round(v);$('speedText').setAttribute('fill',v>=RED?'var(--red)':'var(--text)');
 $('rearBar').style.width=v/MAX*100+'%';$('rearBar').style.background=speedColor(v);$('rearText').textContent=Math.round(v);
 const p=Math.min(Math.max(+d.declog_percent||0,0),100);
 $('declogBar').style.width=p+'%';$('declogText').textContent=Math.round(p);
 lampStates={absLamp:d.abs_lamp,tcsLamp:d.tcs_lamp,sensorLamp:d.sensor_lamp};lamps();
 volts('ref1',d.ref1_v);volts('battery',d.battery_v);
 $('stamp').textContent='Updated '+new Date().toLocaleTimeString();
}
async function poll(){try{const r=await fetch('/api/live',{cache:'no-store'});show(await r.json());}
 catch(e){show({link:'stale'});}setTimeout(poll,250);}
setTimeout(()=>{$('splash').classList.add('gone');setTimeout(()=>$('splash').hidden=true,450);},1700);
poll();
</script></body></html>)rawliteral";
