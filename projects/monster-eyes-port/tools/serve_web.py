"""Serve web/ with a fake device API so the UI can be developed without hardware.

    python projects/monster-eyes-port/tools/serve_web.py
    open http://localhost:8000
"""
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import parse_qs
import json
import sys

WEB = Path(__file__).resolve().parents[1] / "web"
PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 8000

# Deliberately awkward: a very long name, and one containing markup so the
# escaping in app.js is exercised on every load.
NAMES = [
    "Hazel", "Dragon", "No sclera", "Goat", "Newt", "Terminator", "Cat", "Owl",
    "Nauga", "Deer", "Anime", "Hazel sigh", "A package with a really quite long name",
    "<b>injected</b>",
]
state = {
    "mode": "Manual",
    "style": 2,
    "styleName": NAMES[2],
    "styleCount": len(NAMES),
    "batteryPercent": 84,
    "audioPresent": True,
    "hasSound": True,
    "muted": False,
    "volume": 70,
    "brightness": 100,
    "externalPower": "unknown",
    "cycle": False,
    "interval": 30,
    "wifi": "connected",
    "ip": "192.168.1.42",
    "configured": True,
    "provisioning": False,
    "styles": NAMES,
    "enabled": [True] * len(NAMES),
    "ids": [n.lower().replace(" ", "_").replace("<b>", "").replace("</b>", "") for n in NAMES],
}


class Handler(SimpleHTTPRequestHandler):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=str(WEB), **kwargs)

    def log_message(self, fmt, *args):
        print(f"{self.command} {self.path}")

    def do_GET(self):
        if self.path.startswith("/api/status"):
            body = json.dumps(state).encode()
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
            return
        if self.path.startswith("/api/packages/download"):
            self.send_response(200)
            self.send_header("Content-Type", "application/octet-stream")
            self.end_headers()
            self.wfile.write(b"{}\n")
            return
        super().do_GET()

    def do_POST(self):
        length = int(self.headers.get("Content-Length") or 0)
        args = {k: v[0] for k, v in parse_qs(self.rfile.read(length).decode(errors="replace")).items()}
        path = self.path.split("?")[0]
        if path == "/api/audio/mute":
            state["muted"] = args.get("muted") in ("true", "1")
        elif path == "/api/audio/volume":
            state["volume"] = int(args.get("volume", state["volume"]))
        elif path == "/api/audio/play":
            pass  # The real device starts a sound here; nothing to model.
        elif path == "/api/display/brightness":
            state["brightness"] = int(args.get("brightness", state["brightness"]))
        elif path == "/api/style":
            state["style"] = int(args.get("style", state["style"]))
            state["styleName"] = state["styles"][state["style"]]
        elif path in ("/api/style/next", "/api/style/previous"):
            step = 1 if path.endswith("next") else -1
            state["style"] = (state["style"] + step) % state["styleCount"]
            state["styleName"] = state["styles"][state["style"]]
        elif path == "/api/eyes/enabled":
            state["enabled"][int(args["style"])] = args.get("enabled") in ("true", "1")
        elif path == "/api/cycle":
            state["cycle"] = args.get("enabled") in ("true", "1")
            state["interval"] = int(args.get("interval", state["interval"]))
            state["mode"] = "Cycle" if state["cycle"] else "Manual"
        self.send_response(204)
        self.end_headers()


print(f"Serving {WEB} with a fake device API on http://localhost:{PORT}")
ThreadingHTTPServer(("", PORT), Handler).serve_forever()
