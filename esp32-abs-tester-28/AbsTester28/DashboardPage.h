#pragma once
#include <Arduino.h>

// Wi-Fi dashboard for the ABS tester, served from flash at http://192.168.4.1/.
// Same look as the 2.8" LCD: 0-15,000 RPM gauge, REAR and DC LOG bars, REF1 / REF2 /
// CURRENT / BATTERY tiles, key / brake / frequency footer. It polls /api/live
// four times a second; with the key off it shows the "PRESS START" panel.
const char DASHBOARD_HTML[] PROGMEM = R"rawliteral(<!doctype html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>ABS Tester</title>
<style>
:root{--bg:#06090e;--panel:#0b121b;--face:#131d2a;--edge:#26313f;--steel:#5a6a7e;--text:#f2f7fc;--muted:#acbdd1;
--accent:#66d4f1;--amber:#ffbd64;--red:#ff4d4d;--green:#42d97c;--track:#1b2430}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--text);font:15px/1.4 system-ui,Segoe UI,Roboto,sans-serif}
main{max-width:980px;margin:0 auto;padding:14px 16px 20px}
.panel{background:var(--panel);border:1px solid var(--edge);border-radius:10px;box-shadow:inset 0 1px 0 var(--steel)}
header{display:flex;align-items:center;gap:12px;padding:8px 14px;margin-bottom:12px;
background:linear-gradient(#1c2c40,#0a1218);border:1px solid var(--edge);border-radius:10px;box-shadow:inset 0 1px 0 var(--steel)}
header h1{margin:0;font-size:20px;letter-spacing:1px;color:var(--accent)}
.slashes{flex:1;height:14px;background:repeating-linear-gradient(115deg,transparent 0 7px,var(--edge) 7px 9px);
max-width:90px;margin-left:auto}
.pill{padding:3px 12px;border-radius:999px;font-weight:700;font-size:13px;color:var(--bg);background:var(--red)}
.pill.live{background:var(--green)}.pill.off{background:var(--amber)}
.wifi{font-size:12px;color:var(--muted)}
.grid{display:grid;grid-template-columns:minmax(0,2.4fr) minmax(0,1fr);gap:12px}
.left{display:grid;gap:12px}
.gauge{padding:10px 10px 4px}
.gauge svg{width:100%;display:block}
.bars{padding:12px 14px;display:grid;gap:12px}
.bar-row{display:grid;grid-template-columns:58px minmax(0,1fr) 92px;gap:10px;align-items:center}
.tag{background:var(--edge);border-radius:5px;color:var(--muted);font-size:12px;text-align:center;padding:2px 0}
.segs{display:grid;grid-template-columns:repeat(30,1fr);gap:2px;height:16px}
.segs i{background:var(--track);border-radius:1px}
.bar-row strong{text-align:right;font-size:20px;font-weight:500;font-variant-numeric:tabular-nums}
.bar-row strong.hot{color:var(--red)}
.tiles{display:grid;gap:12px;grid-template-rows:repeat(4,1fr)}
.tile{position:relative;padding:10px 14px 10px 20px;display:flex;flex-direction:column;justify-content:space-between;min-height:84px}
.tile:before{content:'';position:absolute;left:7px;top:10px;bottom:10px;width:4px;border-radius:2px;background:var(--c)}
.tile span{color:var(--c);font-size:13px;letter-spacing:.5px}
.tile strong{text-align:right;font-size:24px;font-variant-numeric:tabular-nums}
footer{display:flex;align-items:center;margin-top:12px;padding:8px 14px;font-size:14px}
footer>div{flex:1;display:flex;align-items:center;gap:8px}
footer>div+div{border-left:1px solid var(--edge);padding-left:14px}
footer>div:last-child{justify-content:flex-end;color:var(--muted);font-variant-numeric:tabular-nums}
.lamp{width:11px;height:11px;border-radius:50%;background:#233044;border:1px solid #63738a}
.lamp.on{border-color:transparent}
#keyLamp.on{background:var(--green)}#brakeLamp.on{background:var(--red)}
.locked{position:fixed;inset:0;background:rgba(6,9,14,.88);display:flex;align-items:center;justify-content:center;padding:16px}
.locked .panel{padding:28px 34px;text-align:center;max-width:380px;width:100%}
.locked h2{margin:12px 0 4px;color:var(--amber);font-size:26px;letter-spacing:1px}
.locked p{margin:0;color:var(--muted)}
.locked.fault h2{color:var(--red)}
[hidden]{display:none!important}
@media(max-width:720px){.grid{grid-template-columns:1fr}.tiles{grid-template-columns:1fr 1fr;grid-template-rows:none}
.tile{min-height:72px}.tile strong{font-size:24px}header h1{font-size:17px}.wifi{display:none}}
</style></head><body><main>
<header><h1>ABS TESTER</h1><div class="slashes"></div><span class="wifi">Wi-Fi ABS-Tester · 192.168.4.1</span><span class="pill" id="pill">WAITING</span></header>
<div class="grid">
 <div class="left">
  <section class="panel gauge"><svg viewBox="0 0 260 178" id="gauge" aria-label="RPM gauge">
   <circle cx="130" cy="138" r="122" fill="var(--steel)"/><circle cx="130" cy="138" r="120" fill="var(--edge)"/>
   <circle cx="130" cy="138" r="117" fill="var(--face)"/>
   <path id="track" d="M18 138A112 112 0 0 1 242 138" fill="none" stroke="var(--track)" stroke-width="8"/>
   <path id="lit" d="M18 138A112 112 0 0 1 242 138" fill="none" stroke="var(--accent)" stroke-width="8" pathLength="200" stroke-dasharray="0 200"/>
   <path id="redline" fill="none" stroke="var(--red)" stroke-width="2.5"/>
   <circle cx="130" cy="138" r="70" fill="#0f1722"/>
   <text x="200" y="158" text-anchor="middle" font-size="9" fill="var(--muted)">x1000</text>
   <g id="ticks"></g>
   <g id="needle"><polygon points="130,134 130,142 30,138" fill="var(--red)"/><polygon points="130,134 130,142 147,138" fill="var(--red)"/>
    <line x1="130" y1="138" x2="30" y2="138" stroke="var(--text)" stroke-width=".8"/></g>
   <circle cx="130" cy="138" r="9" fill="var(--steel)"/><circle cx="130" cy="138" r="6.5" fill="var(--edge)"/><circle cx="130" cy="138" r="2.5" fill="var(--red)"/>
   <rect x="78" y="147" width="104" height="27" rx="5" fill="#050a10" stroke="var(--edge)"/>
   <text x="130" y="168" text-anchor="middle" fill="var(--text)" font-size="26" font-weight="700"><tspan id="rpmText">0</tspan><tspan dx="4" font-size="10" font-weight="400" fill="var(--muted)">RPM</tspan></text>
  </svg></section>
  <section class="panel bars">
   <div class="bar-row"><span class="tag">REAR</span><div class="segs" id="rearBar"></div><strong id="rear">0 km/h</strong></div>
   <div class="bar-row"><span class="tag">DC LOG</span><div class="segs" id="dcBar"></div><strong id="dc">0%</strong></div>
  </section>
 </div>
 <div class="tiles">
  <div class="panel tile" style="--c:var(--accent)"><span>REF1</span><strong id="ref1">— V</strong></div>
  <div class="panel tile" style="--c:var(--accent)"><span>REF2</span><strong id="ref2">— V</strong></div>
  <div class="panel tile" style="--c:var(--amber)"><span>CURRENT</span><strong id="current">— A</strong></div>
  <div class="panel tile" style="--c:var(--green)"><span>BATTERY</span><strong id="battery">— V</strong></div>
 </div>
</div>
<footer class="panel"><div><i class="lamp" id="keyLamp"></i><span id="keyText">KEY OFF</span></div>
<div><i class="lamp" id="brakeLamp"></i><span id="brakeText">BRAKE OFF</span></div><div id="hz">0 Hz</div></footer>
</main>
<div class="locked" id="locked"><div class="panel">
 <svg width="64" height="34" viewBox="0 0 64 34" aria-hidden="true"><circle cx="16" cy="17" r="13" fill="currentColor"/><circle cx="16" cy="17" r="6" fill="var(--panel)"/>
 <rect x="27" y="14" width="34" height="7" fill="currentColor"/><rect x="47" y="21" width="5" height="8" fill="currentColor"/><rect x="55" y="21" width="5" height="11" fill="currentColor"/></svg>
 <h2 id="lockTitle">WAITING FOR NANO</h2><p id="lockText">Connecting to the ABS tester…</p></div></div>
<script>
const $=id=>document.getElementById(id),CX=130,CY=138,MAX=200;
const pt=(v,r)=>{const a=Math.PI+Math.min(Math.max(v,0),MAX)/MAX*Math.PI;return[CX+Math.cos(a)*r,CY+Math.sin(a)*r];};
const arc=(v0,v1,r)=>{const[a,b]=pt(v0,r),[c,d]=pt(v1,r);return`M${a} ${b}A${r} ${r} 0 0 1 ${c} ${d}`;};
// The dial is drawn on a 0-200 scale; RPM maps onto it (15,000 rpm = 200).
const RPM_MAX=15000,RPM_RED=12000,rpmV=r=>r/RPM_MAX*MAX;
$('redline').setAttribute('d',arc(rpmV(RPM_RED),200,118));
const rpmColor=r=>r>=RPM_RED?'var(--red)':r>=9000?'var(--amber)':'var(--accent)';
// Ticks every 1,000 rpm, majors (labelled in x1000) every 3,000.
let ticks='';for(let i=0;i<=15;i++){const major=i%3==0,v=rpmV(i*1000),c=i*1000>=RPM_RED?'var(--red)':'var(--text)';
 const[x1,y1]=pt(v,104),[x2,y2]=pt(v,major?92:98);ticks+=`<line x1="${x1}" y1="${y1}" x2="${x2}" y2="${y2}" stroke="${c}" stroke-width="${major?3:1.2}"/>`;
 if(major){const[tx,ty]=pt(v,78);ticks+=`<text x="${tx}" y="${ty+4}" text-anchor="middle" fill="${c}" font-size="12">${i}</text>`;}}
$('ticks').innerHTML=ticks;
for(const id of['rearBar','dcBar'])$(id).innerHTML='<i></i>'.repeat(30);
const speedColor=v=>v>=170?'var(--red)':v>=120?'var(--amber)':'var(--accent)';
function bar(id,f,speed){[...$(id).children].forEach((s,i)=>{const at=i/30,on=at<f;
 s.style.background=!on?'':speed?speedColor(at*MAX):'var(--accent)';});}
const fmt=(v,d)=>typeof v==='number'?v.toFixed(d):'—';
function show(d){
 const live=d.link==='live',on=live&&d.key;
 $('pill').textContent=!live?(d.link==='stale'?'NO LINK':'WAITING'):d.key?'LIVE':'KEY OFF';
 $('pill').className='pill'+(on?' live':live?' off':'');
 $('locked').hidden=on;$('locked').className='locked'+(live?'':' fault');
 $('lockTitle').textContent=!live?(d.link==='stale'?'NO LINK':'WAITING FOR NANO'):'PRESS START';
 $('lockText').textContent=live?'Press the starter button (D8)':'Check Nano D6 → ESP32 GPIO17 and GND';
 $('locked').querySelector('svg').style.color=live?'var(--amber)':'var(--red)';
 if(!on)return;
 const v=d.speed_kmh,shown=Math.min(Math.max(v,0),MAX);
 const rpm=Math.min(Math.max(d.rpm||0,0),RPM_MAX);
 $('needle').setAttribute('transform',`rotate(${rpmV(rpm)/MAX*180} ${CX} ${CY})`);
 $('lit').setAttribute('stroke-dasharray',`${rpmV(rpm)} 200`);
 $('lit').setAttribute('stroke',rpmColor(rpm));
 $('rpmText').textContent=Math.round(rpm);$('rpmText').setAttribute('fill',rpm>=RPM_RED?'var(--red)':'var(--text)');
 bar('rearBar',shown/MAX,true);bar('dcBar',d.dc_log_percent/100,false);
 $('rear').textContent=Math.round(v)+' km/h';$('rear').className=v>=170?'hot':'';
 $('dc').textContent=Math.round(d.dc_log_percent)+'%';
 $('ref1').textContent=fmt(d.ref1_v,2)+' V';$('ref2').textContent=fmt(d.ref2_v,2)+' V';
 $('current').textContent=fmt(d.current_a,2)+' A';$('battery').textContent=fmt(d.battery_v,2)+' V';
 $('keyLamp').className='lamp on';$('keyText').textContent='KEY ON';
 $('brakeLamp').className='lamp'+(d.brake?' on':'');$('brakeText').textContent=d.brake?'BRAKE ON':'BRAKE OFF';
 $('brakeText').style.color=d.brake?'var(--red)':'';$('hz').textContent=Math.round(d.frequency_hz)+' Hz';
}
async function poll(){try{const r=await fetch('/api/live',{cache:'no-store'});show(await r.json());}
 catch(e){show({link:'stale'});}setTimeout(poll,250);}
poll();
</script></body></html>)rawliteral";
