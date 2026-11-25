"""Report time in ms for matching moving-road frames in independent launches."""
from pathlib import Path
from collections import defaultdict
from statistics import mean, median, quantiles
import sys

groups = defaultdict(list)
for path in sorted(Path(sys.argv[1]).glob('0.*-*-*.log')):
    scale, run, mode = path.stem.split('-')
    rows = {}
    for line in path.read_text().split('moppe: world ready')[-1].splitlines():
        if line.startswith('RIDE '):
            _, index, scene, bake, total = line.split()
            rows[int(index)] = (float(scene), float(bake), float(total))
    if not all(i in rows for i in range(97, 481)):
        continue
    selected = [rows[i] for i in range(97, 481)]
    p95 = lambda values: quantiles(values, n=100, method='inclusive')[94]
    summary = [mean(x) for x in zip(*selected)]
    summary.extend((p95([x[2] for x in selected]), max(x[2] for x in selected),
                    p95([x[1] for x in selected]), rows[1][1]))
    groups[scale, mode].append(summary)
    print(path.stem, ' '.join('%.3f' % v for v in summary))
print('Median of runs: scene, refresh, total, p95 total, maximum total, p95 refresh, first fill')
for key, runs in sorted(groups.items()):
    print(*key, len(runs), ' '.join('%.3f' % median(v) for v in zip(*runs)))
