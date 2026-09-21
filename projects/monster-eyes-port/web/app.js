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

var TABS = ['c', 'p', 's', 'e'];

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
  if (tab === 'e') loadConfig();
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
  // Connected, the network's name is the useful part: a page being read over
  // that network hardly needs telling it is connected. Otherwise the state is
  // the news, and the name says what it is waiting on.
  var wifi = s.wifi === 'connected' ? (s.ssid || 'connected')
           : s.ssid ? s.wifi + ' to ' + s.ssid
           : s.wifi;
  q('status').innerHTML =
    '<b>Style</b><span>' + h(s.styleName) + '</span>' +
    '<b>Mode</b><span>' + h(s.mode) + '</span>' +
    '<b>Battery</b><span>' + battery + '</span>' +
    '<b>Power</b><span>' + power + '</span>' +
    '<b>Wi-Fi</b><span>' + h(wifi) + ' ' + h(s.ip || '') + '</span>' +
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

// The poll runs every couple of seconds, and rewriting a control's innards is
// not free on a phone: replacing the options of a <select> while its picker is
// open closes and reopens it, which on Firefox for Android reads as the list
// flashing while you are trying to choose from it. So only write when the
// content has actually changed, and never while the control is in use.
var lastStyleList = null;

function renderStyles() {
  var html = s.styles.map(function (name, i) {
    return s.enabled[i] ? '<option value=' + i + '>' + h(name) + '</option>' : '';
  }).join('');
  if (html !== lastStyleList && document.activeElement !== q('style')) {
    q('style').innerHTML = html;
    lastStyleList = html;
  }
  set(q('style'), s.style);
}

var lastPackageList = null;

function renderPackages() {
  var last = s.styles.length - 1;
  var html = s.styles.map(function (name, i) {
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
  if (html === lastPackageList) return;   // Same rows; leave the DOM alone
  q('eyes').innerHTML = html;
  lastPackageList = html;
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

// ---------------------------------------------------------------------------
// config.eye editor
//
// This mirrors the simulator's panel in sim/src/config_panel.cpp: the same
// sections in the same order, the same controls in each, and the same help
// text. They are two hand-written copies of one list, so an edit to either
// belongs in both.
//
// A `*` after a name means the setting is an extension of this fork and a
// stock Adafruit Monster Eyes package will not understand it.
const EXT = ' *Extension, not in upstream Monster Eyes.';

var CONFIG_SECTIONS = [
  {
    title: 'Geometry',
    note: 'Eyeball and iris sizes, in config pixels not screen.',
    rows: [
      { key: 'eyeRadius', type: 'int', min: 0, max: 250, def: 0,
        help: "Eyeball radius, 0 to derive it. In the config's own pixel space, which begin() rescales to the display." },
      { key: 'irisRadius', type: 'int', min: 0, max: 250, def: 0,
        help: "Iris radius in the config's own pixel space; 0 derives it." },
      { key: 'displaySize', type: 'int', min: 0, max: 240, def: 0,
        help: 'Eye width and height in pixels; 0 fills the display.' },
      { key: 'coverage', type: 'float', min: 0, max: 1.5, step: 0.001, def: 0.6,
        help: 'How much of the eyeball the display shows. begin() may raise it to fit.' },
      { key: 'fixate', type: 'int', min: -60, max: 60, def: 7,
        help: 'Convergence toward the face, in map pixels.' },
      { key: 'tracking', type: 'bool', def: true,
        help: 'Let the lids follow the gaze. Squint does nothing without it.' },
      // Squint is the resting offset of that tracking and is read nowhere
      // else, so it is inert while tracking is off rather than merely subtle.
      { key: 'squint', type: 'float', min: 0, max: 1, step: 0.001, def: 0.5, needs: 'tracking',
        help: 'Where the lids rest while they track the gaze; inert with tracking off. Raising it lowers the upper lid and drops the lower with it, scaled by irisRadius.' }
    ]
  },
  {
    title: 'Pupil',
    note: 'Its shape, and how far it opens and closes.',
    rows: [
      { key: 'slitPupilRadius', type: 'int', min: -1, max: 250, def: 0,
        help: '0 for a round pupil, -1 to derive it, or the slit length in pixels.' },
      { key: 'pupilMin', type: 'float', min: 0, max: 1, step: 0.001, def: 0.05,
        help: 'Smallest pupil as a fraction of the iris, or the smallest the iris disc gets with irisDilation on.' },
      { key: 'pupilMax', type: 'float', min: 0, max: 1, step: 0.001, def: 0.25,
        help: 'Largest pupil as a fraction of the iris, or the largest the iris disc gets with irisDilation on.' },
      { key: 'slitPupilHorizontal', type: 'bool', def: false, ext: true,
        help: 'Lay the slit on its side.' + EXT },
      { key: 'slitPupilRounded', type: 'bool', def: false, ext: true,
        help: 'Round the ends of the slit.' + EXT },
      // Resizing the iris leaves no pupil to fill, so one setting makes the
      // other dead weight rather than merely changing what it does.
      { key: 'texturedPupil', type: 'bool', def: false, ext: true, deadWith: 'irisDilation',
        help: 'Fill the pupil from the iris texture instead of a flat colour, for a pattern that runs to the centre. Ignored with irisDilation on.' + EXT },
      { key: 'irisDilation', type: 'bool', def: false, ext: true,
        help: "Resize the iris instead of opening a pupil in it. pupilMin and pupilMax then mean the disc's smallest and largest." + EXT }
    ]
  },
  {
    title: 'Display',
    note: 'How many eyes the panel shows.',
    rows: [
      { key: 'singleEye', type: 'bool', def: false, ext: true, feature: 'display',
        help: 'One eye filling the panel, 240px centred, instead of two 128px eyes side by side. The eye is rebuilt, so its textures reload at the new size.' + EXT },
      { key: 'side', type: 'select', options: ['left', 'right'], def: 'left',
        ext: true, feature: 'display', needsExt: 'singleEye',
        help: "Which eye the single one is, and so which of the config's left and right blocks applies to it." + EXT },
      { key: 'eyeGap', type: 'int', min: -64, max: 96, def: 28, ext: true,
        feature: 'display',
        help: "Pixels between the two eye squares; each eye moves half the difference, so the pair stays centred. The panel's own layout leaves 28. Negative overlaps them, which suits a face whose eyes nearly touch -- but past the point where one square reaches the other eye's artwork it writes its background over it." + EXT }
    ]
  },
  {
    title: 'Colours',
    note: 'Used where a texture is missing.',
    rows: [
      { key: 'irisColor', type: 'color', def: 0x001F,
        help: 'Flat iris colour, used when irisTexture is missing or is a 1x1 bitmap.' },
      { key: 'scleraColor', type: 'color', def: 0xFFFF,
        help: 'Flat sclera colour, used when scleraTexture is missing.' },
      { key: 'pupilColor', type: 'color', def: 0x0000, help: 'Fill colour of the pupil.' },
      { key: 'backColor', type: 'color', def: 0x5000,
        help: 'Shown outside the eyeball, where no eyelid covers it.' },
      { key: 'eyelidColor', type: 'color', def: 0x0000,
        help: 'The eyelids, and the background the panel is cleared to.' }
    ]
  },
  {
    title: 'Animation',
    note: 'What the eye does when nothing is steering it.',
    rows: [
      { key: 'gazeMax', type: 'int', min: 100000, max: 10000000, step: 100000, def: 3000000,
        help: 'Longest wait between major eye movements, in microseconds. Only matters with autoGaze on.' },
      { key: 'autoGaze', type: 'bool', def: true, ext: true, feature: 'animation',
        help: 'Let the eye look around on its own. Off holds the gaze still, for a package that should stare.' + EXT },
      { key: 'autoBlink', type: 'bool', def: true, ext: true, feature: 'animation',
        help: 'Let the eye blink on its own. Off means it never blinks.' + EXT },
      { key: 'cyclovergence', type: 'float', min: 0, max: 90, step: 0.5, def: 0,
        ext: true, feature: 'animation',
        help: 'Roll the eyes as the gaze goes down, the way a grazing animal keeps its slit level with the horizon while its head is lowered. This is the angle at full downward gaze; looking level or up leaves the eyes level.' + EXT },
      { key: 'gazeRange', type: 'float', min: 0, max: 1, step: 0.01, def: 1,
        ext: true, feature: 'animation',
        help: "How far the eye may look, as a fraction of what the geometry allows. 1 is everything; less keeps a drawn eye's iris inside its own white and its artwork inside the box it is drawn in." + EXT }
    ]
  },
  {
    title: 'Rotation',
    note: 'Rolls the whole eyeball, and spins its textures in place.',
    rows: [
      { key: 'irisSpin', type: 'float', min: -30, max: 30, step: 0.01, def: 0,
        help: 'Turn the iris texture continuously. Positive is clockwise, 0 holds it still.' },
      { key: 'scleraSpin', type: 'float', min: -30, max: 30, step: 0.01, def: 0,
        help: 'Turn the sclera texture continuously.' },
      { key: 'irisAngle', type: 'int', min: 0, max: 1023, def: 0,
        help: 'Where the iris texture starts, 0-1023 counter-clockwise.' },
      { key: 'scleraAngle', type: 'int', min: 0, max: 1023, def: 0,
        help: 'Where the sclera texture starts, 0-1023 counter-clockwise.' },
      { key: 'irisMirror', type: 'bool', def: false,
        help: 'Mirror the iris texture, reversing which way its detail runs.' },
      { key: 'scleraMirror', type: 'bool', def: false,
        help: 'Mirror the sclera texture.' },
      { key: 'roll', type: 'float', min: -90, max: 90, step: 0.5, def: 0, ext: true,
        help: 'Roll the eyeball about its own optic axis. The two eyes take opposite angles, as a grazing animal\'s do when its head goes down and it keeps the slit level with the horizon. Pupil, iris and sclera turn together; the lids do not.' + EXT }
    ]
  },
  {
    title: 'Iris flow',
    note: 'Iris creeps along a moving wave, without turning.',
    rows: [
      { key: 'irisFlow', type: 'float', min: 0, max: 1, step: 0.001, def: 0, ext: true,
        help: 'How far the sampling shifts at the peak, as a fraction of iris depth. 0 switches the effect off.' + EXT },
      { key: 'irisFlowSpeed', type: 'float', min: -10, max: 10, step: 0.01, def: 1, ext: true,
        help: 'Wave crests leaving the pupil per second. Negative draws them inward.' + EXT },
      { key: 'irisFlowWaves', type: 'float', min: 0, max: 20, step: 0.01, def: 2, ext: true,
        help: 'How many crests sit between the pupil and the rim.' + EXT }
    ]
  }
];

var cfg = null;      // The parsed config.eye, or null before it is fetched
var cfgLoadedFor = '';  // Package the form was built for
// Which sections the reader has opened. All shut to begin with: thirty-odd
// controls unrolled at once is a long scroll on a phone, and the section
// titles are the map. Kept here rather than read back off the DOM because the
// form is rebuilt from scratch when a setting gates another one, and a
// rebuilt <details> forgets it was open.
var cfgOpen = {};

// config.eye is JSON with // comments, which the device's parser takes and
// JSON.parse does not. Strings are stepped over so a // inside an asset path
// survives.
function parseConfig(text) {
  var out = '', inString = false, i;
  for (i = 0; i < text.length; i++) {
    var c = text[i];
    if (inString) {
      out += c;
      if (c === '\\') { out += text[++i] || ''; continue; }
      if (c === '"') inString = false;
      continue;
    }
    if (c === '"') { inString = true; out += c; continue; }
    if (c === '/' && text[i + 1] === '/') {
      while (i < text.length && text[i] !== '\n') i++;
      out += '\n';
      continue;
    }
    if (c === '/' && text[i + 1] === '*') {
      i += 2;
      while (i < text.length && !(text[i] === '*' && text[i + 1] === '/')) i++;
      i++;
      continue;
    }
    out += c;
  }
  return JSON.parse(out);
}

function cfgFeature(row) { return row.feature ? row.feature : null; }

function cfgGet(row) {
  if (cfgFeature(row)) {
    var ext = cfg.extensions && cfg.extensions[row.feature];
    var v = ext ? ext[row.key] : undefined;
    return v === undefined ? row.def : v;
  }
  return cfg[row.key];
}

function cfgSet(row, value) {
  if (cfgFeature(row)) {
    if (!cfg.extensions) cfg.extensions = {};
    if (!cfg.extensions[row.feature]) cfg.extensions[row.feature] = {};
    cfg.extensions[row.feature][row.key] = value;
  } else if (value === null) {
    delete cfg[row.key];
  } else {
    cfg[row.key] = value;
  }
}

// Colours are stored as 0xRRRR RGB565, an [r,g,b] triple or a plain number,
// and are written back the way the simulator writes them: a hex string.
function toRgb565(value, fallback) {
  if (typeof value === 'number') return value & 0xffff;
  // "0x1F00" or a decimal string; Number() reads both.
  if (typeof value === 'string') return Number(value) & 0xffff;
  if (Array.isArray(value) && value.length >= 3) {
    var c = value.map(function (v) {
      var n = typeof v === 'string' ? parseInt(v) : v;
      if (n > 0 && n <= 1 && !Number.isInteger(n)) n = n * 255.999;
      return Math.max(0, Math.min(255, Math.round(n)));
    });
    return ((c[0] & 0xf8) << 8) | ((c[1] & 0xfc) << 3) | (c[2] >> 3);
  }
  return fallback;
}

function rgb565ToHtml(c) {
  var r = (c >> 11) & 0x1f, g = (c >> 5) & 0x3f, b = c & 0x1f;
  var hex = function (v) { return ('0' + v.toString(16)).slice(-2); };
  return '#' + hex((r << 3) | (r >> 2)) + hex((g << 2) | (g >> 4)) + hex((b << 3) | (b >> 2));
}

function htmlToRgb565(value) {
  var n = parseInt(value.slice(1), 16);
  return (((n >> 16) & 0xf8) << 8) | ((((n >> 8) & 0xfc)) << 3) | ((n & 0xff) >> 3);
}

function rgb565Hex(c) {
  return '0x' + ('000' + c.toString(16).toUpperCase()).slice(-4);
}

function rowDisabled(row) {
  // Greyed out because another setting has taken it out of play entirely.
  if (row.deadWith && !!cfg[row.deadWith]) return true;
  if (row.needs) return !(cfg[row.needs] === undefined ? true : !!cfg[row.needs]);
  if (row.needsExt) {
    var ext = cfg.extensions && cfg.extensions[row.feature];
    return !(ext && ext[row.needsExt]);
  }
  return false;
}

function renderConfigForm() {
  if (!cfg) return;
  var html = CONFIG_SECTIONS.map(function (section, si) {
    var rows = section.rows.map(function (row, ri) {
      var id = 'cfg-' + si + '-' + ri;
      var value = cfgGet(row);
      var off = rowDisabled(row) ? ' disabled' : '';
      var input;
      if (row.type === 'bool') {
        var on = value === undefined ? row.def : !!value;
        input = '<input type=checkbox id=' + id + (on ? ' checked' : '') + off + '>';
      } else if (row.type === 'select') {
        input = '<select id=' + id + off + '>' + row.options.map(function (o) {
          return '<option' + (o === value ? ' selected' : '') + '>' + o + '</option>';
        }).join('') + '</select>';
      } else if (row.type === 'color') {
        // A swatch with the stored value beside it, as the panel's ColorEdit3
        // shows one. The readout is what the file says, RGB565 in hex, so a
        // colour can be read off and typed into a config by hand.
        var c = toRgb565(value, row.def);
        input = '<input type=color id=' + id + ' value=' + rgb565ToHtml(c) + off + '>' +
          '<span class=tag id=' + id + '-v>' + rgb565Hex(c) +
          (value === undefined ? ' (default)' : '') + '</span>';
      } else {
        // Sliders, as in the simulator's panel: the useful range is the point
        // of the control, and a number box invites values the renderer will
        // only clamp. The readout beside it says where the slider is, and
        // whether that is the file's value or the built-in default.
        var at = value === undefined ? row.def : value;
        input = '<input type=range id=' + id + ' min=' + row.min +
          ' max=' + row.max + ' step=' + (row.step || 1) +
          ' value="' + h(at) + '"' + off + '>' +
          '<span class=tag id=' + id + '-v>' + h(at) +
          (value === undefined ? ' (default)' : '') + '</span>';
      }
      return '<div class=cfgrow><label for=' + id + '>' + h(row.key) +
        (row.ext ? '*' : '') + '</label>' + input +
        '<p class=note>' + h(row.help) + '</p></div>';
    }).join('');
    return '<details' + (cfgOpen[si] ? ' open' : '') + ' data-section=' + si +
      '><summary>' + h(section.title) +
      '</summary><p class=note>' + h(section.note) + '</p>' + rows + '</details>';
  }).join('');
  var scrolled = window.scrollY;
  q('cfgForm').innerHTML = html;
  window.scrollTo(0, scrolled);

  Array.prototype.forEach.call(
    q('cfgForm').querySelectorAll('details'), function (d) {
      d.addEventListener('toggle', function () {
        cfgOpen[d.getAttribute('data-section')] = d.open;
      });
    });

  CONFIG_SECTIONS.forEach(function (section, si) {
    section.rows.forEach(function (row, ri) {
      var el = q('cfg-' + si + '-' + ri);
      if (row.type === 'int' || row.type === 'float') {
        // Dragging fires input continuously. Only the readout follows it; the
        // config is written on release, so a drag is one edit and not fifty.
        el.addEventListener('input', function () {
          q('cfg-' + si + '-' + ri + '-v').textContent = el.value;
        });
      }
      el.addEventListener('change', function () {
        if (row.type === 'bool') cfgSet(row, el.checked);
        else if (row.type === 'select') cfgSet(row, el.value);
        else if (row.type === 'color') {
          // The browser picker has 8 bits a channel and the panel has 5 or 6,
          // so most nudges land on the colour already stored. Writing anyway
          // would rebuild the eyes for no visible change.
          var packed = htmlToRgb565(el.value);
          if (packed === toRgb565(cfgGet(row), row.def)) return;
          cfgSet(row, rgb565Hex(packed));
        }
        else cfgSet(row, Number(el.value));
        // Only tracking and singleEye decide whether another control means
        // anything, so only those need the form rebuilding. Rebuilding on
        // every edit threw away which sections were open and where the page
        // was scrolled to.
        if (row.key === 'tracking' || row.key === 'singleEye' ||
            row.key === 'irisDilation') {
          renderConfigForm();
          return;
        }
        var readout = q('cfg-' + si + '-' + ri + '-v');
        if (readout)
          readout.textContent = row.type === 'color'
            ? rgb565Hex(htmlToRgb565(el.value)) : el.value;
      });
    });
  });
}

async function loadConfig(force) {
  var pkg = s.ids ? s.ids[s.style] : '';
  if (!force && cfg && cfgLoadedFor === pkg) return;
  var r = await fetch('/api/config');
  if (!r.ok) { q('cfgForm').textContent = 'No config on the device.'; return; }
  try {
    cfg = parseConfig(await r.text());
  } catch (e) {
    q('cfgForm').textContent = 'Config could not be parsed: ' + e.message;
    return;
  }
  cfgLoadedFor = pkg;
  q('cfgPkg').textContent = pkg + (r.headers.get('X-Config-Unsaved') === '1' ? ' (unsaved)' : '');
  renderConfigForm();
}

async function postConfig(url, note, params) {
  var query = params ? '?' + new URLSearchParams(params) : '';
  var r = await fetch(url + query, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(cfg, null, 2)
  });
  q('cfgNote').textContent = r.ok ? note : 'Failed: ' + (await r.text() || r.status);
  refresh();
  return r.ok;
}

function applyConfig() {
  postConfig('/api/config/apply', 'Running on these settings. Not saved: a reboot undoes them.');
}

function overwriteConfig() {
  postConfig('/api/config/overwrite', 'Written to this package.');
}

async function saveConfigAs() {
  var id = q('cfgSaveAs').value;
  if (!id) { q('cfgNote').textContent = 'Enter a package ID.'; return; }
  if (await postConfig('/api/config/save-as', 'Saved as ' + id + '.', { id: id })) {
    q('cfgSaveAs').value = '';
    await loadConfig(true);
  }
}

async function revertConfig() {
  await fetch('/api/config/revert', { method: 'POST' });
  await loadConfig(true);
  q('cfgNote').textContent = "Back to the package's saved config.";
  refresh();
}
