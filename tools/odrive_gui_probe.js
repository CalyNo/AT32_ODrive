/*
 * Drive the official GUI's Python backend exactly as the browser GUI does,
 * headlessly, and print what came back.
 *
 * Why this exists: the board's Fibre layer can be correct while the GUI still
 * shows nothing, because the GUI only ever sees values that round-tripped
 * through odrive_server.py.  This probe exercises that whole path -- socket.io
 * client -> backend -> pyfibre -> USB -> firmware -- using the GUI's own
 * bundled socket.io-client, so no extra install is needed and the client is
 * byte-identical to the one the browser runs.
 *
 * Usage:
 *   node tools/odrive_gui_probe.js [<path-to-ODrive-clone>/GUI] [<server-url>]
 *
 * Defaults assume the clone sits next to this repository (../ODrive/GUI, the
 * layout this project was developed in) and that the backend is on
 * http://127.0.0.1:5000.
 */

const path = require('path');

const guiRoot = process.argv[2] || path.join(__dirname, '..', '..', 'ODrive', 'GUI');
const serverUrl = process.argv[3] || 'http://127.0.0.1:5000';

let io;
try {
  io = require(path.join(guiRoot, 'node_modules', 'socket.io-client'));
} catch (e) {
  console.log('cannot load socket.io-client from ' + guiRoot);
  console.log('pass the GUI directory as argv[2]; was it npm-installed?');
  process.exit(2);
}

/* Properties worth reading back: two are known to be width-sensitive on this
 * firmware (requested_state/current_state are enums stored 4 bytes wide), the
 * rest are ordinary scalars that should read as plain numbers. */
const INTERESTING = [
  'axis0.requested_state',
  'axis0.current_state',
  'axis0.motor.config.current_lim',
  'axis0.controller.config.pos_gain',
  'vbus_voltage',
  'fw_version_major',
  'board.reset_cause',
];

const findings = {};
let gotTree = false;
let asked = 0;
let done = false;
let treeRetry = null;
let sawOdriveFound = false;
const startedAt = Date.now();

function finish(code, note) {
  if (done) return;
  done = true;
  console.log('');
  console.log('=== values read back through the GUI backend ===');
  for (const p of INTERESTING) {
    const v = findings[p];
    console.log('  ' + p + ' = ' + (v === undefined ? '<no answer>' : JSON.stringify(v)));
  }
  console.log('');
  console.log('odrive-found event seen: ' + sawOdriveFound
    + (sawOdriveFound ? '' : '  <-- the GUI would stay empty without this'));
  if (note) console.log('NOTE: ' + note);
  console.log('RESULT: ' + (code === 0 ? 'GUI BACKEND TALKS TO THE BOARD' : 'FAILED'));
  socket.close();
  process.exit(code);
}

const socket = io(serverUrl, { reconnection: false, timeout: 8000 });

socket.on('connect', () => {
  console.log('connected to ' + serverUrl + ' (id ' + socket.id + ')');
  console.log('asking the backend to look for ODrives...');
  socket.emit('findODrives', {});
  /* Do not gate on the odrives-status event: the backend only emits it on a
   * fresh discovery or on a disconnect, so a device it already knows about
   * (or one that vanished while a stale handle survived) produces no event at
   * all and waiting for one just times out.  Poll getODrives instead. */
  askForTree();
  treeRetry = setInterval(askForTree, 3000);
});

function askForTree() {
  const now = Date.now();
  if (gotTree || now - startedAt > 21000) {
    if (treeRetry) clearInterval(treeRetry);
    return;
  }
  socket.emit('getODrives', {});
}

socket.on('odrives-status', (payload) => {
  console.log('odrives-status: ' + payload);
});

/* This is the event the GUI actually waits for: store.js dispatches getOdrives
 * only from an odrive-found callback, and the backend emits it only when a
 * discovery actually turns up a device.  If it never arrives, the GUI stays
 * empty no matter how healthy the backend's device handle is. */
socket.on('odrive-found', () => {
  sawOdriveFound = true;
  console.log('odrive-found received (this is what makes the GUI ask for the list)');
});

socket.on('odrives', (payload) => {
  const tree = JSON.parse(payload);
  const keys = Object.keys(tree);
  if (keys.length === 0) {
    console.log('backend reports no ODrives yet (retrying...)');
    return;
  }
  gotTree = true;
  if (treeRetry) clearInterval(treeRetry);
  console.log('property tree received for: ' + JSON.stringify(keys));
  console.log('top-level members: ' + Object.keys(tree[keys[0]]).join(', '));
  for (const p of INTERESTING) {
    socket.emit('getProperty', { path: keys[0] + '.' + p });
  }
});

socket.on('ODriveProperty', (payload) => {
  const msg = JSON.parse(payload);
  console.log('  <- ' + msg.path + ' = ' + JSON.stringify(msg.val));
  // The backend echoes the fully qualified path (odrive0.<rest>); key the
  // summary by the unqualified name that was requested.
  findings[msg.path.split('.').slice(1).join('.')] = msg.val;
  asked += 1;
  if (asked >= INTERESTING.length) finish(0);
});

socket.on('connect_error', (e) => {
  finish(1, 'connect_error: ' + e.message);
});

setTimeout(() => {
  finish(1, gotTree
    ? 'the backend still lists an ODrive but answered none of the property '
      + 'reads -- that is a stale handle, i.e. the device re-enumerated '
      + '(flashed/reset) and the backend never noticed. Restart the backend.'
    : 'the backend never produced a property tree; the board is not being '
      + 'discovered at all (check USB enumeration and power).');
}, 45000);
