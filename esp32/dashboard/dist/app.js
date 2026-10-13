'use strict';
const $=id=>document.getElementById(id),names=['IAT','ECT','O2','MAP','TPS'];
let last=null,previousState='',recording=false;const samples=[],recorded=[],events=[];
let recentPacket=null,freshUntil=0,lastSampleKey=null,lastHistoryMs=0;
const numeric=v=>typeof v==='number'&&Number.isFinite(v);
const displaySpeed=d=>numeric(d.simulated_speed_kmh)?d.simulated_speed_kmh:d.engine_rpm;
let faultRequestPending=false,tpsRequestPending=false;
// Key-on self-test and check-engine (MIL) state, animated in animateRpm.
const SELF_TEST_MS=2000;let selfTestStart=-1e9,milFault=false,milBlink=0;
const label=n=>n==='IAT'?'AIT':n==='O2'?'O₂':n;
const format=(v,n=2)=>numeric(v)?v.toFixed(n):'—';
const sensorEndpoints={IAT:[708.3,343.4],ECT:[747.2,61],O2:[0,257.8],MAP:[78.5,889],TPS:[76,893]};
function zeroBasedPercent(name,raw){if(!numeric(raw))return null;const [zero,max]=sensorEndpoints[name];const p=Math.max(0,Math.min(100,(raw-zero)*100/(max-zero)));return p<=2?0:p>=98?100:p;}
const rowCells={};
for(const [i,name] of names.entries()){
 const tr=document.createElement('tr');rowCells[name]=[];
 for(const value of [name==='O2'?'O₂':name==='IAT'?'AIT':name,'A'+i,'—','—','—','Waiting']){const td=document.createElement('td');td.textContent=value;tr.append(td);rowCells[name].push(td);}
 $('sensorRows').append(tr);
}
let ticks='';for(let i=0;i<=30;i++){const a=Math.PI+i/30*Math.PI,p=r=>[200+Math.cos(a)*r,205+Math.sin(a)*r],x=p(165),y=p(i%5===0?149:157);ticks+=`<line x1="${x[0]}" y1="${x[1]}" x2="${y[0]}" y2="${y[1]}" stroke="#8199b5" stroke-width="2"/>`;if(i%5===0){const t=p(130);ticks+=`<text x="${t[0]}" y="${t[1]+5}" text-anchor="middle" fill="#aebdd0" font-size="14">${i*10}</text>`;}}$('ticks').innerHTML=ticks;
function log(message){events.push({time:new Date().toISOString(),message});if(events.length>200)events.shift();const li=document.createElement('li');li.textContent=new Date().toLocaleTimeString()+' · '+message;$('eventLog').prepend(li);while($('eventLog').children.length>100)$('eventLog').lastChild.remove();}
function valid(d){return d&&d.source==='uno_via_esp32'&&d.stale===false&&numeric(d.sample_age_ms)&&d.sample_age_ms>=0&&d.sample_age_ms<=3000&&numeric(d.frequency_hz)&&d.frequency_hz>=0&&numeric(d.engine_rpm)&&d.engine_rpm>=0&&d.engine_rpm<=300&&names.every(n=>d.sensors&&d.sensors[n]&&numeric(d.sensors[n].raw)&&d.sensors[n].raw>=0&&d.sensors[n].raw<=1023&&numeric(d.sensors[n].voltage_v)&&d.sensors[n].voltage_v>=0&&d.sensors[n].voltage_v<=5.1);}
function render(d,state){
 const live=state==='live';last=live?d:null;
 const locked=state==='locked',fresh=live||locked;
 $('connection').textContent=live?'ESP32 · LIVE DATA':state==='stale'?'INPUT DATA STALE':'CONNECTION LOST';
 $('connection').classList.toggle('offline',!live);
 $('sourceBanner').textContent=live?'LIVE INPUTS · Arduino Uno → ESP32 · Actual sensor readings':'Readings unavailable · Waiting for fresh Uno telemetry';
 $('sampleStatus').textContent=live?`Updated ${new Date().toLocaleTimeString()} · sample age ${d.sample_age_ms} ms · rejected packets ${d.bad_frames}`:'Check Uno power and the data link. Previous values are hidden.';
 if(locked){$('connection').textContent='SMART KEY OFF';$('connection').classList.remove('offline');$('sourceBanner').textContent='SMART KEY OFF · Press the Uno D8 button to enable sensor readings';$('sampleStatus').textContent='Acquisition locked. No testbench sensors are being read.';}
 $('keyLamp').dataset.active=String(fresh&&d.smart_key===true);
 $('keyState').textContent=fresh?(d.smart_key?'On · D8':'Off · press D8'):'Unavailable';
 $('brakeLamp').dataset.active=String(fresh&&d.brake_pressed===true);
 $('brakeState').textContent=fresh?(d.brake_pressed?'Pressed · slowing':'Released · D9'):'Unavailable';
 const fault=live&&d.check_engine===true,dtcs=fault&&Array.isArray(d.dtc)?d.dtc:[];
 milFault=fault;milBlink=dtcs.length&&numeric(dtcs[0].blink)?dtcs[0].blink:0;
 const codes=dtcs.map(x=>x.code+' '+label(x.sensor)).join(', ');
 $('engineState').textContent=fault?(codes||'Fault'):live?'No fault':'Off';
 $('coolantLamp').dataset.active=String(live&&d.coolant_warning===true);
 $('coolantState').textContent=live?(d.coolant_warning?'Overheat · ≥105 °C':'Normal'):'Unavailable';
 const injected=live&&(d.injected_fault_mask||0)!==0;
 $('learnClosed').disabled=$('learnWot').disabled=!live||tpsRequestPending;
 if(!tpsRequestPending&&d&&numeric(d.tps_closed_raw)&&numeric(d.tps_wot_raw))$('tpsLearnStatus').textContent=`Learned endpoints · closed ${(d.tps_closed_raw*5/1023).toFixed(3)} V (ADC ${d.tps_closed_raw.toFixed(1)}) · wide-open ${(d.tps_wot_raw*5/1023).toFixed(3)} V (ADC ${d.tps_wot_raw.toFixed(1)})`;
 $('simulateFault').disabled=!live||faultRequestPending;
 $('clearFault').disabled=!injected||faultRequestPending;
 if(!faultRequestPending)$('faultStatus').textContent=injected?'Injected sensor fault active · clear it to restore the gauge':live?'Select a sensor and press Simulate sensor fault.':'Enable the smart key and wait for live data.';
 const absState=live&&d.smart_key===true?(d.brake_pressed?'braking':'released'):'off';
 $('absLamp').dataset.state=absState;
 $('absState').textContent=absState==='braking'?'Brake pressed':absState==='released'?'Brake released':fresh?'Off':'Unavailable';
 if(live&&d.brake_pressed){$('sourceBanner').textContent='BRAKE PRESSED · Simulated speed ramps down to zero · Sensor readings remain live';}
 if(fault){$('sourceBanner').textContent='CHECK ENGINE · '+codes+(injected?' · Demo fault injected · Gauge slowing to zero':' · Sensor circuit fault detected');}
 if(state==='live'&&previousState==='locked')selfTestStart=performance.now();
 if(state!==previousState){log(live?'Live sensor stream connected':locked?'Smart key off; acquisition locked':state==='stale'?'Uno data missing or stale':'ESP32 connection unavailable');previousState=state;}
 for(const name of names){const s=live?d.sensors[name]:null,c=rowCells[name];c[2].textContent=s?format(s.raw,1):'—';c[3].textContent=s?format(s.voltage_v,3)+' V':'—';c[4].textContent=s?format(s.knob_percent,1)+(numeric(s.knob_percent)?'%':''):'—';
  let status='Unavailable';if(s){if(name==='TPS')status=numeric(s.throttle_percent)?format(s.throttle_percent,1)+' % · learned endpoints':'Circuit fault (open/short)';else if(name==='O2')status='λ '+format(s.lambda,2)+' · '+(numeric(s.afr)?'AFR '+format(s.afr,1):'AFR off gauge')+' · '+String(s.mixture||'').toUpperCase();else{const est=name==='MAP'?s.estimate_kpa:s.estimate_c;status=numeric(est)?format(est,1)+(name==='MAP'?' kPa · linear model':' °C · NTC model'):'Circuit fault (open/short)';}if(s.in_spec===false&&!status.startsWith('Circuit fault'))status+=' · outside client spec';if(s.endpoint_status==='out_of_range')status+=' · outside knob endpoints';if(s.raw>=1022)status+=' · ADC near full scale';}c[5].textContent=status;

 }
 for(const name of names){const s=live?d.sensors[name]:null;const value=s?(name==='O2'?s.lambda:name==='TPS'?s.throttle_percent:name==='MAP'?s.estimate_kpa:s.estimate_c):null;$(name.toLowerCase()).textContent=format(value,name==='O2'?2:1);}
 if(!live){$('rpm').textContent=locked?'000':'—';$('rpmBar').textContent=locked?'0.0':'—';}
 $('frequency').textContent=live?format(Math.max(0,Math.min(100,d.frequency_hz/1639*100)),1)+'%':'— %';
 if(!live)setGauge(0);$('frequencyFill').style.width=live?Math.min(100,d.frequency_hz/1639*100)+'%':'0%';
 $('needle').setAttribute('visibility',live?'visible':'hidden');
 renderExtras(d,live,locked);
}
function updateRecording(){$('recordButton').textContent=recording?'Stop recording':'Start recording';$('recordButton').setAttribute('aria-pressed',String(recording));$('recordStatus').textContent=`${recording?'Recording · ':''}${recorded.length} samples / 3600 · export before refreshing`;}
async function poll(){
 const controller=new AbortController(),timer=setTimeout(()=>controller.abort(),1800);
 try{
  const response=await fetch('/api/live',{cache:'no-store',signal:controller.signal});if(!response.ok)throw Error('HTTP');
  const d=await response.json(),live=valid(d);
  const locked=d&&d.source==='uno_via_esp32'&&d.stale===false&&numeric(d.sample_age_ms)&&d.sample_age_ms>=0&&d.sample_age_ms<=3000&&d.smart_key===false&&d.acquisition_enabled===false&&typeof d.brake_pressed==='boolean';
  if(live||locked){recentPacket=d;freshUntil=performance.now()+Math.max(0,3000-d.sample_age_ms);}else{recentPacket=null;freshUntil=0;}
  render(d,live?'live':locked?'locked':'stale');
  const key=numeric(d.sample_seq)?d.sample_seq:JSON.stringify([d.smart_key,d.brake_pressed,d.frequency_hz,d.engine_rpm,d.sensors]);
  const newSample=key!==lastSampleKey;lastSampleKey=key;
  if(live&&newSample&&recording){recorded.push({time:new Date().toISOString(),data:d});if(recorded.length>=3600){recording=false;log('Recording limit reached; export CSV.');}updateRecording();}
 }catch{
  if(recentPacket&&performance.now()<freshUntil){
   $('connection').textContent='RECONNECTING';$('connection').classList.add('offline');
   $('sampleStatus').textContent='Brief connection interruption · retaining the last recent sample';
  }else{recentPacket=null;render(null,'offline');}
 }finally{
  clearTimeout(timer);
  if(Date.now()-lastHistoryMs>=1000){samples.push({time:Date.now(),data:last&&performance.now()<freshUntil?last:null});lastHistoryMs=Date.now();while(samples.length>120)samples.shift();if(!$('signalsPage').hidden)drawHistory();}
  setTimeout(poll,400);
 }
}
// Source-age deadline, independent of request timeouts. Don't keep old readings
// alive just because a request is still pending or retrying.
setInterval(()=>{if(recentPacket&&performance.now()>=freshUntil){recentPacket=null;render(null,'stale');}},250);
function drawHistory(){const ctx=$('historyChart').getContext('2d'),key=$('channel').value,max=key==='frequency_hz'?1700:key==='engine_rpm'?300:key==='simulated_rpm'?15000:5;ctx.clearRect(0,0,900,320);ctx.fillStyle='#acbdd1';ctx.font='14px sans-serif';ctx.fillText(`${key==='IAT'?'AIT':key==='engine_rpm'?'Simulated speed':key==='simulated_rpm'?'Simulated RPM':key} · scale 0–${max} ${names.includes(key)?'V':key==='frequency_hz'?'Hz':key==='simulated_rpm'?'rpm':'simulated km/h'}`,20,25);ctx.strokeStyle='#66d4f1';ctx.lineWidth=2;ctx.beginPath();let connected=false;samples.forEach((r,i)=>{if(!r.data){connected=false;return;}const v=names.includes(key)?r.data.sensors[key].voltage_v:(key==='engine_rpm'?displaySpeed(r.data):r.data[key]),x=30+i/119*840,y=280-v/max*230;if(connected)ctx.lineTo(x,y);else ctx.moveTo(x,y);connected=true;});ctx.stroke();ctx.fillStyle='#acbdd1';ctx.fillText('Oldest → newest · gaps mean data unavailable',30,310);$('chartLegend').textContent=last?'Live inputs · '+samples.length+' history samples':'Waiting for fresh data';}
for(const button of document.querySelectorAll('[data-page]'))button.addEventListener('click',()=>{for(const b of document.querySelectorAll('[data-page]')){const active=b===button;b.setAttribute('aria-pressed',String(active));$(b.dataset.page+'Page').hidden=!active;}drawHistory();});
$('channel').addEventListener('change',drawHistory);
$('recordButton').addEventListener('click',()=>{if(!recording&&recorded.length>=3600){$('recordStatus').textContent='Recording limit reached. Export CSV before refreshing.';return;}recording=!recording;updateRecording();log(recording?'Live recording started':'Recording stopped');});
function csvText(){const header=['timestamp','source','smart_key','brake_pressed','frequency_hz','simulated_speed_kmh','check_engine','fault_mask',...names.flatMap(n=>[(n==='IAT'?'AIT':n)+'_raw',(n==='IAT'?'AIT':n)+'_voltage_v',(n==='IAT'?'AIT':n)+'_knob_percent',(n==='IAT'?'AIT':n)+'_endpoint_status']), 'AIT_c','ECT_c','MAP_kpa','TPS_percent','O2_lambda','O2_afr','O2_mixture','dtc_codes','coolant_warning','throttle_deg','simulated_rpm','gear'];const rows=recorded.map(r=>[r.time,r.data.source,r.data.smart_key,r.data.brake_pressed,r.data.frequency_hz,displaySpeed(r.data),r.data.check_engine,r.data.fault_mask,...names.flatMap(n=>{const s=r.data.sensors[n];return[s.raw,s.voltage_v,s.knob_percent,s.endpoint_status];}),r.data.sensors.IAT.estimate_c,r.data.sensors.ECT.estimate_c,r.data.sensors.MAP.estimate_kpa,r.data.sensors.TPS.throttle_percent,r.data.sensors.O2.lambda,r.data.sensors.O2.afr,r.data.sensors.O2.mixture,(r.data.dtc||[]).map(x=>x.code).join(' '),r.data.coolant_warning,throttleDeg(r.data.sensors.TPS.throttle_percent),r.data.simulated_rpm,r.data.gear]);return[header,...rows].map(row=>row.map(v=>'"'+String(v??'').replaceAll('"','""')+'"').join(',')).join('\r\n');}
$('exportCsv').addEventListener('click',()=>{if(!recorded.length){$('recordStatus').textContent='Start recording to collect fresh samples first.';return;}const url=URL.createObjectURL(new Blob([csvText()],{type:'text/csv;charset=utf-8'})),a=document.createElement('a');a.href=url;a.download='live-testbench-'+Date.now()+'.csv';a.click();setTimeout(()=>URL.revokeObjectURL(url),1000);log('Exported '+recorded.length+' live samples');});
$("printReport").addEventListener("click",()=>{$("printSummary").innerHTML=window.buildSessionReport({model:$("modelName").value,technician:$("technician").value,notes:$("reportNotes").value,generated:Date.now(),latest:last,recorded,history:samples,events});window.print();});
window.liveDashboard={valid,csvText,getState:()=>({last,recorded:recorded.length,history:samples.length}),render};
async function setSimulatedFault(sensor){
 if(faultRequestPending)return;
 faultRequestPending=true;$('simulateFault').disabled=true;$('clearFault').disabled=true;
 $('faultStatus').textContent='Sending demo command…';
 const controller=new AbortController(),timer=setTimeout(()=>controller.abort(),1800);
 try{
  const response=await fetch('/api/simulate-fault',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'sensor='+encodeURIComponent(sensor),signal:controller.signal});
  const data=await response.json();if(!response.ok)throw Error(data.error||'Command failed');
  if(valid(data)){recentPacket=data;freshUntil=performance.now()+Math.max(0,3000-data.sample_age_ms);render(data,'live');}
  log(sensor==='clear'?'Injected sensor fault cleared':'Injected '+(sensor==='IAT'?'AIT':sensor)+' sensor fault');
 }catch(error){$('faultStatus').textContent=error.message==='Command failed'?error.message:'Command not confirmed. Check live status before retrying.';}
 finally{clearTimeout(timer);faultRequestPending=false;$('simulateFault').disabled=!last;$('clearFault').disabled=!last||!(last.injected_fault_mask||0);}
}
$('simulateFault').addEventListener('click',()=>setSimulatedFault($('faultSensor').value));
$('clearFault').addEventListener('click',()=>setSimulatedFault('clear'));
async function tpsLearn(action){
 if(tpsRequestPending)return;
 tpsRequestPending=true;$('learnClosed').disabled=$('learnWot').disabled=true;
 $('tpsLearnStatus').textContent='Saving TPS endpoint…';
 const controller=new AbortController(),timer=setTimeout(()=>controller.abort(),1800);
 try{
  const response=await fetch('/api/tps-learn',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'action='+encodeURIComponent(action),signal:controller.signal});
  const data=await response.json();if(!response.ok)throw Error(data.error||'Command failed');
  tpsRequestPending=false;render(data,valid(data)?'live':previousState);
  log(action==='reset'?'TPS endpoints restored to defaults':'TPS '+(action==='closed'?'closed':'wide-open')+' position learned');
 }catch(error){$('tpsLearnStatus').textContent=error.name==='AbortError'?'Command not confirmed. Check the endpoints before retrying.':error.message;}
 finally{clearTimeout(timer);tpsRequestPending=false;$('learnClosed').disabled=$('learnWot').disabled=!last;}
}
$('learnClosed').addEventListener('click',()=>tpsLearn('closed'));
$('learnWot').addEventListener('click',()=>tpsLearn('wot'));
$('learnReset').addEventListener('click',()=>tpsLearn('reset'));
// Honda-style blink code: long flashes count tens, short flashes units, then a pause.
function milLit(now){
 if(!milFault)return false;
 if(!milBlink||!last||displaySpeed(last)>=1)return true; // Steady while moving.
 const seq=[];for(let i=0;i<Math.floor(milBlink/10);i++)seq.push([1200,600]);for(let i=0;i<milBlink%10;i++)seq.push([400,400]);seq.push([0,2000]);
 let t=now%seq.reduce((a,[on,off])=>a+on+off,0);
 for(const [on,off] of seq){if(t<on)return true;t-=on;if(t<off)return false;t-=off;}
 return false;
}
let animatedRpm=0,animatedEngineRpm=0,lastAnimation=performance.now();
function animateRpm(now){const dt=Math.min(.1,Math.max(0,(now-lastAnimation)/1000));lastAnimation=now;
 // Key-on self-test: every lamp lit and a full needle sweep, like a real cluster.
 const selfTestMs=now-selfTestStart,selfTest=selfTestMs<SELF_TEST_MS;
 for(const el of document.querySelectorAll('.display,.race'))el.classList.toggle('self-test',selfTest);
 const mil=String(selfTest||milLit(now));if($('engineLamp').dataset.active!==mil)$('engineLamp').dataset.active=mil;
 syncRaceLamps();
 if(selfTest&&!$('dashboardPage').hidden){const sweep=300*(selfTestMs<SELF_TEST_MS/2?selfTestMs:SELF_TEST_MS-selfTestMs)/(SELF_TEST_MS/2);
  $('needle').setAttribute('visibility','visible');setGauge(sweep);$('raceSpeed').textContent=String(Math.round(sweep));setRpmGauge(sweep/300*RPM_MAX,true);requestAnimationFrame(animateRpm);return;}
 if(last&&!$('dashboardPage').hidden&&!document.hidden){const before=animatedRpm,difference=displaySpeed(last)-animatedRpm;animatedRpm+=Math.sign(difference)*Math.min(Math.abs(difference),150*dt);
 const digits=String(Math.round(animatedRpm)).padStart(3,'0'),barText=format(animatedRpm,1);
 if($('rpm').textContent!==digits)$('rpm').textContent=digits;
 if($('rpmBar').textContent!==barText)$('rpmBar').textContent=barText;
 const raceDigits=String(Math.round(animatedRpm));if($('raceSpeed').textContent!==raceDigits)$('raceSpeed').textContent=raceDigits;
 if(before!==animatedRpm||$('rpmFill').style.width!==animatedRpm/3+'%'){
 setGauge(animatedRpm);}
 // Engine RPM moves faster than the speed needle so gear changes read as a quick drop.
 const rpmDiff=(numeric(last.simulated_rpm)?last.simulated_rpm:0)-animatedEngineRpm;animatedEngineRpm+=Math.sign(rpmDiff)*Math.min(Math.abs(rpmDiff),20000*dt);setRpmGauge(animatedEngineRpm,true);
 }else if(!last){animatedRpm=0;animatedEngineRpm=0;}requestAnimationFrame(animateRpm);}
requestAnimationFrame(animateRpm);
poll();

// Throttle angle, air-fuel mixture symbols and the race-style display.
// TPS 0-100 % is the throttle-plate angle, 0 deg closed to 90 deg wide open.
const THROTTLE_MAX_DEG=90;
// Client lambda -> gasoline AFR table (AFR = 14.7 x lambda).
const AFR_TABLE=[[0.70,'10.3'],[0.75,'11.0'],[0.80,'11.8'],[0.85,'12.5'],[0.90,'13.2'],[0.95,'14.0'],[1.00,'14.7'],[1.05,'15.4'],[1.10,'16.2'],[1.15,'16.9'],[1.20,'17.6'],[1.25,'18.4'],[1.30,'19.1'],[1.35,'19.8'],[1.40,'20.6'],[1.45,'21.3'],[1.50,'22.1'],[1.55,'22.8'],[1.60,'23.5']];
const LAMBDA_MIN=0.70,LAMBDA_MAX=1.60,MIXTURE_TEXT={rich:'RICH · more fuel than stoichiometric',stoich:'STOICHIOMETRIC · λ ≈ 1',lean:'LEAN · more air than stoichiometric'};
const throttleDeg=pct=>numeric(pct)?pct*THROTTLE_MAX_DEG/100:null;
// Throttle body side view: the butterfly plate is a bar across the full circle,
// vertical when closed (ends at 0 and 180 deg). TP_DIR -1 turns it
// counter-clockwise (wide open ends at 270 and 90 deg); 1 turns it clockwise.
const TP_DIR=-1;
const TP={cx:60,cy:60,r:42},tpPoint=(deg,radius)=>[TP.cx+Math.sin(deg*Math.PI/180)*radius,TP.cy-Math.cos(deg*Math.PI/180)*radius];
const tpArc=(from,to,radius)=>{const [x1,y1]=tpPoint(from,radius),[x2,y2]=tpPoint(to,radius);return `M${x1.toFixed(2)} ${y1.toFixed(2)}A${radius} ${radius} 0 ${to-from>180?1:0} 1 ${x2.toFixed(2)} ${y2.toFixed(2)}`;};
const tpSector=(from,to,radius)=>`M${TP.cx} ${TP.cy}L`+tpArc(from,to,radius).slice(1)+'Z';
function throttleIcon(){
 let ticks='',labels='';
 for(let deg=0;deg<360;deg+=15){const major=deg%90===0,mid=deg%30===0||deg%45===0;if(!major&&!mid)continue;const [x1,y1]=tpPoint(deg,TP.r),[x2,y2]=tpPoint(deg,TP.r-(major?11:6));ticks+=`<line x1="${x1.toFixed(1)}" y1="${y1.toFixed(1)}" x2="${x2.toFixed(1)}" y2="${y2.toFixed(1)}"/>`;}
 for(const deg of [0,90,180,270]){const [x,y]=tpPoint(deg,TP.r+11);labels+=`<text x="${x.toFixed(1)}" y="${y.toFixed(1)}">${deg}°</text>`;}
 return `<svg class="throttle-icon" viewBox="0 0 120 120" aria-hidden="true"><circle class="tp-track" cx="${TP.cx}" cy="${TP.cy}" r="${TP.r}"/><path class="tp-wedge" d=""/><path class="tp-arc" d=""/><g class="tp-ticks">${ticks}</g><g class="tp-labels">${labels}</g><line class="tp-plate" x1="${TP.cx}" y1="${TP.cy-TP.r+2}" x2="${TP.cx}" y2="${TP.cy+TP.r-2}" visibility="hidden"/><circle class="tp-shaft" cx="${TP.cx}" cy="${TP.cy}" r="4"/></svg><span class="tp-read"><b class="tp-deg">—</b><small class="tp-open"></small></span>`;
}
for(const slot of document.querySelectorAll('.throttle-slot'))slot.innerHTML=throttleIcon();
function updateThrottle(deg){
 // Readout is the plate's two end angles, e.g. closed 0° – 180°, wide open 270° – 90°.
 const has=numeric(deg),on=has&&deg>0.2,turn=has?TP_DIR*deg:0,end1=(Math.round(turn)+360)%360,end2=(end1+180)%360;
 const text=has?`${end1}° – ${end2}°`:'—',open=has?`opening ${format(deg,1)}°`:'';
 // Arcs are drawn clockwise, so a counter-clockwise turn spans [-deg, 0].
 const a0=TP_DIR>0?0:-deg,a1=TP_DIR>0?deg:0;
 const wedge=on?tpSector(a0,a1,TP.r)+tpSector(180+a0,180+a1,TP.r):'',arc=on?tpArc(a0,a1,16)+tpArc(180+a0,180+a1,16):'';
 const [x1,y1]=tpPoint(turn,TP.r-2),[x2,y2]=tpPoint(turn+180,TP.r-2);
 for(const el of document.querySelectorAll('.tp-wedge'))if(el.getAttribute('d')!==wedge)el.setAttribute('d',wedge);
 for(const el of document.querySelectorAll('.tp-arc'))if(el.getAttribute('d')!==arc)el.setAttribute('d',arc);
 for(const el of document.querySelectorAll('.tp-plate')){el.setAttribute('x1',x1.toFixed(2));el.setAttribute('y1',y1.toFixed(2));el.setAttribute('x2',x2.toFixed(2));el.setAttribute('y2',y2.toFixed(2));el.setAttribute('visibility',has?'visible':'hidden');}
 for(const el of document.querySelectorAll('.tp-deg'))if(el.textContent!==text)el.textContent=text;
 for(const el of document.querySelectorAll('.tp-open'))if(el.textContent!==open)el.textContent=open;
}
const lambdaX=v=>10+(v-LAMBDA_MIN)/(LAMBDA_MAX-LAMBDA_MIN)*280;
$('lambdaScale').innerHTML=`<svg class="lambda-scale" viewBox="0 0 300 58" role="img" aria-label="Lambda scale from 0.70 rich to 1.60 lean"><rect class="zone-rich" x="10" y="20" width="${lambdaX(0.97)-10}" height="12"/><rect class="zone-stoich" x="${lambdaX(0.97)}" y="20" width="${lambdaX(1.03)-lambdaX(0.97)}" height="12"/><rect class="zone-lean" x="${lambdaX(1.03)}" y="20" width="${290-lambdaX(1.03)}" height="12"/>`+
 [0.70,0.85,1.00,1.15,1.30,1.45,1.60].map(v=>`<line x1="${lambdaX(v)}" y1="32" x2="${lambdaX(v)}" y2="38"/><text x="${lambdaX(v)}" y="50">${v.toFixed(2)}</text>`).join('')+
 `<text class="zone-label" x="${lambdaX(0.83)}" y="10">RICH</text><text class="zone-label" x="${lambdaX(1)}" y="10">λ = 1</text><text class="zone-label" x="${lambdaX(1.32)}" y="10">LEAN</text><g class="lambda-marker" id="lambdaMarker" visibility="hidden"><path d="M0 20L-6 12H6Z"/><line x1="0" y1="18" x2="0" y2="35"/></g></svg>`;
$('afrTable').innerHTML='<tbody><tr><th scope="row">Lambda</th>'+AFR_TABLE.map(([l])=>`<td>${l.toFixed(2)}</td>`).join('')+'</tr><tr><th scope="row">Gasoline AFR</th>'+AFR_TABLE.map(([,a])=>`<td>${a}</td>`).join('')+'</tr></tbody>';
// Small semicircle AFR gauge: needle and centre value show AFR, bands mark rich / stoich / lean.
const AFR_MIN=10,AFR_MAX=24,AG={cx:60,cy:58,r:46};
const agPoint=(afr,radius)=>{const t=Math.max(0,Math.min(1,(afr-AFR_MIN)/(AFR_MAX-AFR_MIN))),a=Math.PI*(1-t);return [AG.cx+Math.cos(a)*radius,AG.cy-Math.sin(a)*radius];};
const agBand=(from,to)=>{const [x1,y1]=agPoint(from,AG.r),[x2,y2]=agPoint(to,AG.r);return `M${x1.toFixed(2)} ${y1.toFixed(2)}A${AG.r} ${AG.r} 0 0 1 ${x2.toFixed(2)} ${y2.toFixed(2)}`;};
function afrGauge(){
 const stoichLow=0.97*STOICH_AFR,stoichHigh=1.03*STOICH_AFR;
 let ticks='';for(const afr of [10,12,14.7,17,20,24]){const [x1,y1]=agPoint(afr,AG.r-6),[x2,y2]=agPoint(afr,AG.r+6),[tx,ty]=agPoint(afr,AG.r+14);ticks+=`<line x1="${x1.toFixed(1)}" y1="${y1.toFixed(1)}" x2="${x2.toFixed(1)}" y2="${y2.toFixed(1)}"/><text x="${tx.toFixed(1)}" y="${ty.toFixed(1)}">${afr}</text>`;}
 return `<svg class="afr-gauge" viewBox="-6 -6 132 92" aria-hidden="true"><path class="ag-rich" d="${agBand(AFR_MIN,stoichLow)}"/><path class="ag-stoich" d="${agBand(stoichLow,stoichHigh)}"/><path class="ag-lean" d="${agBand(stoichHigh,AFR_MAX)}"/><g class="ag-ticks">${ticks}</g><line class="ag-needle" x1="${AG.cx}" y1="${AG.cy}" x2="${AG.cx}" y2="${AG.cy-AG.r+8}" visibility="hidden"/><circle class="ag-pivot" cx="${AG.cx}" cy="${AG.cy}" r="4"/><text class="ag-value" x="${AG.cx}" y="${AG.cy+22}">—</text><text class="ag-unit" x="${AG.cx}" y="${AG.cy-14}">AFR</text></svg>`;
}
const STOICH_AFR=14.7;
for(const slot of document.querySelectorAll('.afr-slot'))slot.innerHTML=afrGauge();
function updateAfrGauge(o2){
 const lambda=o2&&numeric(o2.lambda)?o2.lambda:null,afr=o2&&numeric(o2.afr)?o2.afr:null;
 // Off-table readings peg the needle at the matching end; the value shows —.
 const needleAfr=afr!==null?afr:lambda!==null?lambda*STOICH_AFR:null,[x,y]=needleAfr===null?[AG.cx,AG.cy]:agPoint(needleAfr,AG.r-8),text=afr!==null?format(afr,1):'—';
 for(const el of document.querySelectorAll('.ag-needle')){el.setAttribute('x2',x.toFixed(2));el.setAttribute('y2',y.toFixed(2));el.setAttribute('visibility',needleAfr===null?'hidden':'visible');}
 for(const el of document.querySelectorAll('.ag-value'))if(el.textContent!==text)el.textContent=text;
}
function updateMixture(o2){
 updateAfrGauge(o2);
 const lambda=o2&&numeric(o2.lambda)?o2.lambda:null,mixture=lambda===null?'':o2.mixture||'';
 for(const id of ['lambdaBadge','o2','raceLambda'])$(id).dataset.mixture=mixture;
 $('lambdaValue').textContent=lambda===null?'—':'λ '+format(lambda,2);
 $('afrValue').textContent=lambda===null?'AFR —':numeric(o2.afr)?'AFR '+format(o2.afr,1)+' : 1':'AFR outside the 0.70–1.60 λ table';
 $('mixtureLabel').textContent=lambda===null?'Waiting for O₂ data':MIXTURE_TEXT[mixture]||'';
 const marker=$('lambdaMarker');marker.setAttribute('visibility',lambda===null?'hidden':'visible');
 if(lambda!==null)marker.setAttribute('transform',`translate(${lambdaX(Math.max(LAMBDA_MIN,Math.min(LAMBDA_MAX,lambda))).toFixed(1)} 0)`);
 const row=lambda===null?-1:Math.round((lambda-LAMBDA_MIN)/0.05);
 $('afrTable').querySelectorAll('tr').forEach(tr=>[...tr.querySelectorAll('td')].forEach((td,i)=>td.classList.toggle('current',i===row)));
}
// Race display: curved segmented bar for simulated engine RPM, 0-15,000.
const RPM_MAX=15000;
let raceTicksDrawn=false;
function drawRaceTicks(){
 if(raceTicksDrawn)return;const track=$('raceTrack'),total=track.getTotalLength();if(!total)return;
 let ticks='';for(let v=0;v<=15;v+=3){const p=track.getPointAtLength(v/15*total);// The 0 label sits beside the bar's foot; 300 is right-aligned to stay inside the frame.
 const x=v===0?p.x+34:v===15?p.x+14:p.x,y=v===0?p.y+4:p.y-21;ticks+=`<text x="${x.toFixed(1)}" y="${y.toFixed(1)}"${v===15?' style="text-anchor:end"':''}>${v}</text>`;}
 $('raceTicks').innerHTML=ticks;raceTicksDrawn=true;
}
function setGauge(value){
 const v=Math.max(0,Math.min(300,value)),fill=v/3+'%',dash=v.toFixed(1)+' 300';
 if($('rpmFill').style.width!==fill)$('rpmFill').style.width=fill;
 $('needle').setAttribute('transform',`rotate(${v/300*180} 200 205)`);
}
function setRpmGauge(rpm,show){
 const r=Math.max(0,Math.min(RPM_MAX,rpm)),dash=(r/RPM_MAX*300).toFixed(1)+' 300',fill=(r/RPM_MAX*100).toFixed(2)+'%',text=show?String(Math.round(r/10)*10):'—';
 if($('raceFill').getAttribute('stroke-dasharray')!==dash)$('raceFill').setAttribute('stroke-dasharray',dash);
 if($('engineRpmFill').style.width!==fill)$('engineRpmFill').style.width=fill;
 for(const id of ['engineRpm','raceRpm'])if($(id).textContent!==text)$(id).textContent=text;
}
function syncRaceLamps(){
 for(const [race,classic] of [['raceKey','keyLamp'],['raceBrake','brakeLamp'],['raceEngine','engineLamp'],['raceCoolant','coolantLamp']]){const v=$(classic).dataset.active;if($(race).dataset.active!==v)$(race).dataset.active=v;}
 const abs=$('absLamp').dataset.state;if($('raceAbs').dataset.state!==abs)$('raceAbs').dataset.state=abs;
}
function renderExtras(d,live,locked){
 const s=live?d.sensors:null,pct=s?s.TPS.throttle_percent:null;
 updateThrottle(throttleDeg(pct));updateMixture(s?s.O2:null);
 $('raceEct').textContent=format(s?.ECT.estimate_c,1);$('raceIat').textContent=format(s?.IAT.estimate_c,1);$('raceMap').textContent=format(s?.MAP.estimate_kpa,1);
 $('raceLambda').textContent=format(s?.O2.lambda,2);$('raceAfr').textContent=format(s?.O2.afr,1);$('raceTps').textContent=format(pct,1);
 $('raceFreq').textContent=live?format(d.frequency_hz,0):'—';
 if(!live){$('raceSpeed').textContent=locked?'0':'—';setRpmGauge(0,locked);}
 const gearText=live&&numeric(d.gear)?(d.gear===0?'N':String(d.gear)):locked?'N':'—';
 for(const id of ['gear','raceGear'])if($(id).textContent!==gearText)$(id).textContent=gearText;
}
function setView(view){
 const race=view==='race';$('raceDisplay').hidden=!race;document.querySelector('.display').hidden=race;
 for(const b of document.querySelectorAll('[data-view]'))b.setAttribute('aria-pressed',String(b.dataset.view===view));
 if(race)drawRaceTicks();
 try{localStorage.setItem('ecuDisplayView',view);}catch{}
}
for(const b of document.querySelectorAll('[data-view]'))b.addEventListener('click',()=>setView(b.dataset.view));
try{if(localStorage.getItem('ecuDisplayView')==='race')setView('race');}catch{}
updateThrottle(null);updateMixture(null);
