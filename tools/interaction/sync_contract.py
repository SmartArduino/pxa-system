#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Sync reviewed interaction implementations into independently released consumers."""
import argparse
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--workspace', type=Path, required=True)
    parser.add_argument('--backend', type=Path)
    parser.add_argument('--manufacturer', type=Path)
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    system = Path(__file__).resolve().parents[2]
    workspace = args.workspace.resolve()
    pairs = [(system/'tools/package/interaction_contract.py', workspace/'tools/pxadb/interaction_contract.py')]
    if args.backend:
        pairs.append((system/'tools/package/interaction_contract.py', args.backend/'app/interaction_contract.py'))
    if args.manufacturer:
        vendor = args.manufacturer/'components/mz79_interaction'
        pairs.extend((workspace/source, vendor/destination) for source, destination in (
            ('firmware/components/pxa/include/pxa/pxa_host_interaction.h', 'include/pxa/pxa_host_interaction.h'),
            ('firmware/components/pxa/src/runtime/pxa_host_interaction.c', 'pxa_host_interaction.c'),
            ('firmware/components/pxadb/include/pxadb/pxadb_interaction_handler.inc', 'include/pxadb/pxadb_interaction_handler.inc')))
    mismatches = []
    for source, destination in pairs:
        data = source.read_bytes()
        if not destination.is_file() or destination.read_bytes() != data:
            if args.check:
                mismatches.append(str(destination))
            else:
                destination.parent.mkdir(parents=True, exist_ok=True)
                destination.write_bytes(data)
    if mismatches:
        parser.exit(1, 'Interaction vendor copies differ: '+', '.join(mismatches)+'\n')
    print('Interaction vendor copies verified' if args.check else 'Interaction vendor copies synchronized')


if __name__ == '__main__':
    main()
