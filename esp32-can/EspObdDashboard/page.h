// Web page of the ESP32 OBD-II Dashboard (kept in a .h file so the Arduino
// prototype generator does not read its JavaScript as C++).
#pragma once
#include <pgmspace.h>

// The page is plain ASCII (HTML entities / \u escapes), so it shows the same
// whatever character set the browser picks.
const char PAGE[] PROGMEM = R"HTML(<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>OBD-II Scan Tool</title>
<style>
:root{--bg:#0b0e13;--card:#141922;--card2:#1a202b;--line:#262e3b;--text:#e8ecf2;--dim:#8b95a5;--faint:#5c6676;
--ok:#34d399;--warn:#fbbf24;--bad:#f87171;--accent:#60a5fa;--r:14px}
*{box-sizing:border-box}html,body{margin:0}
body{background:radial-gradient(1200px 600px at 20% -10%,#16202f 0,var(--bg) 60%);background-attachment:fixed;color:var(--text);
font:15px/1.45 system-ui,-apple-system,Segoe UI,Roboto,sans-serif;min-height:100vh}
header{display:flex;align-items:center;gap:16px;flex-wrap:wrap;padding:14px 22px;border-bottom:1px solid var(--line);
background:rgba(11,14,19,.8);backdrop-filter:blur(8px);position:sticky;top:0;z-index:5}
.brand{display:flex;align-items:center;gap:12px}
.logo{width:36px;height:36px;border-radius:10px;background:linear-gradient(135deg,#2563eb,#60a5fa);display:grid;place-items:center}
h1{font-size:17px;margin:0;letter-spacing:.02em}
.subtitle{color:var(--dim);font-size:12.5px}
.chain{display:flex;align-items:center;gap:6px;margin-left:auto;font-size:12.5px;color:var(--dim)}
.node{display:flex;align-items:center;gap:6px;padding:4px 10px;border:1px solid var(--line);border-radius:999px;background:var(--card)}
.node i{width:8px;height:8px;border-radius:50%;background:var(--faint)}
.node.on i{background:var(--ok);box-shadow:0 0 8px var(--ok)}.node.off i{background:var(--bad)}
.wire{width:18px;height:2px;background:var(--line)}
.pill{font-weight:700;font-size:12.5px;letter-spacing:.06em;padding:6px 12px;border-radius:999px;border:1px solid var(--line);background:var(--card)}
main{padding:20px 22px;max-width:1180px;margin:auto;display:grid;grid-template-columns:minmax(0,1fr);gap:16px}
.card{background:linear-gradient(180deg,var(--card2),var(--card));border:1px solid var(--line);border-radius:var(--r);padding:16px 18px}
.top{display:grid;gap:16px;grid-template-columns:minmax(280px,380px) 1fr}
@media(max-width:800px){.top{grid-template-columns:minmax(0,1fr)}.chain{margin-left:0}main{padding:14px 12px}}
.label{color:var(--dim);font-size:12px;font-weight:600;letter-spacing:.09em;text-transform:uppercase}
.label b{color:var(--faint);font-weight:600}
.slabel{display:flex;justify-content:space-between;gap:8px;white-space:nowrap}
.slabel span{overflow:hidden;text-overflow:ellipsis}
.tach{display:flex;flex-direction:column;align-items:center}
.tach .label{align-self:flex-start}
#gauge{width:100%;max-width:330px;display:block}
#gauge text{font:600 11px system-ui;fill:var(--dim)}
#gauge #rpmv{font:800 36px system-ui;fill:var(--text);font-variant-numeric:tabular-nums}
#gauge #rpmu{font:600 12px system-ui;letter-spacing:.15em}
#gauge #rpms{font:600 12.5px system-ui}
.diag{display:flex;flex-direction:column;gap:12px;min-width:0}
.diaghead{display:flex;align-items:center;gap:14px;flex-wrap:wrap}
.mil{display:flex;align-items:center;gap:12px}
.mil svg{width:54px;height:34px;color:#3a4352;transition:.2s}
.mil.on svg{color:var(--warn);filter:drop-shadow(0 0 8px rgba(251,191,36,.7))}
.mil b{display:block;font-size:16px}.mil span{color:var(--dim);font-size:12.5px}
.spacer{flex:1}
button{font:600 14px system-ui;padding:10px 16px;border-radius:10px;border:1px solid var(--line);background:#202836;color:var(--text);
cursor:pointer;display:inline-flex;align-items:center;gap:8px;transition:.15s}
button:hover{border-color:#3a4558;background:#283243}
button:disabled{opacity:.55;cursor:wait}
button.danger{border-color:#5b2a2a;background:#2a1719;color:#fecaca}
button.danger:hover{background:#3a1d20}
button.confirm,button.confirm:hover{background:var(--bad);border-color:var(--bad);color:#1b0b0b}
.spin{width:14px;height:14px;border:2px solid currentColor;border-right-color:transparent;border-radius:50%;animation:sp .7s linear infinite;display:none}
button.busy .spin{display:inline-block}
@keyframes sp{to{transform:rotate(360deg)}}
#codes{display:grid;gap:8px}
.dtc{display:grid;grid-template-columns:auto minmax(0,1fr) auto;align-items:center;gap:14px;padding:10px 14px;border-radius:10px;
background:rgba(251,191,36,.07);border:1px solid rgba(251,191,36,.25)}
.dtc.pend{background:transparent;border-style:dashed}
.dtc .code{font:700 19px ui-monospace,Consolas,monospace;color:var(--warn);letter-spacing:.04em}
.dtc .t{font-weight:600}.dtc .h{color:var(--dim);font-size:12.5px}
.badge{font-size:11px;font-weight:700;letter-spacing:.08em;text-transform:uppercase;padding:3px 8px;border-radius:6px;
background:rgba(251,191,36,.15);color:var(--warn)}
.pend .badge{background:transparent;border:1px solid rgba(251,191,36,.4)}
.empty{display:flex;align-items:center;gap:10px;padding:14px;border-radius:10px;border:1px solid rgba(52,211,153,.25);
background:rgba(52,211,153,.06);color:var(--ok);font-weight:600}
.muted{color:var(--dim);font-size:13px}
.grid{display:grid;gap:16px;grid-template-columns:repeat(auto-fit,minmax(min(170px,100%),1fr))}
.val{font-size:34px;font-weight:800;font-variant-numeric:tabular-nums;margin:6px 0 2px;letter-spacing:-.01em}
.unit{font-size:15px;color:var(--dim);font-weight:600;margin-left:5px}
.bar{position:relative;height:8px;border-radius:99px;background:#232b38;margin:12px 0 8px}
.bar .z{position:absolute;top:0;bottom:0;opacity:.35}
.z.ok{background:var(--ok)}.z.warn{background:var(--warn)}.z.bad{background:var(--bad)}
.bar .m{position:absolute;top:50%;width:14px;height:14px;margin:-7px 0 0 -7px;border-radius:50%;background:var(--text);
border:3px solid var(--card);box-shadow:0 0 0 1px #0006;transition:left .25s;display:none}
.st{display:flex;justify-content:space-between;gap:8px;font-size:12.5px;color:var(--dim)}
.st b{font-weight:700}
.c-ok{color:var(--ok)}.c-warn{color:var(--warn)}.c-bad{color:var(--bad)}.c-dim{color:var(--dim)}
.log{display:grid;gap:2px;max-height:260px;overflow:auto}
.ev{display:grid;grid-template-columns:10px 1fr auto;gap:10px;align-items:baseline;padding:6px 2px;border-bottom:1px solid #1e2531;font-size:13.5px}
.ev:last-child{border:0}
.ev i{width:8px;height:8px;border-radius:50%;background:var(--faint)}
.ev.l1 i{background:var(--ok)}.ev.l2 i{background:var(--warn)}.ev.l3 i{background:var(--bad)}
.ev small{color:var(--faint);font-variant-numeric:tabular-nums}
footer{color:var(--faint);font-size:12px;text-align:center;padding:2px 18px 22px}
#toast{position:fixed;left:50%;bottom:22px;transform:translate(-50%,30px);opacity:0;pointer-events:none;transition:.25s;width:max-content;max-width:min(640px,92vw);
padding:12px 16px;border-radius:12px;background:#1f2633;border:1px solid var(--line);box-shadow:0 10px 30px #0008;font-weight:600;z-index:9}
#toast.show{opacity:1;transform:translate(-50%,0)}
#toast.l1{border-color:var(--ok)}#toast.l2{border-color:var(--warn)}#toast.l3{border-color:var(--bad)}
</style></head><body>
<header>
  <div class="brand">
    <div class="logo"><svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="#fff" stroke-width="2.2" stroke-linecap="round"><path d="M4 15a8 8 0 1 1 16 0"/><path d="M12 15l4-5"/></svg></div>
    <div><h1>OBD-II Scan Tool</h1><div class="subtitle">Set 2 diagnostic bench &middot; CAN 500 kbps &middot; ISO 15765-4</div></div>
  </div>
  <div class="chain">
    <span class="node" id="nEsp"><i></i>ESP32</span><span class="wire"></span>
    <span class="node" id="nTst"><i></i>Tester</span><span class="wire"></span>
    <span class="node" id="nEcu"><i></i>ECU</span>
  </div>
  <span class="pill" id="state">CONNECTING</span>
</header>
<main>
<section class="top">
  <div class="card tach">
    <div class="label">Engine speed <b>&middot; PID 0C</b></div>
    <svg id="gauge" viewBox="0 0 240 205"></svg>
  </div>
  <div class="card diag">
    <div class="diaghead">
      <div class="mil" id="mil">
        <svg viewBox="0 0 64 40" fill="currentColor"><path d="M20 4h16v4h-5v4h11l5 5h5v-5h6v22h-6v-5h-5l-6 7H22l-5-5h-6v6H5V12h6v6h5l4-4V8z"/></svg>
        <div><b id="miltext">MIL OFF</b><span id="count">&ndash;</span></div>
      </div>
      <span class="spacer"></span>
      <button id="bRead" onclick="cmd('read')"><span class="spin"></span>Read DTC</button>
      <button id="bClear" class="danger" onclick="clearClick()"><span class="spin"></span><span id="clearTxt">Clear DTC</span></button>
    </div>
    <div id="codes"><div class="muted">Waiting for the first DTC read&hellip;</div></div>
    <div class="muted" id="age"></div>
  </div>
</section>
<section class="grid" id="sensors"></section>
<section class="card">
  <div class="label" style="margin-bottom:8px">Activity</div>
  <div class="log" id="log"><div class="muted">No activity yet</div></div>
</section>
</main>
<footer id="foot">ECU simulator &rarr; CAN 500 kbps OBD-II &rarr; Nano tester &rarr; ESP32</footer>
<div id="toast"></div>
<script>
const DEG=String.fromCharCode(176),DOT=' '+String.fromCharCode(183)+' ',DASH=String.fromCharCode(8211);
const DESC={
P0106:['MAP range / performance','MAP too high with the throttle closed'],
P0107:['MAP circuit low','Sensor signal shorted to ground'],
P0108:['MAP circuit high','Open circuit or short to 5 V'],
P0111:['IAT range / performance','Intake air hotter than its limit'],
P0112:['IAT circuit low','Sensor signal shorted to ground'],
P0113:['IAT circuit high','Open circuit or sensor unplugged'],
P0117:['ECT circuit low','Sensor signal shorted to ground'],
P0118:['ECT circuit high','Open circuit or sensor unplugged'],
P0121:['TPS range / performance','Throttle open with no engine load'],
P0122:['TPS circuit low','Sensor signal shorted to ground'],
P0123:['TPS circuit high','Open circuit or short to 5 V'],
P0131:['O2 sensor B1S1 low voltage','Signal shorted to ground'],
P0132:['O2 sensor B1S1 high voltage','Signal shorted to voltage'],
P0171:['System too lean (bank 1)','O2 stays below 0.2 V: vacuum leak or low fuel pressure'],
P0172:['System too rich (bank 1)','O2 stays above 0.8 V: leaking injector or high fuel pressure'],
P0217:['Engine coolant over temperature','Check the coolant level and the cooling fan'],
P0219:['Engine overspeed condition','Engine speed went above 6500 rpm'],
P0335:['Crankshaft position sensor A circuit','No engine speed signal']};
const S=[
{k:'ect',n:'Coolant temp',p:'05',u:DEG+'C',lo:20,hi:120,d:0,z:[[20,105,'ok'],[105,115,'warn'],[115,120,'bad']]},
{k:'iat',n:'Intake air temp',p:'0F',u:DEG+'C',lo:15,hi:55,d:0,z:[[15,45,'ok'],[45,50,'warn'],[50,55,'bad']]},
{k:'map',n:'Manifold press.',p:'0B',u:'kPa',lo:10,hi:100,d:0},
{k:'tps',n:'Throttle',p:'11',u:'%',lo:0,hi:100,d:0},
{k:'o2',n:'O2 sensor B1S1',p:'14',u:'V',lo:0,hi:1.2,d:2,z:[[0,.1,'bad'],[.1,.2,'warn'],[.2,.8,'ok'],[.8,.9,'warn'],[.9,1.2,'bad']]}];
const $=id=>document.getElementById(id);
const esc=s=>String(s).replace(/[&<>"]/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[c]));
function pct(s,v){return Math.max(0,Math.min(100,(v-s.lo)/(s.hi-s.lo)*100))}

// ---------- sensor cards ----------
$('sensors').innerHTML=S.map(s=>{
  const zones=(s.z||[]).map(z=>'<span class="z '+z[2]+'" style="left:'+pct(s,z[0])+'%;width:'+(pct(s,z[1])-pct(s,z[0]))+'%"></span>').join('')||
    '<span class="z" style="left:0;width:100%;background:var(--accent);opacity:.18"></span>';
  return '<div class="card"><div class="label slabel"><span>'+s.n+'</span><b>PID '+s.p+'</b></div>'+
   '<div class="val"><span id="v_'+s.k+'">'+DASH+'</span><span class="unit">'+s.u+'</span></div>'+
   '<div class="bar">'+zones+'<span class="m" id="m_'+s.k+'"></span></div>'+
   '<div class="st"><b id="s_'+s.k+'" class="c-dim">No data</b><span id="x_'+s.k+'"></span></div></div>';
}).join('');
function status(k,v,d){
  if(v===null)return['No data','dim'];
  switch(k){
    case 'ect':return v>115?['Overheating','bad']:v>105?['Running hot','warn']:v<60?['Warming up','ok']:['Normal','ok'];
    case 'iat':return v>50?['Too hot','bad']:v>45?['Hot','warn']:['Normal','ok'];
    case 'o2':return v<.1?['Very lean','bad']:v<.2?['Lean','warn']:v>.9?['Very rich','bad']:v>.8?['Rich','warn']:['Normal','ok'];
    case 'map':if(d.tps===null||d.tps>5)return['Throttle open','ok'];return v>80?['High at idle','bad']:v>60?['High at idle','warn']:['Normal at idle','ok'];
    case 'tps':if(d.map===null||d.map>30)return['Normal','ok'];return v>80?['Open, no load','bad']:v>50?['Open, no load','warn']:['Normal','ok'];
  }
}

// ---------- tachometer ----------
const G={cx:120,cy:118,r:92,max:8000,a0:135,sweep:270};
const ang=v=>G.a0+G.sweep*Math.max(0,Math.min(v,G.max))/G.max;
const pt=(a,r)=>{const t=a*Math.PI/180;return[(G.cx+r*Math.cos(t)).toFixed(1),(G.cy+r*Math.sin(t)).toFixed(1)]};
function arc(v0,v1,r){const a=ang(v0),b=ang(v1),p=pt(a,r),q=pt(b,r);return 'M'+p+' A'+r+' '+r+' 0 '+(b-a>180?1:0)+' 1 '+q}
(function(){
  let h='<path d="'+arc(0,8000,G.r)+'" stroke="#232b38" stroke-width="14" fill="none" stroke-linecap="round"/>'+
    '<path d="'+arc(6000,6500,G.r+12)+'" stroke="var(--warn)" stroke-width="4" fill="none"/>'+
    '<path d="'+arc(6500,8000,G.r+12)+'" stroke="var(--bad)" stroke-width="4" fill="none"/>'+
    '<path id="rpmArc" d="" stroke="var(--accent)" stroke-width="14" fill="none" stroke-linecap="round"/>';
  for(let v=0;v<=8000;v+=500){
    const a=ang(v),big=v%1000==0,p=pt(a,G.r-12),q=pt(a,G.r-(big?22:17));
    h+='<line x1="'+p[0]+'" y1="'+p[1]+'" x2="'+q[0]+'" y2="'+q[1]+'" stroke="'+(v>6500?'var(--bad)':'#4a5566')+'" stroke-width="'+(big?2:1)+'"/>';
    if(big){const t=pt(a,G.r-34);h+='<text x="'+t[0]+'" y="'+(+t[1]+4)+'" text-anchor="middle">'+v/1000+'</text>'}
  }
  h+='<text id="rpmv" x="120" y="132" text-anchor="middle">'+DASH+'</text>'+
    '<text id="rpmu" x="120" y="150" text-anchor="middle">RPM</text>'+
    '<text x="120" y="172" text-anchor="middle" style="font-size:10px;fill:var(--faint)">x1000 r/min</text>'+
    '<text id="rpms" x="120" y="196" text-anchor="middle">No data</text>';
  $('gauge').innerHTML=h;
})();
function drawRpm(v){
  const col=v===null?'#4a5566':v>6500?'var(--bad)':v>6000?'var(--warn)':'var(--accent)';
  $('rpmArc').setAttribute('d',v>0?arc(0,v,G.r):'');
  $('rpmArc').setAttribute('stroke',col);
  $('rpmv').textContent=v===null?DASH:Math.round(v);
  const s=v===null?['No data','dim']:v>6500?['Over-rev','bad']:v>6000?['High','warn']:v==0?['Engine stopped / no signal','warn']:v<1000?['Idle','ok']:['Running','ok'];
  const e=$('rpms');e.textContent=s[0];e.style.fill='var(--'+s[1]+')';
}

// ---------- commands ----------
let confirmT=0;
function resetClear(){$('bClear').classList.remove('confirm');$('clearTxt').textContent='Clear DTC'}
function clearClick(){
  const b=$('bClear');
  clearTimeout(confirmT);
  if(!b.classList.contains('confirm')){b.classList.add('confirm');$('clearTxt').textContent='Confirm clear?';confirmT=setTimeout(resetClear,3000);return}
  resetClear();cmd('clear');
}
async function cmd(c){
  $(c=='read'?'bRead':'bClear').classList.add('busy');
  try{await fetch('/api/cmd?c='+c,{cache:'no-store'})}catch(e){toast('ESP32 not reachable',3)}
}
let toastT=0,lastToast='';
function toast(m,l){const t=$('toast');t.textContent=m;t.className='show l'+l;clearTimeout(toastT);toastT=setTimeout(()=>t.className='',6000)}
function ago(ms){const s=ms/1000;return s<1?'now':s<60?Math.round(s)+' s ago':Math.round(s/60)+' min ago'}

// ---------- refresh ----------
function setNode(id,on){$(id).className='node '+(on?'on':'off')}
async function tick(){
  let d;
  try{d=await (await fetch('/api/live',{cache:'no-store'})).json()}
  catch(e){$('state').textContent='ESP32 OFFLINE';$('state').style.color='var(--bad)';setNode('nEsp',0);setNode('nTst',0);setNode('nEcu',0);return}
  setNode('nEsp',1);setNode('nTst',d.tester);setNode('nEcu',d.ecu);
  $('state').textContent=d.state;$('state').style.color=d.ecu?'var(--ok)':'var(--bad)';
  drawRpm(d.rpm);
  for(const s of S){
    const v=d[s.k];$('v_'+s.k).textContent=v===null?DASH:Number(v).toFixed(s.d);
    const m=$('m_'+s.k);if(v===null)m.style.display='none';else{m.style.display='block';m.style.left=pct(s,v)+'%'}
    const st=status(s.k,v,d),e=$('s_'+s.k);e.textContent=st[0];e.className='c-'+st[1];
  }
  $('x_o2').textContent=d.afr===null?'':'AFR '+d.afr.toFixed(1)+':1';
  $('mil').classList.toggle('on',d.mil);$('miltext').textContent=d.mil?'CHECK ENGINE'+DOT+'MIL ON':'MIL OFF';
  $('count').textContent=d.dtcCount+' stored DTC'+(d.dtcCount==1?'':'s')+' reported in PID 01';
  const pend=d.pend.filter(x=>!d.dtcs.includes(x));
  const row=(x,p)=>{const i=DESC[x]||['Unknown code','Not in this tool\'s list'];
    return '<div class="dtc'+(p?' pend':'')+'"><div class="code">'+esc(x)+'</div><div><div class="t">'+esc(i[0])+'</div><div class="h">'+esc(i[1])+'</div></div><span class="badge">'+(p?'Pending':'Stored')+'</span></div>'};
  $('codes').innerHTML=!d.dtcRead?'<div class="muted">Waiting for the first DTC read&hellip;</div>':
    (d.dtcs.length||pend.length)?d.dtcs.map(x=>row(x,0)).join('')+pend.map(x=>row(x,1)).join(''):
    '<div class="empty"><svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="3"><path d="M5 12l5 5 9-10"/></svg>No DTCs stored or pending</div>';
  $('age').textContent=d.dtcRead?'DTCs read '+ago(d.dtcAge)+DOT+'read every 2 s':'';
  $('bRead').classList.toggle('busy',d.busy=='read');$('bRead').disabled=d.busy!='';
  $('bClear').classList.toggle('busy',d.busy=='clear');$('bClear').disabled=d.busy!='';
  $('log').innerHTML=d.log.length?d.log.map(e=>'<div class="ev l'+e.lv+'"><i></i><span>'+esc(e.msg)+'</span><small>'+ago(e.age)+'</small></div>').join(''):'<div class="muted">No activity yet</div>';
  const n=d.log[0];
  if(n&&n.lv>0&&n.age<1500&&n.msg!=lastToast){lastToast=n.msg;toast(n.msg,n.lv)}
  $('foot').textContent='answers '+d.answers+DOT+'no answer '+d.noResp+DOT+'link errors '+d.bad+DOT+'PIDs 01-20: '+d.supported.toUpperCase();
}
setInterval(tick,300);tick();
</script></body></html>)HTML";
