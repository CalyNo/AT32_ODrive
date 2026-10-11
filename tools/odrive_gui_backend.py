"""Start the official ODrive GUI's Python backend without editing upstream.

The GUI frontend talks only socket.io (there is no WebUSB path in GUI/src), so
the browser GUI is useless without this backend.  Upstream's
GUI/server/odrive_server.py cannot be used as-is here for three reasons, none of
which are ours to fix upstream:

  1. fibre.utils.Logger.__init__ uses win32console unconditionally on Windows
     while the import is guarded by try/except ImportError, so without pywin32
     it dies with `NameError: name 'win32console' is not defined`.
     Fix: `pip install pywin32`.
  2. It calls socketio.run() with the default allow_unsafe_werkzeug=False, which
     python-engineio >= 4 raises on under Werkzeug 3.x.  Fix: pass the flag.
  3. **findODrives only ever works once per process.**  Its handler calls
     start_discovery(), which builds a brand new fibre.Domain() and enters it on
     every call, and never releases the previous one.  The first call finds the
     device (the log says "Found <serial>"); every later call finds nothing and
     just prints "starting disco loop...", because the device is already claimed.
     That is invisible in the intended usage -- the electron app connects once
     and stays connected -- but it breaks any browser reload, because the GUI
     populates its device list *only* from the odrive-found event that
     discovered_device emits, and that event is never emitted again.  Fix:
     re-announce devices we already hold instead of re-running discovery, and
     only fall back to real discovery when we hold nothing.

Protocol pairing matters too: GUI/src uses socket.io-client 3.0.4 (Engine.IO v4),
which needs flask-socketio 5.x + python-engineio 4.x.  flask-socketio 4.x speaks
Engine.IO v3 and the handshake simply fails.

Usage:
    python tools/odrive_gui_backend.py <path-to-ODrive-clone>/GUI/server/odrive_server.py
    python tools/odrive_gui_backend.py <server.py> [<tools-dir>]

<tools-dir> defaults to <path-to-ODrive-clone>/tools, which is where upstream
expects the odrive/ and fibre/ python packages to live.
"""

import importlib.util
import json
import os
import sys

MODULE_NAME = "odrive_server_patched"


def _port_already_serving(host, port, timeout=1.5):
    """True if something is already listening on host:port.

    Werkzeug sets allow_reuse_address, which on Windows lets a second process
    bind a port that is already in use.  Starting a second backend therefore
    does not fail loudly -- instead both listen, connections go to one of them
    (typically the older, possibly wedged one), and the new one looks dead.  It
    also means two processes can drive the same USB device, which corrupts
    libfibre's transport.  Refuse to start in that case.
    """
    import socket

    probe = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    probe.settimeout(timeout)
    try:
        return probe.connect_ex((host, port)) == 0
    finally:
        probe.close()


def _load_server_module(server_script, tools_dir):
    """Import upstream's server as a module, not as __main__.

    Running it as __main__ (upstream's own entry point, or runpy) executes the
    module and then blocks forever inside socketio.run(), which leaves no way to
    patch its handlers.  Importing it under a different name skips the __main__
    block, so we can install the fixes below and start the server ourselves.

    The price is that we must inject what that block would have set up.
    """
    sys.path.insert(0, tools_dir)

    # Order matters: fibre/ does not live directly under tools/, it lives at
    # tools/odrive/pyfibre/fibre, and it is odrive/__init__.py that puts that
    # directory on sys.path.  So odrive must be imported first.
    import odrive
    import odrive.utils  # noqa: F401  (upstream's __main__ imports this)
    import fibre

    spec = importlib.util.spec_from_file_location(MODULE_NAME, server_script)
    if spec is None or spec.loader is None:
        raise RuntimeError("cannot load " + server_script)
    module = importlib.util.module_from_spec(spec)
    sys.modules[MODULE_NAME] = module
    spec.loader.exec_module(module)

    # Everything upstream's `if __name__ == "__main__":` block would have bound
    # in the module namespace, since its functions read these as globals.
    module.odrive = odrive
    module.fibre = fibre
    module.odrives = {}
    module.odrives_status = {}
    module.discovered_devices = []
    module.inUse = False
    module.log = fibre.Logger(verbose=False)
    module.shutdown = fibre.Event()

    return module


def _install_find_odrives_fix(module):
    """Make findODrives work on every call, not just the first (see above)."""

    @module.socketio.on("findODrives")
    def _find_odrives(message):
        known = [name for name, ok in module.odrives_status.items() if ok]
        if known:
            print("findODrives: re-announcing known odrive(s): " + ", ".join(known))
            module.socketio.emit("odrives-status", json.dumps(module.odrives_status))
            module.socketio.emit("odrive-found")
            return
        print("looking for odrive")
        module.start_discovery()

    print("patched: findODrives re-announces known devices instead of only "
          "discovering once per process")


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2

    server_script = os.path.abspath(argv[1])
    if not os.path.isfile(server_script):
        print("no such server script: " + server_script)
        return 2

    if len(argv) > 2:
        tools_dir = os.path.abspath(argv[2])
    else:
        # <clone>/GUI/server/odrive_server.py -> <clone>/tools
        tools_dir = os.path.join(
            os.path.dirname(os.path.dirname(os.path.dirname(server_script))), "tools")

    if not os.path.isdir(tools_dir):
        print("no such tools directory: " + tools_dir)
        return 2

    if _port_already_serving("127.0.0.1", 5000):
        print("port 5000 is ALREADY being served -- refusing to start a second backend.")
        print("Two backends can both bind it on Windows, and then two processes drive")
        print("the same USB device, which is what corrupts libfibre. Kill the old one")
        print("first (look for a python.exe running odrive_server.py / odrive_gui_backend.py).")
        return 3

    print("gui backend : " + server_script)
    print("import path : " + tools_dir)

    module = _load_server_module(server_script, tools_dir)
    _install_find_odrives_fix(module)

    print("listening on http://127.0.0.1:5000  (socket.io, Engine.IO v4)")
    module.socketio.run(module.app, host="0.0.0.0", port=5000,
                        allow_unsafe_werkzeug=True)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
