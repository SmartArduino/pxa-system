#!/usr/bin/env python3
"""Measure RGB565 Canvas workloads against the streaming parser.

Build pxa_lvgl_canvas_test with PXA_LVGL_SOURCE_DIR, then pass --executable and
an ignored local/artifacts/<topic>/<timestamp> directory as --output. Each run
compares the indexed path and its allocation fallback in the same process,
including movement, fractional DPI, geometry changes and empty lists.
An optional pxa_lvgl_canvas_reference binary reports the compile-time baseline
separately. Process CPU timings cover warm submission and synchronous redraw.
"""
import argparse
import json
import os
from pathlib import Path
import statistics
import subprocess

WORKLOADS = ('partial-static', 'full-static', 'partial-moving', 'full-moving')
SCENES = ('shapes', 'lines', 'clipped-images', 'arcs')
DENSITIES = (65536, 98304)
CASES = [(workload, scene, density)
         for workload in range(len(WORKLOADS))
         for scene in range(len(SCENES)) for density in DENSITIES]


def read_measurements(output, reference):
    rows = [json.loads(line.removeprefix('CANVAS_BENCH '))
            for line in output.splitlines() if line.startswith('CANVAS_BENCH ')]
    selected = [row for row in rows if row['reference'] == reference]
    keys = {(row['workload'], row['scene'], row['density']) for row in selected}
    if keys != set(CASES) or len(selected) != len(CASES):
        raise RuntimeError('incomplete or repeated Canvas measurements')
    return selected


def summarize(runs):
    summaries = []
    for workload, scene, density in CASES:
        pairs = []
        for run in runs:
            pair = {row['reference']: row for row in run if
                    (row['workload'], row['scene'], row['density']) == (workload, scene, density)}
            pairs.append(pair)
        modes = {}
        for reference, label in ((True, 'reference'), (False, 'indexed')):
            samples = [pair[reference] for pair in pairs]
            modes[label] = {
                metric: statistics.median(row[metric] / row['frames'] for row in samples)
                for metric in ('submit_ns', 'draw_ns')}
        total = lambda row: (row['submit_ns'] + row['draw_ns']) / row['frames']
        gain = statistics.median(100 * (1 - total(pair[False]) / total(pair[True]))
                                 for pair in pairs)
        summaries.append({'workload': WORKLOADS[workload], 'scene': SCENES[scene],
                          'density_q16': density, **modes,
                          'total_time_reduction_percent': gain})
    return summaries


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--executable', type=Path, required=True)
    parser.add_argument('--reference-executable', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--repeat', type=int, default=3)
    args = parser.parse_args()
    if args.repeat < 1:
        parser.error('--repeat must be positive')
    args.output.mkdir(parents=True, exist_ok=True)
    runs, compiled_runs = [], []
    for index in range(args.repeat):
        modes = [('indexed', args.executable)]
        if args.reference_executable:
            modes.insert(0, ('reference', args.reference_executable))
        if index % 2:
            modes.reverse()
        outputs = {}
        for mode, executable in modes:
            result = subprocess.run([str(executable.resolve())], text=True,
                stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                env={**os.environ, 'PXA_CANVAS_BENCHMARK': '1'})
            (args.output / f'run-{index}-{mode}.log').write_text(result.stdout)
            result.check_returncode()
            outputs[mode] = result.stdout
        indexed = read_measurements(outputs['indexed'], False)
        runs.append(read_measurements(outputs['indexed'], True) + indexed)
        if 'reference' in outputs:
            compiled_runs.append(read_measurements(outputs['reference'], True) + indexed)
    summaries = summarize(runs)
    report = {'scope': 'native-host-cpu-rgb565', 'clock': 'process-cpu', 'byte_equal': True,
              'baseline': 'same-process-allocation-fallback', 'runs': runs, 'summary': summaries}
    if compiled_runs:
        report['compiled_reference'] = {'runs': compiled_runs, 'summary': summarize(compiled_runs)}
    (args.output / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(summaries, indent=2))


if __name__ == '__main__':
    main()
