'use strict';
const $ = id => document.getElementById(id);
const esc = value => String(value ?? '').replace(/[&<>"']/g, c => ({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
const human = value => String(value || '—').replaceAll('_', ' ');
const date = value => value ? new Date(value * 1000).toLocaleString() : '—';
const eventNames = {fall:'Fall detected', sos:'Help requested', local_ack:'Cleared on device'};
const eventName = e => e.type === 'recording' ? `Motion recording #${e.capture_id}` : eventNames[e.type] || human(e.type);
const recordingKey = c => `${c.device}/${c.boot}/${c.capture_id}`;
let token = sessionStorage.getItem('eldercare-token') || '';
let state = {devices:[], events:[], sensors:[], captures:[]}, busy = false, connected = false;
let selectedRecording = null, recordingVersion = '', recordingLoad = 0;

// Keep unchanged controls in place while polling, so keyboard focus is preserved.
const rendered = new Map();
function html(id, content) {
  if (rendered.get(id) === content) return false;
  $(id).innerHTML = content;
  rendered.set(id, content);
  return true;
}
async function api(path, body) {
  const response = await fetch(path, {headers:{Authorization:`Bearer ${token}`, 'Content-Type':'application/json'},
    method:body === undefined ? 'GET':'POST', body:body === undefined ? undefined:JSON.stringify(body)});
  if (!response.ok) {
    const error = await response.json().catch(() => ({}));
    throw Error(error.error || `Receiver returned ${response.status}`);
  }
  return response;
}
function matching(items) { return items.filter(x => !$('deviceFilter').value || x.device === $('deviceFilter').value); }
// Overview contains care alerts; Event log also includes the recorded motion.
function careEvents() { return matching(state.events).filter(e => Object.hasOwn(eventNames, e.type)); }
function page(name) {
  document.querySelectorAll('.page').forEach(x => x.hidden = x.id !== name);
  document.querySelectorAll('nav [data-page]').forEach(x => {
    x.classList.toggle('selected', x.dataset.page === name);
    if (x.dataset.page === name) x.setAttribute('aria-current', 'page');
    else x.removeAttribute('aria-current');
  });
  $('pageTitle').textContent = {monitor:'Overview', sensors:'Sound activity', events:'Event log'}[name];
}
document.querySelectorAll('[data-page]').forEach(x => x.onclick = e => { e.preventDefault(); page(x.dataset.page); });
function ackHTML(e) {
  if (e.type === 'local_ack') return '<span class="muted">Acknowledged on device</span>';
  if (e.caregiver_ack) return `<span class="muted">Seen ${esc(date(e.caregiver_ack))}</span>`;
  return `<button class="quiet" data-ack="${state.events.indexOf(e)}">Mark seen</button>`;
}
function bindAck(root) {
  root.querySelectorAll('[data-ack]').forEach(button => button.onclick = async ev => {
    ev.stopPropagation(); button.disabled = true;
    const e = state.events[Number(button.dataset.ack)];
    try { await api('/api/ack', {device:e.device, boot:e.boot, incident:e.incident}); await refresh(); }
    catch (error) { $('status').textContent = error.message; }
    finally { button.disabled = false; }
  });
}
function deviceState(d) {
  if (!connected || !d.online) return 'Status unavailable';
  if (d.state === 'FALL') return d.reason === 'manual_sos' ? 'Help requested' : 'Fall detected';
  return {STARTUP:'Settling', NORMAL:'Monitoring', WAIT_IMPACT:'Checking movement', CONFIRM:'Checking movement'}[d.state] || human(d.state);
}
function render() {
  const devices = matching(state.devices);
  html('devices', devices.map(d => {
    const online = connected && d.online;
    return `<article class="device-card"><div class="device-top"><span class="device-name">${esc(d.device)}</span><span class="badge ${!online?'offline':d.state==='FALL'?'alarm':''}">${!online?'Offline':d.state==='FALL'?'Alert':'Online'}</span></div><h3 class="device-state">${esc(deviceState(d))}</h3><p class="muted">Last update ${esc(date(d.last_seen))}</p></article>`;
  }).join('') || '<div class="empty">No device has connected yet.</div>');
  const alarms = devices.filter(d => d.state === 'FALL');
  let attention;
  if (!connected) {
    attention = state.devices.length
      ? '<div class="alarm-card"><h3>Current status unavailable</h3><p>Reconnect to see the latest device status.</p></div>'
      : '<div class="all-clear"><h3>Waiting for connection</h3><p>Connect to your receiver in Settings.</p></div>';
  } else if (alarms.length) {
    attention = alarms.map(d => {
      const event = state.events.find(e => e.device === d.device && e.boot === d.boot && e.incident === d.incident && ['fall','sos'].includes(e.type));
      return `<article class="alarm-card"><p class="eyebrow">${d.online?'ACTIVE ALERT':'LAST KNOWN ALERT · DEVICE OFFLINE'}</p><h3>${d.reason==='manual_sos'?'Help requested':'Fall detected'}</h3><p>${esc(d.device)}</p>${event?ackHTML(event):'<p>Waiting for incident details.</p>'}<p class="footnote">Mark seen records your response. Clear the device alarm with B2.</p></article>`;
    }).join('');
  } else {
    const offline = devices.some(d => !d.online);
    attention = `<div class="all-clear"><h3>${!devices.length?'Waiting for a device':offline?'Some devices are offline':'No active alerts'}</h3><p>${offline?'Current status is unavailable for offline devices.':'Latest received status.'}</p></div>`;
  }
  if (html('attention', attention)) bindAck($('attention'));
  html('recent', careEvents().slice(0,5).map(e => `<div class="activity-row"><time>${esc(new Date(e.received*1000).toLocaleTimeString())}</time><div><strong>${esc(eventName(e))}</strong><p>${esc(e.device)}</p></div></div>`).join('') || '<div class="empty">Events will appear here as they arrive.</div>');
  renderEvents(); renderSensors();
}
function filteredEvents() {
  const query = $('search').value.toLowerCase().trim();
  // Captures have no incident ID. Show their own entries rather than guessing
  // an association from upload time, which can be delayed by Wi-Fi outages.
  const entries = [...careEvents(), ...matching(state.captures || []).map(c => ({...c, type:'recording'}))];
  return entries.filter(e => (!$('eventFilter').value || e.type === $('eventFilter').value) && `${e.device} ${e.type} ${eventName(e)}`.toLowerCase().includes(query))
    .sort((a,b) => b.received - a.received);
}
function renderEvents() {
  const events = filteredEvents();
  $('eventEmpty').hidden = !!events.length;
  updateRecording();
  if (!html('history', events.map(e => {
    const recording = e.type === 'recording';
    const target = recording ? `data-recording="${esc(recordingKey(e))}"` : `data-event="${state.events.indexOf(e)}"`;
    const status = recording ? `<span class="muted">${e.complete?'View waveforms':`Receiving ${e.received_rows}/${e.total} samples`}</span>` : ackHTML(e);
    return `<tr tabindex="0" ${target} aria-label="${esc(eventName(e))}, ${esc(e.device)}, ${esc(date(e.received))}"><td>${esc(date(e.received))}</td><td><strong>${esc(eventName(e))}</strong></td><td>${esc(e.device)}</td><td>${status}</td></tr>`;
  }).join(''))) return;
  $('history').querySelectorAll('tr').forEach(row => {
    const open = () => row.dataset.recording ? showRecording(row.dataset.recording) : showEvent(state.events[Number(row.dataset.event)]);
    row.onclick = open;
    row.onkeydown = e => {
      if (e.target === row && ['Enter',' '].includes(e.key)) { e.preventDefault(); open(); }
    };
  });
  bindAck($('history'));
}
function showEvent(e) {
  resetRecording();
  $('eventDialog').classList.remove('recording-dialog');
  $('eventDetail').innerHTML = `<h2 id="eventTitle">${esc(eventName(e))}</h2><p>${esc(e.device)}</p><dl class="event-facts"><dt>Received</dt><dd>${esc(date(e.received))}</dd><dt>Response</dt><dd>${e.type==='local_ack'?'Acknowledged on device':e.caregiver_ack?'Seen '+esc(date(e.caregiver_ack)):'Not yet marked seen'}</dd></dl><details><summary>Technical details</summary><dl class="event-facts"><dt>Reason</dt><dd>${esc(human(e.reason))}</dd><dt>Peak motion</dt><dd>${e.peak_mg} mg / ${e.peak_dps} °/s</dd><dt>Device uptime</dt><dd>${e.uptime_ms} ms</dd><dt>Event / incident</dt><dd>${e.seq} / ${e.incident}</dd></dl><p class="footnote">Received time may be later than the event when Wi-Fi is unavailable.</p></details>`;
  $('eventDialog').showModal();
}
function resetRecording() {
  selectedRecording = null; recordingVersion = ''; recordingLoad++;
}
function showRecording(id) {
  resetRecording();
  const meta = (state.captures || []).find(c => recordingKey(c) === id);
  if (!meta) return;
  selectedRecording = id;
  $('eventDialog').classList.add('recording-dialog');
  $('eventDetail').innerHTML = `<h2 id="eventTitle">Motion recording #${meta.capture_id}</h2><p>${esc(meta.device)} · Received ${esc(date(meta.received))}</p><div id="recordingBody"></div>`;
  $('eventDialog').showModal();
  updateRecording(true);
}
function recordingProgress(received, total) {
  $('recordingBody').innerHTML = `<div class="recording-progress"><p role="status">Receiving ${received} of ${total} samples</p><progress value="${received}" max="${total}" aria-label="Recording upload"></progress><p class="muted">Waveforms will appear when the upload finishes.</p></div>`;
}
async function updateRecording(force = false) {
  if (!selectedRecording || !$('eventDialog').open) return;
  const meta = (state.captures || []).find(c => recordingKey(c) === selectedRecording);
  if (!meta) return;
  const version = `${meta.received_rows}/${meta.total}/${meta.complete}`;
  if (!force && version === recordingVersion) return;
  recordingVersion = version;
  const load = ++recordingLoad;
  if (!meta.complete) { recordingProgress(meta.received_rows, meta.total); return; }
  $('recordingBody').innerHTML = '<p class="empty" role="status">Loading waveforms…</p>';
  try {
    const params = new URLSearchParams({device:meta.device, boot:meta.boot, id:meta.capture_id});
    const c = await (await api('/api/capture?'+params)).json();
    if (load !== recordingLoad) return;
    if (!c.complete) { recordingVersion=''; recordingProgress(c.samples.length,c.total); return; }
    renderRecording(c, params);
  } catch (error) {
    if (load !== recordingLoad) return;
    $('recordingBody').innerHTML = `<p class="error" role="alert">Could not load this recording. ${esc(error.message)}</p><button id="retryRecording">Retry</button>`;
    $('retryRecording').onclick = () => updateRecording(true);
  }
}
function renderRecording(c, params) {
  const rows = c.samples, magnitude = (r,start) => Math.hypot(...r.slice(start,start+3));
  const rawA = rows.map(r => magnitude(r,1)), filteredA = rows.map(r => magnitude(r,4));
  const rawG = rows.map(r => magnitude(r,7)/1000), filteredG = rows.map(r => magnitude(r,10)/1000);
  const elapsed = rows.map(r => (r[0]-rows[0][0])>>>0), trigger = (c.trigger_ms-rows[0][0])>>>0;
  $('recordingBody').innerHTML = `
    <div class="recording-toolbar"><span class="muted">${rows.length} samples · ${(elapsed.at(-1)/1000).toFixed(2)} seconds</span><button id="downloadCapture" class="quiet">Download recording CSV</button></div>
    <p id="recordingDownloadStatus" class="error" role="status"></p>
    <div class="measurements"><div><small>Lowest acceleration</small><strong>${Math.round(Math.min(...rawA))} mg</strong></div><div><small>Peak raw / filtered acceleration</small><strong>${Math.round(Math.max(...rawA))} / ${Math.round(Math.max(...filteredA))} mg</strong></div><div><small>Peak angular speed</small><strong>${Math.round(Math.max(...rawG))} °/s</strong></div></div>
    ${rows.some(r=>r[14]&32)?'<p class="capture-warning">The accelerometer may have clipped the impact. Its recorded peak can be lower than the actual peak.</p>':''}
    ${rows.some(r=>r[14]&64)?'<p class="capture-warning">This recording contains a sampling gap.</p>':''}
    <div id="accelChart"></div><div id="gyroChart"></div>
    <div class="sample-controls"><label for="samplePosition">Inspect sample</label><input id="samplePosition" type="range" min="0" max="${rows.length-1}" step="1" value="0"><p id="cursorReadout"></p></div>
    <details class="recording-facts"><summary>Recording details</summary><dl class="event-facts"><dt>Trigger</dt><dd>${esc({candidate:'Fall check',raw_motion:'Motion change',manual:'Manual recording'}[c.source] || human(c.source))}</dd><dt>Detector result</dt><dd>${esc(c.outcome==='no_candidate'?'No decision recorded':human(c.outcome))}</dd><dt>Before / after trigger</dt><dd>${c.pre_count} / ${c.total-c.pre_count} samples</dd></dl></details>`;
  const cursors = [];
  const inspect = index => {
    $('samplePosition').value = index;
    cursors.forEach(move => move(index));
    $('cursorReadout').textContent = `${((elapsed[index]-trigger)/1000).toFixed(2)} s from trigger · Acceleration: ${Math.round(rawA[index])} / ${Math.round(filteredA[index])} mg · Angular speed: ${Math.round(rawG[index])} / ${Math.round(filteredG[index])} °/s (raw / filtered)`;
  };
  cursors.push(recordingChart('accelChart','Acceleration','mg',rawA,filteredA,elapsed,trigger,inspect));
  cursors.push(recordingChart('gyroChart','Angular speed','°/s',rawG,filteredG,elapsed,trigger,inspect));
  $('samplePosition').oninput = e => inspect(Number(e.target.value));
  inspect(Math.min(c.pre_count,rows.length-1));
  $('downloadCapture').onclick = async () => {
    const button = $('downloadCapture'), status = $('recordingDownloadStatus');
    button.disabled = true; status.textContent = '';
    try { download(await (await api('/api/capture?'+params+'&format=csv')).blob(),`${c.device}-capture-${c.capture_id}.csv`); }
    catch (error) { status.textContent = error.message; }
    finally { button.disabled = false; }
  };
}
function recordingChart(target, title, unit, raw, filtered, elapsed, trigger, inspect) {
  const w=760,h=210,left=72,right=15,top=20,bottom=34;
  const duration=Math.max(1,elapsed.at(-1)), ymax=Math.max(...raw,...filtered,1)*1.12;
  const x=t=>left+t/duration*(w-left-right), y=v=>h-bottom-v/ymax*(h-top-bottom);
  const line=values=>values.map((v,i)=>`${x(elapsed[i]).toFixed(1)},${y(v).toFixed(1)}`).join(' ');
  const ticks=[0,.25,.5,.75,1].map(f=>`<line x1="${left}" y1="${y(ymax*f)}" x2="${w-right}" y2="${y(ymax*f)}" stroke="#e6e7e9"/><text x="${left-8}" y="${y(ymax*f)+4}" text-anchor="end">${ymax<10?(ymax*f).toFixed(1):Math.round(ymax*f)}</text>`).join('');
  const triggerLine=trigger<=duration?`<line x1="${x(trigger)}" x2="${x(trigger)}" y1="${top}" y2="${h-bottom}" stroke="#b42318" stroke-dasharray="3 4"/>`:'';
  $(target).innerHTML=`<div class="chart-head"><h3>${title} <small>${unit}</small></h3><span class="legend"><b class="raw"></b>Raw <b class="filtered"></b>Filtered</span></div><svg class="chart" viewBox="0 0 ${w} ${h}" role="img" aria-label="${title}, raw and filtered waveforms; red line marks the trigger"><g font-size="11" fill="#686c72">${ticks}<polyline points="${line(raw)}" fill="none" stroke="#a3aaa6" stroke-width="1.6"/><polyline points="${line(filtered)}" fill="none" stroke="#315d46" stroke-width="2"/>${triggerLine}<text x="${left}" y="${h-10}">${(-trigger/1000).toFixed(2)} s</text><text x="${w-right}" y="${h-10}" text-anchor="end">${((elapsed.at(-1)-trigger)/1000).toFixed(2)} s · red line: trigger</text><line class="cursor" y1="${top}" y2="${h-bottom}" stroke="#202124" stroke-dasharray="2 3"/></g></svg>`;
  const svg=$(target).querySelector('svg');
  const point=e=>{
    const rect=svg.getBoundingClientRect(), t=Math.max(0,Math.min(duration,((e.clientX-rect.left)*w/rect.width-left)/(w-left-right)*duration));
    let nearest=0;
    elapsed.forEach((value,i)=>{if(Math.abs(value-t)<Math.abs(elapsed[nearest]-t))nearest=i;});
    inspect(nearest);
  };
  svg.onpointermove=point; svg.onclick=point;
  return index=>{const cursor=svg.querySelector('.cursor');cursor.setAttribute('x1',x(elapsed[index]));cursor.setAttribute('x2',x(elapsed[index]));};
}
function download(blob, name) {
  const url = URL.createObjectURL(blob), a = document.createElement('a');
  a.href = url; a.download = name; a.click(); setTimeout(() => URL.revokeObjectURL(url), 1000);
}
$('exportEvents').onclick = () => {
  const columns = ['received','device','type','capture_id','caregiver_ack'];
  const quote = v => '"' + String(v ?? '').replaceAll('"','""') + '"';
  const rows = filteredEvents().map(e => columns.map(k => quote(k==='received'||k==='caregiver_ack'?date(e[k]):e[k])).join(','));
  download(new Blob([columns.join(',')+'\r\n'+rows.join('\r\n')], {type:'text/csv'}), 'eldercare-events.csv');
};
async function refresh() {
  if (busy || !token) return;
  busy = true;
  try {
    state = await (await api('/api/state')).json(); connected = true;
    const old = $('deviceFilter').value;
    const deviceNames = [...new Set([...state.devices, ...(state.captures || []), ...(state.sensors || [])].map(d => d.device))].sort();
    if (html('deviceFilter', '<option value="">All devices</option>'+deviceNames.map(name => `<option value="${esc(name)}">${esc(name)}</option>`).join(''))) {
      $('deviceFilter').value = deviceNames.includes(old) ? old : '';
    }
    $('deviceFilter').hidden = deviceNames.length < 2;
    $('connectionState').textContent = 'Connected'; $('status').textContent = '';
    $('updated').textContent = 'Updated '+new Date().toLocaleTimeString(); render();
  } catch (error) {
    connected = false; $('connectionState').textContent = 'Connection unavailable';
    $('status').textContent = 'Connection lost. Displayed information may be out of date.';
    render(); throw error;
  } finally { busy = false; }
}
$('connectionButton').onclick = () => $('connectDialog').showModal();
$('closeConnect').onclick = () => $('connectDialog').close();
$('closeEvent').onclick = () => $('eventDialog').close();
$('eventDialog').addEventListener('close', resetRecording);
$('login').onsubmit = async e => {
  e.preventDefault(); token = $('token').value.trim(); $('loginError').textContent = '';
  try { await refresh(); if (connected) { sessionStorage.setItem('eldercare-token',token); $('token').value=''; $('connectDialog').close(); } }
  catch (error) { $('loginError').textContent = error.message; }
};
$('disconnectButton').onclick = () => { token=''; sessionStorage.removeItem('eldercare-token'); location.reload(); };
$('deviceFilter').onchange = () => { $('sensorSaveStatus').textContent=''; render(); };
$('search').oninput = renderEvents;
$('eventFilter').onchange = renderEvents;
setInterval(() => refresh().catch(() => {}), 250);
render();
if (token) refresh().catch(() => {}); else $('connectDialog').showModal();
