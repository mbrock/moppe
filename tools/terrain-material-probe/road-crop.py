"""Compare the distant-road crop in two Moppe 1920x1080 capture sequences."""
import argparse
from pathlib import Path
import struct
import zlib

def crop(path):
    # Moppe's screenshot writer uses RGB8 and PNG filter zero.
    data = path.read_bytes()
    offset = 8
    compressed = []
    while offset < len(data):
        size, kind = struct.unpack_from('>I4s', data, offset)
        body = data[offset + 8:offset + 8 + size]
        if kind == b'IHDR' and struct.unpack_from('>IIBB', body) != (1920, 1080, 8, 2):
            raise ValueError(f'expected a 1920x1080 RGB8 screenshot at {path}')
        if kind == b'IDAT':
            compressed.append(body)
        offset += size + 12
    pixels = zlib.decompress(b''.join(compressed))
    stride = 1 + 1920 * 3
    if any(pixels[::stride]):
        raise ValueError(f'expected unfiltered rows at {path}')
    return b''.join(pixels[y*stride + 1 + 910*3:y*stride + 1 + 1010*3]
                    for y in range(555, 590))

def above_threshold(errors):
    return sum(max(abs(x) for x in errors[i:i+3]) > 8
               for i in range(0, len(errors), 3))

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('reference', type=Path)
    parser.add_argument('candidate', type=Path)
    args = parser.parse_args()
    previous = []
    changed = temporal = 0
    for i in range(160, 176):
        name = f'{i}.png'
        errors = [b-a for a,b in zip(crop(args.reference/name), crop(args.candidate/name))]
        changed += above_threshold(errors)
        if previous:
            temporal += above_threshold([a-b for a,b in zip(errors, previous)])
        previous = errors
    print(f'{changed/(16*3500):.3%} spatial, {temporal/(15*3500):.3%} adjacent-frame')
