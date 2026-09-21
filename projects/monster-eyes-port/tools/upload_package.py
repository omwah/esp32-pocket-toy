#!/usr/bin/env python3
"""Push one eye package to a running device over its local-network API.

    python tools/upload_package.py 192.168.1.42 goat

The device stages the files, validates the whole package, and only then
renames it into place, so a failure part way through leaves the package that
was there before. Files are sent one at a time because the web server is
polled from the render loop and has one request in flight at a time.
"""

import argparse
import http.client
import mimetypes
import pathlib
import sys
import time
import uuid

HERE = pathlib.Path(__file__).resolve().parent
PACKAGES = HERE.parent / "data" / "eyes"


# The device's web server is polled from the render loop, so it drains a
# socket in bursts between frames. Handing it a whole texture in one write
# resets the connection (measured: a 172 KB iris died every time through
# urllib, and went through in 4 s when curl paced it), so the body goes out in
# small pieces with the socket left to breathe between them.
CHUNK = 4096


def post(host, path, fields=None, file=None):
    """Form POST, multipart when a file is attached."""
    if file is None:
        body = "&".join(f"{k}={v}" for k, v in (fields or {}).items()).encode()
        content_type = "application/x-www-form-urlencoded"
    else:
        boundary = uuid.uuid4().hex
        kind = mimetypes.guess_type(file.name)[0] or "application/octet-stream"
        body = (f"--{boundary}\r\n"
                f'Content-Disposition: form-data; name="file"; '
                f'filename="{file.name}"\r\n'
                f"Content-Type: {kind}\r\n\r\n").encode()
        body += file.read_bytes() + f"\r\n--{boundary}--\r\n".encode()
        content_type = f"multipart/form-data; boundary={boundary}"

    connection = http.client.HTTPConnection(host, timeout=60)
    try:
        connection.putrequest("POST", path)
        connection.putheader("Content-Type", content_type)
        connection.putheader("Content-Length", str(len(body)))
        connection.endheaders()
        for start in range(0, len(body), CHUNK):
            connection.send(body[start:start + CHUNK])
            time.sleep(0.002)
        response = connection.getresponse()
        return response.status, response.read().decode(errors="replace")
    finally:
        connection.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("host", help="Device address, e.g. 192.168.1.42")
    parser.add_argument("package", help="Package directory under data/eyes")
    args = parser.parse_args()

    source = PACKAGES / args.package
    files = sorted(p for p in source.iterdir() if p.is_file())
    if not files:
        sys.exit(f"no files in {source}")

    status, body = post(args.host, "/api/packages/upload/start",
                        {"id": args.package})
    if status != 200:
        sys.exit(f"start failed: {body}")
    token = body.split('"')[3]

    for path in files:
        status, body = post(args.host,
                            f"/api/packages/upload/file?token={token}",
                            file=path)
        print(f"{path.name}: {path.stat().st_size} bytes -> {status}")
        if status >= 400:
            sys.exit(f"upload failed: {body}")

    status, body = post(args.host, "/api/packages/upload/commit",
                        {"token": token})
    if status >= 400:
        sys.exit(f"commit failed: {body}")
    print(f"published {args.package}")


if __name__ == "__main__":
    main()
