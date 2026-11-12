#!/usr/bin/env python3
"""Summarize saved RESULT lines; also require completed pixel smoke checks."""
import csv
import re
import sys

writer = csv.writer(sys.stdout)
writer.writerow(['log', 'case', 'baseline_median_ms', 'baseline_p95_ms', 'E_median_ms', 'E_p95_ms',
                 'A_median_ms', 'A_p95_ms', 'E_delta_median_ms', 'E_delta_p95_ms',
                 'A_delta_median_ms', 'A_delta_p95_ms', 'E_delta_median_percent', 'A_delta_median_percent'])
for path in sys.argv[1:]:
    text = open(path).read()
    assert 'COMPLETE gl_error=0' in text, f'{path}: incomplete run'
    assert len(re.findall(r'^SMOKE .*chain_1_to_6=PASS pixels=PASS$', text, re.M)) == 4, f'{path}: missing smoke checks'
    results = {}
    for case, mode, med, tail in re.findall(r'^RESULT case=(\S+) mode=(\S+) n=\d+ median_ms=(\S+) p95_ms=(\S+)$', text, re.M):
        results.setdefault(case, {})[mode] = (float(med), float(tail))
    assert len(results) == 5, f'{path}: missing cases'
    for case, modes in results.items():
        baseline, publication, reflection = (modes[m] for m in ['baseline', 'E', 'A'])
        delta = [publication[0]-baseline[0], publication[1]-baseline[1], reflection[0]-baseline[0], reflection[1]-baseline[1]]
        writer.writerow([path, case, *[f'{v:.6f}' for pair in [baseline, publication, reflection] for v in pair],
                         *[f'{v:.6f}' for v in delta], f'{100*delta[0]/baseline[0]:.1f}', f'{100*delta[2]/baseline[0]:.1f}'])
