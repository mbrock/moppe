#!/usr/bin/env python3
"""Check the stapled Moppe ZIP after moving it away from the build tree."""
from pathlib import Path
import subprocess
import sys
import tempfile

with tempfile.TemporaryDirectory(prefix="moppe moved download ") as directory:
    root = Path(directory)
    subprocess.run(["ditto", "-x", "-k", str(Path(sys.argv[1]).resolve()), str(root)], check=True)
    app = root / "moppe.app"
    for command in (["codesign", "--verify", "--deep", "--strict"],
                    ["xcrun", "stapler", "validate"],
                    ["spctl", "--assess", "--type", "execute"]):
        subprocess.run([*command, str(app)], check=True)
    command = [str(app / "Contents/MacOS/moppe")]
    result = subprocess.run([*command, "--help"], capture_output=True, text=True, timeout=60)
    assert result.returncode == 0 and "Usage" in result.stdout, result.stderr
    result = subprocess.run([*command, "--terrain-resolution", "invalid"], capture_output=True, timeout=60)
    assert result.returncode != 0
    print("PASS: moved ZIP, signatures, stapled ticket, Gatekeeper, help, and error exit")
