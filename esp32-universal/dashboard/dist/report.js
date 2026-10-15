'use strict';
window.buildSessionReport=function(session){
 const esc=v=>String(v??'').replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
 const num=v=>typeof v==='number'&&Number.isFinite(v);
 const fmt=(v,n=2)=>num(v)?v.toFixed(n):'Unavailable';
 const stamp=t=>new Date(t).toLocaleString();
 const channels=[['IAT','AIT','Intake air temperature','°C','estimate_c','NTC thermistor model through the client points 3.00 V → 30 °C and 1.72 V → 50 °C.'],['ECT','ECT','Engine coolant temperature','°C','estimate_c','NTC thermistor model through the client points 3.00 V → 20 °C and 0.43 V → 100 °C.'],['O2','O₂','Oxygen sensor lambda','λ','lambda','Client encoding: λ = signal volts, AFR = 14.7 × λ (0.7 V → 10.3, 1.0 V → 14.7, 1.2 V → 17.6).'],['MAP','MAP','Manifold absolute pressure','kPa','estimate_kpa','Linear model through the client points 0.78 V → 20 kPa and 4.00 V → 101.3 kPa.'],['TPS','TPS','Throttle position','%','throttle_percent','Percentage from the TPS endpoints learned on the ESP32.']];
 const recorded=session.recorded||[],history=session.history||[];
 const rows=recorded.length?recorded:history;
 const usable=rows.filter(r=>r.data&&r.data.sensors);
 const latest=session.latest||null;
 const start=rows.length?new Date(rows[0].time).getTime():null,end=rows.length?new Date(rows[rows.length-1].time).getTime():null;
 const mode=recorded.length?'Recorded session':'Recent rolling history (not a full recording)';
 const table=(headers,body)=>'<table><thead><tr>'+headers.map(h=>'<th>'+esc(h)+'</th>').join('')+'</tr></thead><tbody>'+body.map(row=>'<tr>'+row.map(v=>'<td>'+esc(v)+'</td>').join('')+'</tr>').join('')+'</tbody></table>';
 const stats=values=>{const a=values.filter(num);return a.length?[a.length,Math.min(...a),Math.max(...a),a.reduce((x,y)=>x+y,0)/a.length]:[0,null,null,null];};
 const heading=(title,tag)=>'<div class="report-kicker">ECU TESTBENCH / '+esc(tag)+'</div><h1>'+esc(title)+'</h1>';
 const footer='<footer>Simulator input report · No ECU/ABS health verdict · Arduino Uno/Nano → ESP32</footer>';
 const chart=(key,field,unit)=>{
  const points=rows.map(r=>({t:new Date(r.time).getTime(),v:r.data?.sensors?.[key]?.[field]}));
  const available=points.filter(p=>num(p.v)&&num(p.t));
  if(available.length<2||!(end>start))return '<p class="report-empty">History graph unavailable: at least two timestamped valid samples are required.</p>';
  let low=Math.min(...available.map(p=>p.v)),high=Math.max(...available.map(p=>p.v));
  const pad=(high-low||Math.max(1,Math.abs(high)*.05))*.1;low-=pad;high+=pad;
  const x=t=>60+(t-start)/(end-start)*660,y=v=>195-(v-low)/(high-low)*160;
  // Bound SVG size while retaining extrema; null samples break the path.
  const stride=Math.max(1,Math.ceil(points.length/240));let path='',lastTime=null;
  for(let i=0;i<points.length;i+=stride){const bucket=points.slice(i,i+stride);const valid=bucket.filter(p=>num(p.v)&&num(p.t));
   if(valid.length!==bucket.length){lastTime=null;path+=' ';}
   if(!valid.length)continue;
   const picked=[valid[0],valid.reduce((a,b)=>a.v<b.v?a:b),valid.reduce((a,b)=>a.v>b.v?a:b),valid[valid.length-1]].sort((a,b)=>a.t-b.t);
   for(const p of picked){path+=(lastTime===null?'M':'L')+x(p.t).toFixed(1)+' '+y(p.v).toFixed(1)+' ';lastTime=p.t;}
   if(valid.length!==bucket.length)lastTime=null;
  }
  let grid='';for(let i=0;i<=4;i++){const v=low+(high-low)*i/4,yy=y(v);grid+='<line x1="60" y1="'+yy+'" x2="720" y2="'+yy+'" stroke="#dbe2ea"/><text x="52" y="'+(yy+4)+'" text-anchor="end">'+fmt(v,2)+'</text>';}
  return '<figure><svg viewBox="0 0 760 240" role="img" aria-label="'+esc(key+' history in '+unit)+'"><g font-size="11" fill="#475569">'+grid+'<text x="60" y="20">'+esc(unit)+'</text><text x="60" y="220">0 s</text><text x="720" y="220" text-anchor="end">'+fmt((end-start)/1000,1)+' s</text></g><path d="'+path+'" fill="none" stroke="#087e9c" stroke-width="2"/></svg><figcaption>Elapsed session time · plotted range follows recorded values · unavailable samples leave gaps.</figcaption></figure>';
 };
 const summary=channels.map(([key,label,,unit,field])=>{const s=stats(usable.map(r=>r.data.sensors[key]?.[field]));return [label,unit,s[0],fmt(s[1]),fmt(s[2]),fmt(s[3])];});
 let html='<section class="report-page">'+heading('Sensor session report','SESSION OVERVIEW')+'<p class="report-subtitle">'+esc(session.model||'Model not specified')+'</p>'+table(['Session detail','Value'],[['Generated',stamp(session.generated||Date.now())],['Technician',session.technician||'Not specified'],['Data selection',mode],['Samples in selection',rows.length],['Valid sensor packets',usable.length],['Session start',start===null?'Unavailable':stamp(start)],['Session end',end===null?'Unavailable':stamp(end)],['Duration',start===null?'Unavailable':fmt((end-start)/1000,1)+' s'],['Check-engine samples',usable.filter(r=>r.data.check_engine===true).length],['DTC codes seen',[...new Set(usable.flatMap(r=>(r.data.dtc||[]).map(x=>x.code)))].join(', ')||'None'],['Coolant warning samples',usable.filter(r=>r.data.coolant_warning===true).length],['Brake-pressed samples',usable.filter(r=>r.data.brake_pressed===true).length]])+'<h2>Sensor summary</h2>'+table(['Sensor','Unit','Valid values','Minimum','Maximum','Average'],summary)+'<h2>Session notes</h2><p class="report-notes">'+esc(session.notes||'No notes entered.')+'</p><div class="report-callout">Voltages assume a nominal 5.000 V ADC reference. Temperature, pressure and lambda follow models fitted to the client specification, not a physical sensor calibration. Check engine reflects injected demonstration faults or detected sensor circuit faults. This report does not establish ECU or ABS health.</div>'+footer+'</section>';
 for(const [key,label,description,unit,field,calibration] of channels){
  const sensor=latest?.sensors?.[key],lastRecorded=usable.length?usable[usable.length-1].data.sensors[key]:null;
  const metrics=[['Raw ADC','counts','raw',1],['Signal voltage','V','voltage_v',3],[description,unit,field,key==='O2'?3:1]];
  const detail=metrics.map(([name,u,f,d])=>{const s=stats(usable.map(r=>r.data.sensors[key]?.[f]));return [name,u,fmt(sensor?.[f],d),fmt(lastRecorded?.[f],d),fmt(s[1],d),fmt(s[2],d),fmt(s[3],d),s[0]];});
  const faults=usable.filter(r=>(r.data.injected_fault_mask||0)&(1<<channels.findIndex(c=>c[0]===key))).length;
  html+='<section class="report-page">'+heading(label+' · '+description,'SENSOR DETAIL')+'<p class="report-subtitle">Arduino A'+channels.findIndex(c=>c[0]===key)+' · '+esc(calibration)+'</p>'+table(['Measurement','Unit','Live snapshot','Last selected','Min','Max','Average','Valid'],detail)+'<p>Injected fault samples: <strong>'+faults+'</strong>. Unavailable engineering values: <strong>'+usable.filter(r=>!num(r.data.sensors[key]?.[field])).length+'</strong>.</p><h2>'+esc(label)+' history · '+esc(unit)+'</h2>'+chart(key,field,unit)+(field!=='voltage_v'?'<h2>Measured voltage history · V</h2>'+chart(key,'voltage_v','V'):'')+'<div class="report-callout">'+esc(calibration)+' '+'Values beyond the client specification are reported by the model and marked; open or shorted circuits are unavailable.'+'</div>'+footer+'</section>';
 }
 html+='<section class="report-page">'+heading('Controls and event log','SESSION CONTEXT')+'<p>Speed is a simulated 0–300 km/h display mapped from 0–1639 Hz; it is not measured road speed. Brake and injected faults reduce the display only.</p>'+table(['Live control','Snapshot'],[['Smart key',latest?latest.smart_key?'ON':'OFF':'Unavailable'],['Brake',latest?latest.brake_pressed?'Pressed':'Released':'Unavailable'],['Check engine',latest?latest.check_engine?'Injected fault active':'OFF':'Unavailable'],['Frequency',fmt(latest?.frequency_hz,2)+' Hz'],['Simulated speed',fmt(latest?.simulated_speed_kmh??latest?.engine_rpm,1)+' km/h'],['Simulated RPM / gear',fmt(latest?.simulated_rpm,0)+' rpm · '+(latest?(latest.gear===0?'N':latest.gear??'Unavailable'):'Unavailable')]])+'<h2>Recent events</h2>'+table(['Timestamp','Event'],(session.events||[]).slice(-50).map(e=>[stamp(e.time),e.message]))+footer+'</section>';
 return html;
};
