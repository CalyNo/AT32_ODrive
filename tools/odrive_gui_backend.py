"""Start the official ODrive GUI's Python backend without editing upstream.

The GUI frontend talks only socket.io (there is no WebUSB path in GUI/src), so
the browser GUI is useless without this backend.  Upstream's
GUI/server/odrive_server.py cannot start unmodified on a current Windows
Python, and neither problem is ours to patch upstream:

  1. fibre.utils.Logger.__init__ uses win32console unconditionally on Windows
     while the import is guarded by try/except ImportError, so without pywin32
     it dies with `NameError: name 'win32console' is not defined`.  Fix:
     `pip install pywin32`.
  2. It calls socketio.run() with the default allow_unsafe_werkzeug=False, which
     python-engineio >= 4 raises on under Werkzeug 3.x.  Fix: patch the bound
     method here.

Protocol pairing matters too: GUI/src uses socket.io-client 3.0.4 (Engine.IO v4),
which needs flask-socketio 5.x + python-engineio 4.x.  flask-socketio 4.x speaks
Engine.IO v3 and the handshake simply fails.

Usage:
    python tools/odrive_gui_backend.py <path-to-ODrive-clone>/GUI/server/odrive_server.py
    python tools/odrive_gui_backend.py <server.py> [<tools-dir>]

<tools-dir> defaults to <path-to-ODrive-clone>/tools, which is where upstream
expects the odrive/ and fibre/ python packages to live.
"""

import os
import runpy
import sys

import flask_socketio

_ORIGINAL_RUN = flask_socketio.SocketIO.run


def _permissive_run(self, app, *args, **kwargs):
    kwargs.setdefault("allow_unsafe_werkzeug", True)
    return _ORIGINAL_RUN(self, app, *args, **kwargs)


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

    # Upstream's own __main__ block inserts each of sys.argv[1:] into sys.path;
    # do the same here so the path is in place before runpy takes over.
    sys.path.insert(0, tools_dir)
    sys.argv = [server_script, tools_dir]

    print("gui backend : " + server_script)
    print("import path : " + tools_dir)
    print("listening on http://127.0.0.1:5000  (socket.io, Engine.IO v4)")

    flask_socketio.SocketIO.run = _permissive_run
    runpy.run_path(server_script, run_name="__main__")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
