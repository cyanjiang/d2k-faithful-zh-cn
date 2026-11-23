"""Queries a running GhidraMCP headless server (github.com/bethington/ghidra-mcp)
over its local HTTP API. Much faster than re/query.sh (no Ghidra start-up per
query) and free of the project lock, because the server works on its own copy
of the project.

    python re/ghidra_http.py decompile 0x0042e0b0
    python re/ghidra_http.py xrefs 0x005162b0
    python re/ghidra_http.py get "/get_xrefs_from?address=0x00448550"

The server listens on 127.0.0.1 only and needs a bearer token. The token file
lives outside the repo: D2K_GHIDRA_TOKEN_FILE, default
~/tools/ghidra-mcp/token.txt. Setup notes: docs/reference-setup.md.
"""
import json
import os
import sys
import urllib.parse
import urllib.request
from pathlib import Path

URL = os.environ.get("D2K_GHIDRA_URL", "http://127.0.0.1:8089")
TOKEN_FILE = Path(os.environ.get("D2K_GHIDRA_TOKEN_FILE", Path.home() / "tools" / "ghidra-mcp" / "token.txt"))


def get(path: str) -> dict:
    token = TOKEN_FILE.read_text().strip()
    req = urllib.request.Request(URL + path, headers={"Authorization": "Bearer " + token})
    with urllib.request.urlopen(req, timeout=300) as r:
        return json.loads(r.read().decode())


def main() -> None:
    if len(sys.argv) < 3:
        print(__doc__)
        sys.exit(1)
    cmd, arg = sys.argv[1], sys.argv[2]
    if cmd == "decompile":
        out = get("/force_decompile?function=" + urllib.parse.quote(arg))
        print(out.get("decompiled", out))
    elif cmd == "xrefs":
        for r in get("/get_xrefs_to?limit=500&address=" + urllib.parse.quote(arg)).get("references", []):
            print(r.get("type"), r.get("from_address"), r.get("from_function"))
    elif cmd == "get":
        print(json.dumps(get(arg), indent=1))
    else:
        print(__doc__)
        sys.exit(1)


if __name__ == "__main__":
    main()
