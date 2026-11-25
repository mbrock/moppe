"""Average the timestamped passes over exact route frames 97 through 480."""
import sys
from collections import defaultdict
from pathlib import Path

for name in sys.argv[1:]:
    totals = defaultdict(float)
    frames = set()
    for line in Path(name).read_text().split('moppe: world ready')[-1].splitlines():
        if not line.startswith('PASS '):
            continue
        _, index, time, label = line.split(maxsplit=3)
        if not 97 <= int(index) <= 480:
            continue
        if label.startswith('route-'):
            label = 'main scene'
            frames.add(int(index))
        elif label.startswith('material bake '):
            label = 'refresh'
        totals[label] += float(time)
    if len(frames) != 384:
        raise ValueError(f'{name} has {len(frames)} of 384 route frames')
    print(name)
    for label, time in sorted(totals.items(), key=lambda item: -item[1]):
        print(f'  {label:36} {time / len(frames):.3f} ms')
