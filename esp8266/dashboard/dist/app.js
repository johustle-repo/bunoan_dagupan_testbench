'use strict';
const $=id=>document.getElementById(id),names=['IAT','ECT','O2','MAP','TPS'];
let last=null,previousState='',recording=false;const samples=[],recorded=[],events=[];
let recentPacket=null,freshUntil=0,lastSampleKey=null,lastHistoryMs=0;
const numeric=v=>typeof v==='number'&&Number.isFinite(v);
const displaySpeed=d=>numeric(d.simulated_speed_kmh)?d.simulated_speed_kmh:d.engine_rpm;
let faultRequestPending=false;
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
function valid(d){return d&&d.source==='uno_via_esp8266'&&d.stale===false&&numeric(d.sample_age_ms)&&d.sample_age_ms>=0&&d.sample_age_ms<=3000&&numeric(d.frequency_hz)&&d.frequency_hz>=0&&numeric(d.engine_rpm)&&d.engine_rpm>=0&&d.engine_rpm<=300&&names.every(n=>d.sensors&&d.sensors[n]&&numeric(d.sensors[n].raw)&&d.sensors[n].raw>=0&&d.sensors[n].raw<=1023&&numeric(d.sensors[n].voltage_v)&&d.sensors[n].voltage_v>=0&&d.sensors[n].voltage_v<=5.1);}
function render(d,state){
 const live=state==='live';last=live?d:null;
 const locked=state==='locked',fresh=live||locked;
 $('connection').textContent=live?'ESP8266 · LIVE DATA':state==='stale'?'INPUT DATA STALE':'CONNECTION LOST';
 $('connection').classList.toggle('offline',!live);
 $('sourceBanner').textContent=live?'LIVE INPUTS · Arduino Uno → ESP8266 · Actual sensor readings':'Readings unavailable · Waiting for fresh Uno telemetry';
 $('sampleStatus').textContent=live?`Updated ${new Date().toLocaleTimeString()} · sample age ${d.sample_age_ms} ms · rejected packets ${d.bad_frames}`:'Check Uno power and the data link. Previous values are hidden.';
 if(locked){$('connection').textContent='SMART KEY OFF';$('connection').classList.remove('offline');$('sourceBanner').textContent='SMART KEY OFF · Press the Uno D8 button to enable sensor readings';$('sampleStatus').textContent='Acquisition locked. No testbench sensors are being read.';}
 $('keyLamp').dataset.active=String(fresh&&d.smart_key===true);
 $('keyState').textContent=fresh?(d.smart_key?'On · D8':'Off · press D8'):'Unavailable';
 $('brakeLamp').dataset.active=String(fresh&&d.brake_pressed===true);
 $('brakeState').textContent=fresh?(d.brake_pressed?'Pressed · slowing':'Released · D9'):'Unavailable';
 const fault=live&&d.check_engine===true;
 $('engineLamp').dataset.active=String(fault);
 const reasons=names.filter((n,i)=>(d?.fault_mask||0)&(1<<i)).map(n=>n==='IAT'?'AIT':n);
 $('engineState').textContent=fault?'Fault · '+reasons.join(', '):live?'No simulated fault':'Off';
 const injected=live&&(d.injected_fault_mask||0)!==0;
 $('simulateFault').disabled=!live||faultRequestPending;
 $('clearFault').disabled=!injected||faultRequestPending;
 if(!faultRequestPending)$('faultStatus').textContent=injected?'Injected sensor fault active · clear it to restore the gauge':live?'Select a sensor and press Simulate sensor fault.':'Enable the smart key and wait for live data.';
 const absState=live&&d.smart_key===true?(d.brake_pressed?'braking':'released'):'off';
 $('absLamp').dataset.state=absState;
 $('absState').textContent=absState==='braking'?'Brake pressed':absState==='released'?'Brake released':fresh?'Off':'Unavailable';
 if(live&&d.brake_pressed){$('sourceBanner').textContent='BRAKE PRESSED · Simulated speed ramps down to zero · Sensor readings remain live';}
 if(fault){$('sourceBanner').textContent='SIMULATED CHECK ENGINE · '+reasons.join(', ')+(injected?' · Fault injected':' outside simulator limits')+' · Gauge slowing to zero';}
 if(state!==previousState){log(live?'Live sensor stream connected':locked?'Smart key off; acquisition locked':state==='stale'?'Uno data missing or stale':'ESP8266 connection unavailable');previousState=state;}
 for(const name of names){const s=live?d.sensors[name]:null,c=rowCells[name];c[2].textContent=s?format(s.raw,1):'—';c[3].textContent=s?format(s.voltage_v,3)+' V':'—';c[4].textContent=s?format(s.knob_percent,1)+(numeric(s.knob_percent)?'%':''):'—';
  let status='Unavailable';if(s){if(name==='TPS')status='Endpoint-based throttle';else if(name==='O2')status='Voltage only · AFR unavailable';else{const est=name==='MAP'?s.estimate_kpa:s.estimate_c;status=numeric(est)?format(est,1)+(name==='MAP'?' kPa':' °C')+' · PROVISIONAL':'Outside provisional curve';}if(s.endpoint_status==='out_of_range')status+=' · outside knob endpoints';if(s.raw>=1022)status+=' · ADC near full scale';}c[5].textContent=status;

 }
 for(const name of names){const s=live?d.sensors[name]:null;const value=s?(name==='O2'?s.voltage_v:name==='TPS'?s.throttle_percent:name==='MAP'?s.estimate_kpa:s.estimate_c):null;$(name.toLowerCase()).textContent=format(value,name==='O2'?3:1);}
 if(!live){$('rpm').textContent=locked?'000':'—';$('rpmBar').textContent=locked?'0.0':'—';}
 $('frequency').textContent=live?format(Math.max(0,Math.min(100,d.frequency_hz/1639*100)),1)+'%':'— %';
 if(!live){$('rpmFill').style.width='0%';$('needle').setAttribute('transform','rotate(0 200 205)');}$('frequencyFill').style.width=live?Math.min(100,d.frequency_hz/1639*100)+'%':'0%';
 $('needle').setAttribute('visibility',live?'visible':'hidden');
}
function updateRecording(){$('recordButton').textContent=recording?'Stop recording':'Start recording';$('recordButton').setAttribute('aria-pressed',String(recording));$('recordStatus').textContent=`${recording?'Recording · ':''}${recorded.length} samples / 3600 · export before refreshing`;}
async function poll(){
 const controller=new AbortController(),timer=setTimeout(()=>controller.abort(),1800);
 try{
  const response=await fetch('/api/live',{cache:'no-store',signal:controller.signal});if(!response.ok)throw Error('HTTP');
  const d=await response.json(),live=valid(d);
  const locked=d&&d.source==='uno_via_esp8266'&&d.stale===false&&numeric(d.sample_age_ms)&&d.sample_age_ms>=0&&d.sample_age_ms<=3000&&d.smart_key===false&&d.acquisition_enabled===false&&typeof d.brake_pressed==='boolean';
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
function drawHistory(){const ctx=$('historyChart').getContext('2d'),key=$('channel').value,max=key==='frequency_hz'?1700:key==='engine_rpm'?300:5;ctx.clearRect(0,0,900,320);ctx.fillStyle='#acbdd1';ctx.font='14px sans-serif';ctx.fillText(`${key==='IAT'?'AIT':key==='engine_rpm'?'Simulated speed':key} · scale 0–${max} ${names.includes(key)?'V':key==='frequency_hz'?'Hz':'simulated km/h'}`,20,25);ctx.strokeStyle='#66d4f1';ctx.lineWidth=2;ctx.beginPath();let connected=false;samples.forEach((r,i)=>{if(!r.data){connected=false;return;}const v=names.includes(key)?r.data.sensors[key].voltage_v:(key==='engine_rpm'?displaySpeed(r.data):r.data[key]),x=30+i/119*840,y=280-v/max*230;if(connected)ctx.lineTo(x,y);else ctx.moveTo(x,y);connected=true;});ctx.stroke();ctx.fillStyle='#acbdd1';ctx.fillText('Oldest → newest · gaps mean data unavailable',30,310);$('chartLegend').textContent=last?'Live inputs · '+samples.length+' history samples':'Waiting for fresh data';}
for(const button of document.querySelectorAll('[data-page]'))button.addEventListener('click',()=>{for(const b of document.querySelectorAll('[data-page]')){const active=b===button;b.setAttribute('aria-pressed',String(active));$(b.dataset.page+'Page').hidden=!active;}drawHistory();});
$('channel').addEventListener('change',drawHistory);
$('recordButton').addEventListener('click',()=>{if(!recording&&recorded.length>=3600){$('recordStatus').textContent='Recording limit reached. Export CSV before refreshing.';return;}recording=!recording;updateRecording();log(recording?'Live recording started':'Recording stopped');});
function csvText(){const header=['timestamp','source','smart_key','brake_pressed','frequency_hz','simulated_speed_kmh','check_engine','fault_mask',...names.flatMap(n=>[(n==='IAT'?'AIT':n)+'_raw',(n==='IAT'?'AIT':n)+'_voltage_v',(n==='IAT'?'AIT':n)+'_knob_percent',(n==='IAT'?'AIT':n)+'_endpoint_status']), 'AIT_provisional_c','ECT_provisional_c','MAP_provisional_kpa','TPS_percent'];const rows=recorded.map(r=>[r.time,r.data.source,r.data.smart_key,r.data.brake_pressed,r.data.frequency_hz,displaySpeed(r.data),r.data.check_engine,r.data.fault_mask,...names.flatMap(n=>{const s=r.data.sensors[n];return[s.raw,s.voltage_v,s.knob_percent,s.endpoint_status];}),r.data.sensors.IAT.estimate_c,r.data.sensors.ECT.estimate_c,r.data.sensors.MAP.estimate_kpa,r.data.sensors.TPS.throttle_percent]);return[header,...rows].map(row=>row.map(v=>'"'+String(v??'').replaceAll('"','""')+'"').join(',')).join('\r\n');}
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
let animatedRpm=0,lastAnimation=performance.now();
function animateRpm(now){const dt=Math.min(.1,Math.max(0,(now-lastAnimation)/1000));lastAnimation=now;
 if(last&&!$('dashboardPage').hidden&&!document.hidden){const before=animatedRpm,difference=displaySpeed(last)-animatedRpm;animatedRpm+=Math.sign(difference)*Math.min(Math.abs(difference),150*dt);
 const digits=String(Math.round(animatedRpm)).padStart(3,'0'),barText=format(animatedRpm,1);
 if($('rpm').textContent!==digits)$('rpm').textContent=digits;
 if($('rpmBar').textContent!==barText)$('rpmBar').textContent=barText;
 if(before!==animatedRpm||$('rpmFill').style.width!==animatedRpm/3+'%'){
 $('rpmFill').style.width=animatedRpm/3+'%';$('needle').setAttribute('transform',`rotate(${animatedRpm/300*180} 200 205)`);}
 }else if(!last)animatedRpm=0;requestAnimationFrame(animateRpm);}
requestAnimationFrame(animateRpm);
poll();
