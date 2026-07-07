"""Build data/pico.u16 for the game's --pico mode.

Downloads the Copernicus GLO-30 DEM tile covering Pico Island
(Azores) from the AWS open data bucket, crops a 49.4 km square
around the island, resamples to 2049x2049, and writes raw
little-endian uint16 decimeters.

Tile: https://copernicus-dem-30m.s3.amazonaws.com/Copernicus_DSM_COG_10_N38_00_W029_00_DEM/Copernicus_DSM_COG_10_N38_00_W029_00_DEM.tif
Deps: pip install numpy tifffile imagecodecs
"""
import struct
import zlib

import numpy as np
import tifffile

# Copernicus GLO-30 tile N38 W029: lat 39..38 (rows), lon -29..-28 (cols)
tile = tifffile.imread("pico_tile.tif").astype(np.float32)

# ~49.4 km square centered on Pico Island
CENTER_LAT, CENTER_LON = 38.48, -28.2835
HALF_M = 24700.0
M_PER_DEG_LAT = 111000.0
M_PER_DEG_LON = 111320.0 * np.cos(np.radians(CENTER_LAT))

dlat = HALF_M / M_PER_DEG_LAT
dlon = HALF_M / M_PER_DEG_LON

lat0, lat1 = CENTER_LAT + dlat, CENTER_LAT - dlat  # north -> south
lon0, lon1 = CENTER_LON - dlon, CENTER_LON + dlon  # west -> east
print(f"region: lat {lat1:.4f}..{lat0:.4f}  lon {lon0:.4f}..{lon1:.4f}")
print(f"extent: {2 * HALF_M / 1000:.1f} km square")

N = 2049
rows = (39.0 - np.linspace(lat0, lat1, N)) * 3600.0
cols = (np.linspace(lon0, lon1, N) + 29.0) * 3600.0
rows = np.clip(rows, 0, 3599 - 1e-6)
cols = np.clip(cols, 0, 3599 - 1e-6)

r0 = np.floor(rows).astype(int)
c0 = np.floor(cols).astype(int)
rf = (rows - r0)[:, None]
cf = (cols - c0)[None, :]

g = (tile[np.ix_(r0, c0)] * (1 - rf) * (1 - cf)
     + tile[np.ix_(r0, c0 + 1)] * (1 - rf) * cf
     + tile[np.ix_(r0 + 1, c0)] * rf * (1 - cf)
     + tile[np.ix_(r0 + 1, c0 + 1)] * rf * cf)

g = np.clip(g, 0.0, None)
print(f"resampled: {g.shape}, max {g.max():.1f} m, "
      f"land fraction {(g > 1.0).mean():.3f}")

# 16-bit decimeters, little endian, row-major from north
u16 = np.round(g * 10.0).astype("<u2")
u16.tofile("pico.u16")
print(f"wrote pico.u16 ({u16.nbytes / 1e6:.1f} MB)")


def write_png(path, arr8):
    h, w = arr8.shape
    raw = b"".join(b"\x00" + arr8[i].tobytes() for i in range(h))

    def chunk(tag, data):
        c = tag + data
        return (struct.pack(">I", len(data)) + c
                + struct.pack(">I", zlib.crc32(c) & 0xFFFFFFFF))

    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 0, 0, 0, 0)))
        f.write(chunk(b"IDAT", zlib.compress(raw, 6)))
        f.write(chunk(b"IEND", b""))


preview = (g[::4, ::4] / g.max() * 255).astype(np.uint8)
write_png("pico_preview.png", preview)
print("wrote pico_preview.png")
