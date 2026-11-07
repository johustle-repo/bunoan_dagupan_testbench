#pragma once
#include <Arduino.h>

// Wi-Fi dashboard for the ABS tester, served from flash at http://192.168.4.1/.
// Same layout as the 2.8" LCD: REF1 and BATTERY in the header, FRONT (left,
// green) and REAR (right, red) 0-250 km/h speedometers with their speed bars,
// the DECLOG bar, and a footer with the round ABS / TCS lights, KEY ON and the
// rear / front / Declog pot generator settings.
// Opens with a short splash like the LCD, then polls /api/live four times a
// second; until live data arrives it shows the WAITING FOR MEGA / NO LINK panel.
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
main{max-width:1000px;margin:0 auto;padding:max(14px,env(safe-area-inset-top)) 16px 20px;display:grid;gap:12px}
.panel{background:linear-gradient(180deg,var(--panel2),var(--panel));border:1px solid var(--edge);border-radius:14px;
box-shadow:0 8px 24px rgba(0,0,0,.35),inset 0 1px 0 rgba(255,255,255,.03)}
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
.gauges{display:grid;grid-template-columns:1fr 1fr;gap:12px}
.gauge{padding:10px 12px 12px;display:grid;gap:8px}
.gauge h2{margin:0;font-size:15px;font-weight:700;letter-spacing:1px;color:var(--c)}
.gauge svg{width:100%;display:block;overflow:visible}
.ndl{transform-box:view-box;transform-origin:130px 140px;transition:transform .25s linear}
.lit{transition:stroke-dasharray .25s linear,stroke .25s}
.bar-row{display:grid;grid-template-columns:minmax(0,1fr) 92px;gap:12px;align-items:center}
.bars{padding:14px 16px}.bars .bar-row{grid-template-columns:70px minmax(0,1fr) 92px}
.bar-row>span{font-weight:700;font-size:14px;letter-spacing:1px;color:var(--muted)}
.track{height:10px;border-radius:5px;background:var(--track);overflow:hidden}
.fill{height:100%;width:0;border-radius:5px;transition:width .25s linear,background .25s}
.bar-row strong{text-align:right;font-size:20px;font-weight:700}
.bar-row strong small{font-size:12px;font-weight:600;color:var(--muted);margin-left:3px}
footer{display:flex;flex-wrap:wrap;align-items:center;gap:10px 20px;padding:10px 16px;font-size:15px;font-weight:700;letter-spacing:1px}
.lamp{display:inline-flex;align-items:center;gap:8px;font-size:17px;font-weight:800;letter-spacing:1.5px;color:var(--offText);transition:color .12s}
.lamp i{width:18px;height:18px;border-radius:50%;background:var(--off);border:2px solid var(--offEdge);transition:background .12s,box-shadow .12s}
.lamp.lit{color:var(--text)}
.lamp.lit i{background:radial-gradient(circle at 35% 35%,#ffe3b3 0,var(--amber) 55%);border-color:transparent;box-shadow:0 0 14px rgba(255,181,71,.75)}
.key{display:inline-flex;align-items:center;gap:8px}
.dot{width:10px;height:10px;border-radius:50%;background:var(--dim)}
.dot.on{background:var(--green);box-shadow:0 0 10px rgba(61,220,132,.7)}
.gens{display:flex;flex-wrap:wrap;gap:6px 16px;font-weight:600;color:var(--muted)}
.gens b{color:var(--accent);font-weight:700}
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
@media(max-width:640px){header{grid-template-columns:1fr 1fr}.brand{grid-column:1/-1;grid-row:1;flex-direction:row;justify-content:space-between}
.tile strong{font-size:28px}.gauges{grid-template-columns:1fr}.bars .bar-row{grid-template-columns:62px minmax(0,1fr) 84px}}
@media(prefers-reduced-motion:reduce){*{transition:none!important;animation-duration:.01s!important}}
</style></head><body><main>
<header>
 <div class="panel tile" style="--c:var(--accent)"><span>REF1</span><strong class="num" id="ref1">—</strong></div>
 <div class="panel brand"><h1><b>ABS</b> TESTER</h1><span class="pill" id="pill">WAITING</span></div>
 <div class="panel tile" style="--c:var(--amber)"><span>BAT</span><strong class="num" id="battery">—</strong></div>
</header>
<div class="gauges">
 <section class="panel gauge" style="--c:var(--green)" id="frontGauge"></section>
 <section class="panel gauge" style="--c:var(--red)" id="rearGauge"></section>
</div>
<section class="panel bars">
 <div class="bar-row"><span>DECLOG</span><div class="track"><div class="fill" id="declogBar" style="background:var(--accent)"></div></div><strong class="num"><span id="declogText">0</span><small>Hz</small></strong></div>
</section>
<footer class="panel">
 <span class="lamp" id="absLamp"><i></i>ABS</span><span class="lamp" id="tcsLamp"><i></i>TCS</span>
 <span class="key"><i class="dot" id="keyDot"></i><span id="keyText">KEY OFF</span></span>
 <span class="gens" id="gens"></span><span class="stamp" id="stamp"></span>
</footer>
</main>
<div class="overlay" id="locked"><div class="panel">
 <svg class="absring" width="96" height="64" viewBox="-48 -32 96 64" aria-hidden="true" id="lockIcon" style="color:var(--amber)">
  <circle r="22" fill="none" stroke="currentColor" stroke-width="4"/>
  <path d="M-30 -14A33 33 0 0 0 -30 14M30 -14A33 33 0 0 1 30 14" fill="none" stroke="currentColor" stroke-width="4" stroke-linecap="round"/>
  <text y="5" text-anchor="middle" font-size="14" font-weight="800" fill="currentColor">ABS</text></svg>
 <h2 id="lockTitle">WAITING FOR MEGA</h2><p id="lockText">Connecting to the ABS tester…</p></div></div>
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
// One speedometer and its speed bar: p is the id prefix, c the needle colour.
function gauge(p,label,c){return`<h2>${label}</h2><svg viewBox="0 0 260 150" aria-label="${label} speed">
 <path d="M14 140A116 116 0 0 1 246 140Z" fill="var(--face)"/>
 <path d="M10 140A120 120 0 0 1 250 140" fill="none" stroke="var(--edge)" stroke-width="1.2"/>
 <path d="M22 140A108 108 0 0 1 238 140" fill="none" stroke="var(--track)" stroke-width="9"/>
 <path class="lit" id="${p}Lit" d="M22 140A108 108 0 0 1 238 140" fill="none" stroke="var(--accent)" stroke-width="9" pathLength="200" stroke-dasharray="0 200"/>
 <g>${t}</g>
 <g class="ndl" id="${p}Needle"><line x1="100" y1="140" x2="26" y2="140" stroke="${c}" stroke-width="7" stroke-linecap="round" opacity=".22"/>
  <polygon points="100,137 100,143 25,140.6 25,139.4" fill="${c}"/><line x1="100" y1="140" x2="30" y2="140" stroke="#fff" stroke-opacity=".55" stroke-width=".9"/></g>
 <rect x="91" y="98" width="78" height="48" rx="9" fill="var(--face)"/>
 <rect x="94" y="101" width="72" height="42" rx="8" fill="var(--plate)" stroke="#22303f"/>
 <text x="130" y="113" text-anchor="middle" font-size="8" fill="var(--muted)" letter-spacing=".5">km/h</text>
 <text x="130" y="138" text-anchor="middle" font-size="26" font-weight="800" fill="var(--text)" class="num" id="${p}Speed">--</text>
</svg><div class="bar-row"><div class="track"><div class="fill" id="${p}Bar"></div></div><strong class="num"><span id="${p}Text">--</span><small>km/h</small></strong></div>`;}
$('frontGauge').innerHTML=gauge('front','FRONT','var(--green)');
$('rearGauge').innerHTML=gauge('rear','REAR','var(--red)');
// Speed colour, as on the LCD: cyan to 140, to amber by 170, amber to red from 200 to 215.
const mix=(a,b,f)=>a.map((x,i)=>Math.round(x+(b[i]-x)*f));
const C={a:[79,210,245],m:[255,181,71],r:[255,75,75]};
function speedColor(v){let c=v<140?C.a:v<170?mix(C.a,C.m,(v-140)/30):v<200?C.m:v<215?mix(C.m,C.r,(v-200)/15):C.r;return`rgb(${c})`;}
function setGauge(p,v,have){
 $(p+'Needle').style.transform=`rotate(${(have?clamp(v):0)/MAX*180}deg)`;$(p+'Needle').style.opacity=have?1:.25;
 $(p+'Lit').setAttribute('stroke-dasharray',`${have?v/MAX*200:0} 200`);$(p+'Lit').setAttribute('stroke',speedColor(v));
 const s=have?Math.round(v):'--';$(p+'Speed').textContent=s;$(p+'Text').textContent=s;
 $(p+'Speed').setAttribute('fill',have&&v>=RED?'var(--red)':'var(--text)');
 $(p+'Bar').style.width=(have?v/MAX*100:0)+'%';$(p+'Bar').style.background=speedColor(v);}
// Round warning lights: "on" steady, "flash" blinks at 2 Hz.
let lampStates={};
function lamps(){const blink=Math.floor(Date.now()/250)%2;
 for(const id of['absLamp','tcsLamp']){const s=lampStates[id];$(id).classList.toggle('lit',s==='on'||(s==='flash'&&!!blink));}}
setInterval(lamps,125);
function volts(id,v){if(typeof v!=='number'){$(id).textContent='—';return;}
 const [w,f]=v.toFixed(2).split('.');$(id).innerHTML=`${w}<small>.${f}</small><em>V</em>`;}
function show(d){
 const live=d.link==='live',on=live&&d.key;
 $('pill').textContent=!live?(d.link==='stale'?'NO LINK':'WAITING'):d.key?'LIVE':'KEY OFF';
 $('pill').className='pill'+(on?' live':live?' off':'');
 $('locked').classList.toggle('gone',on);
 $('lockTitle').textContent=!live?(d.link==='stale'?'NO LINK':'WAITING FOR MEGA'):'UPDATE MEGA';
 $('lockText').textContent=live?'Flash the new Mega firmware':d.link==='stale'?'Check the Mega link':'Connecting to the ABS tester…';
 const c=live?'var(--amber)':'var(--red)';$('lockIcon').style.color=c;$('lockTitle').style.color=c;
 $('keyDot').className='dot'+(on?' on':'');$('keyText').textContent=on?'KEY ON':'KEY OFF';
 // Pot generator settings, when the Mega sends them: rear A1, front A0, Declog A2.
 const g=[['REAR',d.gen_rear_hz],['FRONT',d.gen_front_hz],['DECLOG',d.gen_declog_hz]];
 $('gens').innerHTML=on&&g.every(x=>typeof x[1]==='number')?'POT '+g.map(([n,v])=>`<span>${n} <b>${Math.round(v)}</b> Hz</span>`).join(''):'';
 lampStates=on?{absLamp:d.abs_lamp,tcsLamp:d.tcs_lamp}:{};lamps();
 if(!on)return;
 setGauge('front',clamp(d.front_speed_kmh),!!d.front);
 setGauge('rear',clamp(d.speed_kmh),true);
 const hz=Math.max(+d.declog_hz||0,0),p=Math.min(hz,100);
 $('declogBar').style.width=p+'%';$('declogText').textContent=Math.round(hz);
 volts('ref1',d.ref1_v);volts('battery',d.battery_v);
 $('stamp').textContent='Updated '+new Date().toLocaleTimeString();
}
async function poll(){try{const r=await fetch('/api/live',{cache:'no-store'});show(await r.json());}
 catch(e){show({link:'stale'});}setTimeout(poll,250);}
setTimeout(()=>{$('splash').classList.add('gone');setTimeout(()=>$('splash').hidden=true,450);},1700);
poll();
</script></body></html>)rawliteral";
