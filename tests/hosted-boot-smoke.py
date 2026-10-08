#!/usr/bin/env python3
"""Hosted boot markers and persistent count; no hardware reset evidence."""
import argparse
from pathlib import Path
from firmware_fixture import NativeNode

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--executable', required=True)
parser.add_argument('--output', type=Path, required=True)
parser.add_argument('--restart', action='store_true')
args = parser.parse_args()
args.output.mkdir(parents=True, exist_ok=False)
flash = args.output / 'flash.bin'
with NativeNode(args.executable, args.output / 'first', flash) as node:
    text = node.log.read_text()
    assert text.index('@BOOT ') < text.index('@READY ')
    offset = node.send('param table 32')
    node.wait('boot_revisions', offset)
    text = node.log.read_text()[offset:]
    for name in ('boot_image', 'boot_count', 'boot_reset_cause', 'boot_revisions'):
        assert name in text, text
    print('markers ordered; table 32 readable')
    if args.restart:
        count = node.parameter('boot_count')
        node.command('param save', 'Parameter snapshot save: PASS')
if args.restart:
    with NativeNode(args.executable, args.output / 'second', flash) as node:
        after = node.parameter('boot_count')
        assert after > count, (count, after)
        print(f'boot_count advanced: {count} -> {after}')
print('HOSTED BOOT RESULT: PASS')
