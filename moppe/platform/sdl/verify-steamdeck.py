"""Reject runtime ABI/store leaks before publishing a portable tarball."""
import pathlib
import re
import subprocess
import sys

binary = pathlib.Path(sys.argv[1])

def readelf(*args):
    return subprocess.check_output(["readelf", *args, str(binary)], text=True)

program = readelf("-lW")
dynamic = readelf("-dW")
versions = readelf("--version-info", "-W")
interpreter = re.search(r"Requesting program interpreter: (.*?)\]", program)
assert interpreter and interpreter[1] == "/lib64/ld-linux-x86-64.so.2", program
needed = re.findall(r"\(NEEDED\).*?\[(.*?)\]", dynamic)
allowed = {"libc.so.6", "libm.so.6", "libpthread.so.0", "libdl.so.2", "librt.so.1", "ld-linux-x86-64.so.2"}
assert needed and set(needed) <= allowed, needed
assert not re.search(r"\((?:RPATH|RUNPATH)\)", dynamic), dynamic
glibc_names = re.findall(r"Name: (GLIBC_\S+)", versions)
assert all(re.fullmatch(r"GLIBC_[0-9.]+", name) for name in glibc_names), glibc_names
requirements = {tuple(map(int, v.split('.'))) for v in re.findall(r"GLIBC_([0-9.]+)", versions)}
assert requirements and max(requirements) <= (2, 31), requirements
assert b"/nix/store/" not in binary.read_bytes(), "embedded Nix store path"
print("Interpreter:", interpreter[1])
print("NEEDED:", ", ".join(needed))
print("Maximum GLIBC:", ".".join(map(str, max(requirements))))
print("No RPATH/RUNPATH or embedded /nix/store paths")
