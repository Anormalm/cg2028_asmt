'use strict';
const $ = id => document.getElementById(id);
const esc = value => String(value ?? '').replace(/[&<>"']/g, c => ({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
const human = value => String(value || '—').replaceAll('_', ' ');
const date = value => value ? new Date(value * 1000).toLocaleString() : '—';
const key = c => `${c.device}/${c.boot}/${c.capture_id}`;
let token = sessionStorage.getItem('eldercare-token') || '', state = {devices:[],events:[],captures:[]};
let busy = false, connected = false, selected = null, selectedRows = -1, captureLoad = 0, audio = null, sound = false, seen = null;
async function api(path, body) {
  const response = await fetch(path, {headers:{Authorization:`Bearer ${token}`, 'Content-Type':'application/json'},
    method:body === undefined ? 'GET':'POST', body:body === undefined ? undefined:JSON.stringify(body)});
  if (!response.ok) { const error = await response.json().catch(() => ({})); throw Error(error.error || `Receiver returned ${response.status}`); }
  return response;
}
function matching(items) { return items.filter(x => !$('deviceFilter').value || x.device === $('deviceFilter').value); }
function page(name) {
  document.querySelectorAll('.page').forEach(x => x.hidden = x.id !== name);
  document.querySelectorAll('nav [data-page]').forEach(x => x.classList.toggle('selected', x.dataset.page === name));
  $('pageTitle').textContent = {monitor:'Activity & alerts',trials:'Motion trials',events:'Event log'}[name];
}
document.querySelectorAll('[data-page]').forEach(x => x.onclick = () => page(x.dataset.page));
function ackHTML(e) {
  if (e.type === 'local_ack') return '<span class="muted">Local acknowledgement</span>';
  if (!['fall','sos'].includes(e.type)) return '<span class="muted">—</span>';
  if (e.caregiver_ack) return `<span class="muted">Seen ${esc(date(e.caregiver_ack))}</span>`;
  return `<button class="quiet" data-ack="${state.events.indexOf(e)}">Mark seen</button>`;
}
function bindAck(root) {
  root.querySelectorAll('[data-ack]').forEach(button => button.onclick = async ev => {
    ev.stopPropagation(); button.disabled = true;
    const e = state.events[Number(button.dataset.ack)];
    try { await api('/api/ack', {device:e.device,boot:e.boot,incident:e.incident}); await refresh(); }
    catch(error) { $('status').textContent = error.message; button.disabled = false; }
  });
}
function render() {
  const devices = matching(state.devices), events = matching(state.events);
  $('deviceCount').textContent = `${devices.filter(d => d.online).length} online / ${devices.length} total`;
  $('devices').innerHTML = devices.map(d => `<article class="device-card"><div class="device-top"><span class="device-name">${esc(d.device)}</span><span class="badge ${!d.online?'offline':d.state==='FALL'?'alarm':''}">${!d.online?'Offline':d.state==='FALL'?'Alert':'Online'}</span></div><h3 class="device-state">${esc(d.state)}</h3><p class="muted">Last received ${esc(date(d.last_seen))}</p><div class="device-metrics"><span>Uptime <b>${Math.floor(d.uptime_ms/1000)}s</b></span><span>Dropped events <b>${d.dropped}</b></span></div></article>`).join('') || '<div class="empty">No device has checked in yet.</div>';
  const alarms = devices.filter(d => d.state === 'FALL');
  $('attention').innerHTML = alarms.length ? alarms.map(d => {
    const event = state.events.find(e => e.device === d.device && e.boot === d.boot && e.incident === d.incident && ['fall','sos'].includes(e.type));
    return `<article class="alarm-card"><p class="eyebrow">${d.online?'ACTIVE ALERT':'LAST KNOWN ALERT · DEVICE OFFLINE'}</p><h3>${d.reason==='manual_sos'?'Help requested':'Fall detected'}</h3><p>${esc(d.device)} · ${esc(human(d.reason))}</p>${event?ackHTML(event):'<p>Waiting for incident details.</p>'}<p class="footnote">Marking seen records caregiver acknowledgement. Clear the board alarm locally with B2.</p></article>`;
  }).join('') : `<div class="all-clear"><span class="status-mark">${devices.length && devices.every(d=>d.online)?'✓':'—'}</span><div><h3>${!devices.length?'Waiting for a device':devices.some(d=>!d.online)?'Some devices are offline':'No active alerts'}</h3><p>${devices.some(d=>!d.online)?'An offline device’s current condition is unknown.':'Based on the latest received device states.'}</p></div></div>`;
  bindAck($('attention'));
  $('recent').innerHTML = events.slice(0,5).map(e => `<div class="activity-row"><time>${esc(new Date(e.received*1000).toLocaleTimeString())}</time><div><strong>${esc(human(e.type))}</strong><p>${esc(e.device)} · ${esc(human(e.reason))}</p></div></div>`).join('') || '<div class="empty">Events will appear here as they arrive.</div>';
  renderEvents(); renderCaptures();
  if (!connected && state.devices.length) $('attention').innerHTML='<div class="alarm-card"><h3>Current status unavailable</h3><p>Displayed device states are stale. Reconnect to verify current status.</p></div>';
}
function filteredEvents() {
  const query = $('search').value.toLowerCase();
  return matching(state.events).filter(e => (!$('eventFilter').value || e.type === $('eventFilter').value) && `${e.device} ${e.type} ${e.reason}`.toLowerCase().includes(query));
}
function renderEvents() {
  const events = filteredEvents();
  $('eventEmpty').hidden = !!events.length;
  $('history').innerHTML = events.map(e => `<tr tabindex="0" data-event="${state.events.indexOf(e)}"><td>${esc(date(e.received))}</td><td><strong>${esc(human(e.type))}</strong> #${e.seq}</td><td>${esc(e.device)}</td><td>${esc(human(e.reason))}</td><td>${e.peak_mg} mg / ${e.peak_dps} °/s</td><td>${ackHTML(e)}</td></tr>`).join('');
  $('history').querySelectorAll('[data-event]').forEach(row => {
    const open = () => showEvent(state.events[Number(row.dataset.event)]);
    row.onclick = open; row.onkeydown = e => {if(e.key==='Enter')open();};
  }); bindAck($('history'));
}
const rejectionNames = [[1,'No impact within the allowed window'],[2,'Insufficient filtered rotation'],[4,'No settled reference posture'],[8,'Quiet period too short'],[16,'Changed posture not held long enough'],[32,'Sampling gap interrupted the candidate']];
function showEvent(e) {
  $('eventDetail').innerHTML = `<p class="eyebrow">EVENT #${e.seq}</p><h2>${esc(human(e.type))}</h2><p>${esc(e.device)} · ${esc(date(e.received))}</p><dl class="event-facts"><dt>Decision</dt><dd>${esc(human(e.reason))}</dd><dt>Board uptime</dt><dd>${e.uptime_ms} ms</dd><dt>Acceleration range</dt><dd>${e.min_mg}–${e.peak_mg} mg</dd><dt>Peak angular speed</dt><dd>${e.peak_dps} °/s</dd><dt>Boot / incident</dt><dd>${esc(e.boot)} / ${e.incident}</dd></dl><ul class="event-reasons">${rejectionNames.filter(([bit]) => (e.rejection_flags||0)&bit).map(([,name])=>`<li>${name}</li>`).join('')}</ul><p class="footnote">Rejection flags describe unmet checks; the low-g and posture paths have different confirmation requirements.</p>`;
  $('eventDialog').showModal();
}
function renderCaptures() {
  $('captureCount').textContent = state.captures.length;
  const captures = matching(state.captures).filter(c => !$('trialFilter').value || c.label === $('trialFilter').value);
  $('captures').innerHTML = captures.map(c => `<button class="capture-item ${key(c)===selected?'active':''}" data-capture="${esc(key(c))}"><strong>${esc(c.device)} <span>#${c.capture_id}</span></strong><small>${esc(date(c.received))}</small><div class="meta">${esc(human(c.outcome))} · ${esc(human(c.label))}</div>${c.complete?'':`<div class="upload-track"><span style="width:${100*c.received_rows/c.total}%"></span></div><small>Receiving ${c.received_rows}/${c.total} samples</small>`}</button>`).join('') || '<div class="empty">No recordings match this view.</div>';
  $('captures').querySelectorAll('[data-capture]').forEach(b => b.onclick=()=>selectCapture(b.dataset.capture));
  const current = state.captures.find(c => key(c) === selected);
  if(current && current.received_rows !== selectedRows) selectCapture(selected);
}
async function selectCapture(id) {
  const load = ++captureLoad;
  selected = id;
  const meta = state.captures.find(c=>key(c)===id); if(!meta)return;
  selectedRows = meta.received_rows;
  document.querySelectorAll('[data-capture]').forEach(b=>b.classList.toggle('active',b.dataset.capture===id));
  try {
    const params = new URLSearchParams({device:meta.device,boot:meta.boot,id:meta.capture_id});
    const c = await (await api('/api/capture?'+params)).json(); if(selected!==id || load!==captureLoad)return;
    if(!c.complete) { $('captureDetail').innerHTML=`<div class="empty tall"><h3>Receiving recording #${c.capture_id}</h3><p>${c.samples.length} of ${c.total} samples received. Alarms take priority over recording uploads.</p></div>`; return; }
    const rows=c.samples, magnitude=(r,start)=>Math.hypot(...r.slice(start,start+3));
    const ra=rows.map(r=>magnitude(r,1)), fa=rows.map(r=>magnitude(r,4)), rg=rows.map(r=>magnitude(r,7)/1000), fg=rows.map(r=>magnitude(r,10)/1000);
    $('captureDetail').innerHTML=`<div class="detail-head"><div><p class="eyebrow">RECORDING #${c.capture_id} · ${esc(human(c.source))}</p><h2>${esc(human(c.outcome))}</h2><p class="muted">${esc(c.device)} · ${esc(date(c.received))}</p></div><button id="downloadCapture" class="quiet">Download CSV</button></div><div class="measurements"><div><small>Lowest raw acceleration</small><strong>${Math.round(Math.min(...ra))} mg</strong></div><div><small>Peak raw / filtered</small><strong>${Math.round(Math.max(...ra))} / ${Math.round(Math.max(...fa))} mg</strong></div><div><small>Peak raw rotation</small><strong>${Math.round(Math.max(...rg))} °/s</strong></div></div>${rows.some(r=>r[14]&32)?'<p class="capture-warning">An acceleration axis approached ±2g. The sensor may have clipped the impact; the recorded peak can understate it.</p>':''}${rows.some(r=>r[14]&64)?'<p class="capture-warning">This recording contains a sampling gap. Inspect timestamps before comparing trials.</p>':''}<div id="accelChart"></div><div id="gyroChart"></div><p id="cursorReadout" class="cursor-readout">Move across a chart to inspect individual samples. Dashed lines show the current starting thresholds.</p><form id="trialForm" class="trial-form"><h3>Trial notes</h3><p class="muted">Label what you actually did, independently of the detector’s result.</p><div class="form-row"><label>Observed activity<select id="trialLabel">${['unlabelled','fall_trial','normal_activity','false_alarm','missed_fall'].map(v=>`<option value="${v}">${human(v)}</option>`).join('')}</select></label><button type="submit">Save notes</button></div><label>Setup and observations<textarea id="trialNote" maxlength="1000" rows="3" placeholder="Board orientation, surface, motion, expected result…"></textarea></label><p id="noteStatus" role="status"></p></form>`;
    chart('accelChart','Acceleration magnitude','mg',ra,fa,[650,1600],c);
    chart('gyroChart','Angular speed magnitude','°/s',rg,fg,[100],c);
    $('trialLabel').value=c.label; $('trialNote').value=c.note;
    $('trialForm').onsubmit=async e=>{e.preventDefault();try{await api('/api/notes',{device:c.device,boot:c.boot,capture_id:c.capture_id,label:$('trialLabel').value,note:$('trialNote').value});$('noteStatus').textContent='Saved.';await refresh();}catch(error){$('noteStatus').textContent=error.message;}};
    $('downloadCapture').onclick=async()=>{try{download(await(await api('/api/capture?'+params+'&format=csv')).blob(),`${c.device}-capture-${c.capture_id}.csv`);}catch(error){$('status').textContent=error.message;}};
  } catch(error) {if(load===captureLoad){selectedRows=-1; $('captureDetail').textContent=error.message;}}
}
function chart(target,title,unit,raw,filtered,thresholds,c) {
  const w=760,h=210,left=52,right=15,top=20,bottom=34;
  const elapsed = c.samples.map(r=>(r[0]-c.samples[0][0])>>>0), duration=Math.max(1,elapsed.at(-1));
  const ymax=Math.max(...raw,...thresholds,1)*1.12;
  const x=t=>left+t/duration*(w-left-right),y=v=>h-bottom-v/ymax*(h-top-bottom);
  const line=values=>values.map((v,i)=>`${x(elapsed[i]).toFixed(1)},${y(v).toFixed(1)}`).join(' ');
  const ticks=[0,.25,.5,.75,1].map(f=>`<line x1="${left}" y1="${y(ymax*f)}" x2="${w-right}" y2="${y(ymax*f)}" stroke="#e8e8e0"/><text x="${left-8}" y="${y(ymax*f)+4}" text-anchor="end">${Math.round(ymax*f)}</text>`).join('');
  $(target).innerHTML=`<div class="chart-head"><h3>${title} <small>${unit}</small></h3><span class="legend"><b style="background:#a3aaa6"></b>Raw <b style="background:#315d46"></b>Filtered</span></div><svg class="chart" viewBox="0 0 ${w} ${h}" role="img" aria-label="${title} raw and filtered readings"><g font-size="11" fill="#637067">${ticks}${thresholds.map(v=>`<line x1="${left}" y1="${y(v)}" x2="${w-right}" y2="${y(v)}" stroke="#b27b44" stroke-dasharray="4 5"/><text x="${w-right}" y="${y(v)-4}" text-anchor="end">${v}</text>`).join('')}<polyline points="${line(raw)}" fill="none" stroke="#a3aaa6" stroke-width="1.4"/><polyline points="${line(filtered)}" fill="none" stroke="#315d46" stroke-width="2"/><line x1="${x((c.trigger_ms-c.samples[0][0])>>>0)}" x2="${x((c.trigger_ms-c.samples[0][0])>>>0)}" y1="${top}" y2="${h-bottom}" stroke="#a13529" stroke-dasharray="3 4"/><text x="${left}" y="${h-10}">0 s</text><text x="${w-right}" y="${h-10}" text-anchor="end">${(duration/1000).toFixed(2)} s · red line: trigger</text><line class="cursor" y1="${top}" y2="${h-bottom}" stroke="#202b26" visibility="hidden"/></g></svg>`;
  const svg=$(target).querySelector('svg');
  svg.onpointermove=e=>{const rect=svg.getBoundingClientRect(), t=Math.max(0,Math.min(duration,((e.clientX-rect.left)*w/rect.width-left)/(w-left-right)*duration));let i=0;while(i<elapsed.length-1 && Math.abs(elapsed[i+1]-t)<Math.abs(elapsed[i]-t))i++;const cursor=svg.querySelector('.cursor');cursor.setAttribute('x1',x(elapsed[i]));cursor.setAttribute('x2',x(elapsed[i]));cursor.setAttribute('visibility','visible');$('cursorReadout').textContent=`${(elapsed[i]/1000).toFixed(2)} s · ${title}: raw ${Math.round(raw[i])}, filtered ${Math.round(filtered[i])} ${unit} · ${['STARTUP','NORMAL','WAIT_IMPACT','CONFIRM','FALL'][c.samples[i][13]]} · flags ${c.samples[i][14]}`;};
}
function download(blob,name) {const url=URL.createObjectURL(blob),a=document.createElement('a');a.href=url;a.download=name;a.click();setTimeout(()=>URL.revokeObjectURL(url),1000);}
$('exportEvents').onclick=()=>{const columns=['received','device','type','reason','uptime_ms','peak_mg','peak_dps','caregiver_ack'];const quote=v=>'"'+String(v??'').replaceAll('"','""')+'"';download(new Blob([columns.join(',')+'\r\n'+filteredEvents().map(e=>columns.map(k=>quote(k==='received'||k==='caregiver_ack'?date(e[k]):e[k])).join(',')).join('\r\n')],{type:'text/csv'}),'eldercare-events.csv');};
function chime(){if(!audio)return;const start=audio.currentTime;[660,880,660].forEach((frequency,i)=>{const o=audio.createOscillator(),g=audio.createGain();o.frequency.value=frequency;o.connect(g);g.connect(audio.destination);g.gain.setValueAtTime(.06,start+i*.2);g.gain.exponentialRampToValueAtTime(.001,start+i*.2+.15);o.start(start+i*.2);o.stop(start+i*.2+.16);});}
$('soundButton').onclick=async()=>{sound=!sound;if(sound){audio ||= new AudioContext();await audio.resume();chime();}$('soundButton').textContent=`Browser sound: ${sound?'on':'off'}`;};
async function refresh() {
  if(busy || !token)return;busy=true;
  try {
    const next=await(await api('/api/state')).json();
    const ids=new Set(next.events.filter(e=>['fall','sos'].includes(e.type)).map(e=>`${e.device}/${e.boot}/${e.seq}`));
    if(sound && seen && [...ids].some(id=>!seen.has(id)))chime();seen=ids;state=next;connected=true;
    const old=$('deviceFilter').value;$('deviceFilter').innerHTML='<option value="">All devices</option>'+state.devices.map(d=>`<option value="${esc(d.device)}">${esc(d.device)}</option>`).join('');$('deviceFilter').value=old;
    $('connectionDot').className='live';$('connectionLabel').textContent='Receiver connected';$('status').textContent='Caregiver acknowledgement records that an alert was seen. It does not silence the board or confirm recovery.';
    $('updated').textContent='Updated '+new Date().toLocaleTimeString();render();
  } catch(error) {connected=false;$('connectionDot').className='failed';$('connectionLabel').textContent='Receiver unavailable';$('status').textContent='Connection lost — displayed data is stale. '+error.message;$('attention').innerHTML='<div class="alarm-card"><h3>Current status unavailable</h3><p>Reconnect to the receiver to verify device status.</p></div>';throw error;}
  finally {busy=false;}
}
$('connectionButton').onclick=()=>$('connectDialog').showModal();
$('closeConnect').onclick=()=>$('connectDialog').close();$('closeEvent').onclick=()=>$('eventDialog').close();
$('login').onsubmit=async e=>{e.preventDefault();token=$('token').value.trim();$('loginError').textContent='';try{await refresh();if(connected){sessionStorage.setItem('eldercare-token',token);$('token').value='';$('connectDialog').close();}}catch(error){$('loginError').textContent=error.message;}};
$('disconnectButton').onclick=()=>{token='';sessionStorage.removeItem('eldercare-token');location.reload();};
$('deviceFilter').onchange=render;$('trialFilter').onchange=renderCaptures;
$('search').oninput=renderEvents;$('eventFilter').onchange=renderEvents;
setInterval(()=>refresh().catch(()=>{}),2000);render();
if(token)refresh().catch(()=>{});else $('connectDialog').showModal();
