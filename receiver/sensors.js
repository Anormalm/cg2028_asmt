'use strict';
let sensorSelected = '', sensorFormDevice = '', sensorRevision = 0, sensorDirty = false;
function sensorItem() { return (state.sensors || []).find(s => s.device === sensorSelected); }
function fillSensorSettings(s) {
  const c = s.settings;
  $('proximityEnabled').checked = !!c.proximity;
  $('proximityBeeps').checked = !!c.beeps;
  $('soundEnabled').checked = !!c.sound;
  $('nearDistance').value = c.near_mm;
  $('farDistance').value = c.far_mm;
  $('soundThreshold').value = c.sound_threshold / 10;
  sensorRevision = c.revision; sensorFormDevice = s.device; sensorDirty = false;
}
function renderSensors() {
  const list = matching(state.sensors || []);
  if (!list.some(s => s.device === sensorSelected)) sensorSelected = list[0]?.device || '';
  $('sensorDevice').innerHTML = list.map(s => `<option value="${esc(s.device)}">${esc(s.device)}</option>`).join('');
  $('sensorDevice').value = sensorSelected;
  const s = sensorItem();
  $('sensorEmpty').hidden = !!s; $('sensorContent').hidden = !s;
  if (!s) { $('sensorUpdated').textContent = 'Waiting for sensor data'; return; }
  const fresh = connected && s.online;
  $('sensorUpdated').textContent = (fresh ? 'Received ' : 'Stale data - last received ') + date(s.received);
  $('distanceValue').textContent = fresh && s.distance_mm >= 0 ? `${s.distance_mm} mm` : '--';
  $('proximityStatus').textContent = !fresh ? 'Unavailable' : s.range_status === -1 ? 'Disabled' : s.range_status === -2 ? 'Sensor error' : s.distance_mm < 0 ? 'No valid range' : s.proximity_active ? 'Object nearby' : 'Clear';
  $('proximityStatus').classList.toggle('sensor-alert', fresh && !!s.proximity_active);
  $('soundValue').textContent = fresh && s.sound_valid ? `${(s.sound_dbfs/10).toFixed(1)} dBFS` : '--';
  $('soundStatus').textContent = !fresh ? 'Unavailable' : s.mic_error ? `Microphone error ${s.mic_error}` : !s.sound_valid ? (s.config_revision === s.settings.revision && !s.settings.sound ? 'Disabled' : 'Waiting for samples') : s.sound_masked ? 'Device sound excluded' : s.sound_active ? 'Activity' : 'Quiet';
  $('soundCount').textContent = `${s.sound_events} activity bursts since boot`;
  const applied = s.config_revision === s.settings.revision;
  $('settingsApplied').textContent = !fresh ? 'Board unavailable - settings may be pending' : applied ? 'Applied on board' : 'Waiting for board to apply settings';
  if (sensorFormDevice !== s.device || (!sensorDirty && sensorRevision !== s.settings.revision)) fillSensorSettings(s);
  sensorPlot('distancePlot', s.history, 'distance_mm', 0, 2000, s.settings.far_mm, 'Distance (mm)');
  sensorPlot('soundPlot', s.history, 'sound_dbfs', -960, 0, s.settings.sound_threshold, 'Sound level (dBFS)');
}
function sensorPlot(id, history, field, low, high, threshold, title) {
  if (!history?.length) { $(id).textContent='Waiting for readings'; return; }
  const width=520,height=145,left=42,right=12,top=12,bottom=25;
  const start=history[0].received,span=Math.max(1,history.at(-1).received-start);
  const x=t=>left+(t-start)/span*(width-left-right),y=v=>top+(high-v)/(high-low)*(height-top-bottom);
  let paths=[],line=[],previous=null;
  for(const p of history) {
    const valid=field==='distance_mm' ? p[field]>=0 : p.sound_valid && !p.sound_masked;
    if(!valid || (previous!==null && p.received-previous>5)) {if(line.length)paths.push(line);line=[];}
    if(valid)line.push(`${x(p.received).toFixed(1)},${y(p[field]).toFixed(1)}`);
    previous=p.received;
  }
  if(line.length)paths.push(line);
  const label=v=>field==='sound_dbfs'?v/10:v;
  $(id).innerHTML=`<svg class="chart sensor-chart" viewBox="0 0 ${width} ${height}" role="img" aria-label="${title}, recent received readings"><g font-size="10" fill="#74777b">${[low,(low+high)/2,high].map(v=>`<line x1="${left}" x2="${width-right}" y1="${y(v)}" y2="${y(v)}" stroke="#e6e7e9"/><text x="${left-7}" y="${y(v)+3}" text-anchor="end">${label(v)}</text>`).join('')}<line x1="${left}" x2="${width-right}" y1="${y(threshold)}" y2="${y(threshold)}" stroke="#a57530" stroke-dasharray="4 4"/>${paths.map(points=>`<polyline points="${points.join(' ')}" fill="none" stroke="#333" stroke-width="1.6"/>`).join('')}<text x="${left}" y="${height-5}">${Math.round(span)} s ago</text><text x="${width-right}" y="${height-5}" text-anchor="end">Latest received</text></g></svg>`;
}
document.getElementById('sensorDevice').onchange=e=>{
  sensorSelected=e.target.value;sensorFormDevice='';sensorDirty=false;
  $('sensorSaveStatus').textContent='';renderSensors();
};
document.getElementById('sensorSettings').oninput=()=>{sensorDirty=true;};
document.getElementById('reloadSensors').onclick=()=>{
  const s=sensorItem();if(s)fillSensorSettings(s);
  $('sensorSaveStatus').textContent='Loaded latest saved settings.';
};
document.getElementById('sensorSettings').onsubmit=async e=>{
  e.preventDefault();const s=sensorItem();if(!s)return;
  const body={device:s.device,revision:sensorRevision,proximity:Number($('proximityEnabled').checked),
    sound:Number($('soundEnabled').checked),beeps:Number($('proximityBeeps').checked),
    near_mm:Number($('nearDistance').value),far_mm:Number($('farDistance').value),sound_threshold:Math.round(Number($('soundThreshold').value)*10)};
  if(body.far_mm<body.near_mm+100){$('sensorSaveStatus').textContent='Warning distance must exceed near distance by at least 100 mm.';return;}
  $('saveSensors').disabled=true;
  try {
    const c=await(await api('/api/sensor-settings',body)).json();
    s.settings=c;sensorDirty=false;sensorFormDevice='';
    $('sensorSaveStatus').textContent='Saved. Waiting for the board to apply it.';
    renderSensors();await refresh();
  } catch(error) {$('sensorSaveStatus').textContent=error.message;}
  finally {$('saveSensors').disabled=false;}
};
