// Monster Eyes web control. Talks to the device over the /api endpoints and
// re-renders from a GET /api/status poll.

var s = {};
var sawProvisioning = false;

function q(id) { return document.getElementById(id); }

// Package names come from config.eye and reach us unescaped: the device only
// escapes `"` and `\` when building its JSON, so anything interpolated into
// innerHTML has to go through here.
function h(text) {
  return String(text).replace(/[&<>"]/g, function (c) {
    return { '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[c];
  });
}

// The poll rewrites every field. Leave alone whatever the user is editing.
function set(el, value) {
  if (document.activeElement !== el) el.value = value;
}

var TABS = ['c', 'p', 's'];

function show(tab) {
  if (TABS.indexOf(tab) < 0) tab = 'c';
  TABS.forEach(function (t) {
    q('panel-' + t).hidden = t !== tab;
    q('tab-' + t).setAttribute('aria-selected', t === tab);
  });
  if (location.hash.slice(1) !== tab) location.hash = tab;
  // Live capture costs the device a whole extra frame per request, so it only
  // runs while its own tab is open.
  if (tab !== 's') stopLive();
  else if (!q('shot').src) capture();
}

async function post(url, data) {
  await fetch(url, {
    method: 'POST',
    headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
    body: new URLSearchParams(data || {})
  });
  refresh();
}

function renderStatus() {
  var battery = s.batteryPercent === null ? 'Unavailable' : s.batteryPercent + '%';
  // The device can only see whether a USB host is attached, not whether a
  // charger is; see projects/power-diagnostics for why there is nothing better.
  var power = s.externalPower === 'usb' ? 'USB connected'
    : s.externalPower === 'battery' ? 'Battery' : 'Unknown';
  var audio = s.audioPresent
    ? (s.hasSound ? (s.muted ? 'Muted' : 'On, ' + s.volume + '%') : 'Package has no sound')
    : 'No codec';
  q('status').innerHTML =
    '<b>Style</b><span>' + h(s.styleName) + '</span>' +
    '<b>Mode</b><span>' + h(s.mode) + '</span>' +
    '<b>Battery</b><span>' + battery + '</span>' +
    '<b>Power</b><span>' + power + '</span>' +
    '<b>Wi-Fi</b><span>' + h(s.wifi) + ' ' + h(s.ip || '') + '</span>' +
    '<b>Audio</b><span>' + audio + '</span>';
}

function renderAudio() {
  q('mute').textContent = s.muted ? 'Unmute' : 'Mute';
  q('mute').disabled = !s.audioPresent;
  // The device refuses a play while muted or with a silent package, so say so
  // in the button rather than letting the press fail quietly.
  q('play').disabled = !s.audioPresent || !s.hasSound || s.muted;
  q('volume').disabled = !s.audioPresent;
  set(q('volume'), s.volume);
  if (document.activeElement !== q('volume')) q('volpct').textContent = s.volume + '%';
}

function renderBrightness() {
  set(q('brightness'), s.brightness);
  if (document.activeElement !== q('brightness'))
    q('brightpct').textContent = s.brightness + '%';
}

function renderStyles() {
  q('style').innerHTML = s.styles.map(function (name, i) {
    return s.enabled[i] ? '<option value=' + i + '>' + h(name) + '</option>' : '';
  }).join('');
  set(q('style'), s.style);
}

function renderPackages() {
  var last = s.styles.length - 1;
  q('eyes').innerHTML = s.styles.map(function (name, i) {
    var active = i === s.style;
    return '<div class=pkg>' +
      '<input type=checkbox ' + (s.enabled[i] ? 'checked' : '') +
        ' aria-label="Enable ' + h(name) + '"' +
        ' onchange="post(\'/api/eyes/enabled\',{style:' + i + ',enabled:this.checked})">' +
      '<div class=name>' + h(name) + (active ? ' <span class=tag>active</span>' : '') + '</div>' +
      '<div class=acts>' +
        '<button class=ghost ' + (i === 0 ? 'disabled' : '') + ' title="Move up" onclick="move(' + i + ',-1)">&uarr;</button>' +
        '<button class=ghost ' + (i === last ? 'disabled' : '') + ' title="Move down" onclick="move(' + i + ',1)">&darr;</button>' +
        '<a href="/api/packages/download?id=' + encodeURIComponent(s.ids[i]) + '&path=config.eye">cfg</a>' +
        '<button class=ghost ' + (active ? 'disabled' : '') + ' onclick="renamePkg(' + i + ')">Rename</button>' +
        '<button class=danger ' + (active || last === 0 ? 'disabled' : '') + ' onclick="deletePkg(' + i + ')">Delete</button>' +
      '</div></div>';
  }).join('');
}

async function refresh() {
  try {
    s = await (await fetch('/api/status')).json();
  } catch (e) {
    q('status').textContent = 'Device unavailable';
    return;
  }
  renderStatus();
  renderAudio();
  renderBrightness();
  renderStyles();
  renderPackages();
  q('cycle').textContent = s.cycle ? 'Disable cycle' : 'Enable cycle';
  set(q('interval'), s.interval);
  q('setup').hidden = !s.provisioning;
  // During captive-portal setup the Wi-Fi form is the only thing that matters.
  if (s.provisioning && !sawProvisioning) { sawProvisioning = true; show('c'); }
}

function toggleCycle() { post('/api/cycle', { enabled: !s.cycle, interval: q('interval').value }); }
function saveCycle() { post('/api/cycle', { enabled: s.cycle, interval: q('interval').value }); }

function move(i, delta) {
  var order = s.ids.slice(), j = i + delta;
  if (j < 0 || j >= order.length) return;
  order[i] = s.ids[j];
  order[j] = s.ids[i];
  post('/api/packages/order', { order: order.join(',') });
}

function renamePkg(i) {
  var name = prompt('New package ID', s.ids[i]);
  if (name) post('/api/packages/rename', { id: s.ids[i], newId: name });
}

function deletePkg(i) {
  if (confirm('Delete ' + s.styles[i] + '?')) post('/api/packages/delete', { id: s.ids[i] });
}

async function uploadPackage() {
  var note = q('uploadNote');
  var id = q('packageId').value;
  var files = q('packageFiles').files;
  if (!id || !files.length) { note.textContent = 'Enter a package ID and choose files.'; return; }

  var fail = function (message) { note.textContent = 'Upload failed: ' + message; };
  note.textContent = 'Starting…';
  var r = await fetch('/api/packages/upload/start', {
    method: 'POST',
    headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
    body: new URLSearchParams({ id: id })
  });
  if (!r.ok) return fail(await r.text());
  var token = (await r.json()).token;

  for (var i = 0; i < files.length; i++) {
    note.textContent = 'Uploading ' + files[i].name + ' (' + (i + 1) + '/' + files.length + ')…';
    var body = new FormData();
    body.append('file', files[i], files[i].name);
    r = await fetch('/api/packages/upload/file?token=' + token, { method: 'POST', body: body });
    if (!r.ok) return fail(await r.text());
  }

  note.textContent = 'Validating…';
  r = await fetch('/api/packages/upload/commit', {
    method: 'POST',
    headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
    body: new URLSearchParams({ token: token })
  });
  if (!r.ok) return fail(await r.text());
  note.textContent = 'Uploaded ' + id + '.';
  q('packageId').value = '';
  q('packageFiles').value = '';
  refresh();
}

// Screenshots. The device serves a BMP built from a mirror of the panel, and
// the cache-buster is what makes each request a fresh frame rather than the
// one the browser already has.
var liveTimer = null;

function capture() {
  q('shot').src = '/api/frame?t=' + Date.now();
}

function stopLive() {
  if (liveTimer) clearInterval(liveTimer);
  liveTimer = null;
  q('live').textContent = 'Start live';
}

function toggleLive() {
  if (liveTimer) return stopLive();
  // Chained on load rather than on a fixed interval: the device answers when
  // it can, and piling requests onto a single-threaded server helps nobody.
  q('live').textContent = 'Stop live';
  liveTimer = setInterval(function () {
    if (q('shot').complete) capture();
  }, 400);
}

q('shot').addEventListener('error', function () {
  q('shotNote').textContent = 'Capture failed: the device had no buffer to spare.';
});

// A download needs its own fresh frame, not the one already on screen.
q('shotSave').addEventListener('click', function () {
  this.href = '/api/frame?t=' + Date.now();
});

// The slider fires input continuously while dragging. The device polls its
// single-threaded web server from the render loop, so only commit on release.
q('volume').addEventListener('input', function () { q('volpct').textContent = this.value + '%'; });
q('volume').addEventListener('change', function () { post('/api/audio/volume', { volume: this.value }); });
q('brightness').addEventListener('input', function () { q('brightpct').textContent = this.value + '%'; });
q('brightness').addEventListener('change', function () { post('/api/display/brightness', { brightness: this.value }); });
q('style').addEventListener('change', function () { post('/api/style', { style: this.value }); });

addEventListener('hashchange', function () { show(location.hash.slice(1)); });
show(location.hash.slice(1));
refresh();
setInterval(refresh, 2500);
