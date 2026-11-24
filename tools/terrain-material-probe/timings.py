"""Summarize complete crossover cycles from the M5 material experiment."""
import sys
from collections import defaultdict
from statistics import mean
from pathlib import Path

for log in map(Path, sys.argv[1:]):
    rows = defaultdict(lambda: defaultdict(list))
    for line in log.read_text().splitlines():
        if line.startswith('MATERIAL '):
            _, cycle, mode, scene, total = line.split()
            rows[int(cycle)][int(mode)].append((float(scene), float(total)))
    cycles = [row for row in rows.values()
              if all(len(row[i]) == 32 for i in range(6))]
    print(log.name, len(cycles), 'complete cycles')
    if not cycles:
        continue
    for i, label in enumerate(('original', 'base colour', 'surface', 'no roads',
                               'flat', 'repeat original')):
        means = []
        for row in cycles:
            # The Metal backend can report the last resolved frame twice.
            unique = list(dict.fromkeys(row[i]))
            means.append([mean(x[j] for x in unique) for j in (0, 1)])
        print(f'{label:16s}', *(round(mean(x[j] for x in means), 3)
                                for j in (0, 1)))
