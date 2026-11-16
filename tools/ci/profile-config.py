#!/usr/bin/env python3
"""Check resolved Kconfig selections for a reference image."""

import argparse
import json
from pathlib import Path
import re
import sys

SYMBOL = r'CONFIG_[A-Za-z0-9_]+'


def unique_keys(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError(f'duplicate JSON key: {key}')
        result[key] = value
    return result


def valid_value(value):
    if not isinstance(value, str):
        return False
    if re.fullmatch(r'y|m|n|-?(?:0|[1-9][0-9]*)|0x[0-9a-fA-F]+', value):
        return True
    try:
        return value.startswith('"') and isinstance(json.loads(value), str)
    except ValueError:
        return False


def settings(values):
    if not isinstance(values, dict) or not values:
        raise ValueError('requirements settings must be a nonempty object')
    for name, value in values.items():
        if not re.fullmatch(SYMBOL, name) or not valid_value(value):
            raise ValueError(f'invalid requirement: {name}={value!r}')
    return values


def requirements(path, profile):
    with path.open() as source:
        spec = json.load(source, object_pairs_hook=unique_keys)
    if (not isinstance(spec, dict) or set(spec) != {'version', 'common', 'profiles'}
            or type(spec['version']) is not int or spec['version'] != 1
            or not isinstance(spec['profiles'], dict) or not spec['profiles']):
        raise ValueError('requirements need version 1, common settings and profiles')
    common = settings(spec['common'])
    for name, values in spec['profiles'].items():
        if not re.fullmatch(r'[a-z0-9]+(?:-[a-z0-9]+)*', name):
            raise ValueError(f'invalid requirements profile: {name}')
        settings(values)
        repeated = common.keys() & values.keys()
        if repeated:
            raise ValueError(f'repeated requirement: {sorted(repeated)[0]}')
    if profile not in spec['profiles']:
        raise ValueError(f'unknown profile: {profile}')
    return common | spec['profiles'][profile]


def configuration(path):
    result = {}
    for number, line in enumerate(path.read_text().splitlines(), 1):
        match = re.fullmatch(rf'({SYMBOL})=(.*)', line)
        if match is not None and valid_value(match[2]):
            name, value = match.groups()
        else:
            match = re.fullmatch(rf'# ({SYMBOL}) is not set', line)
            if match is not None:
                name, value = match[1], 'n'
            elif not line.strip() or (line.startswith('#') and not line.startswith('# CONFIG_')):
                continue
            else:
                raise ValueError(f'invalid configuration line {number}: {line}')
        if name in result:
            raise ValueError(f'duplicate configuration symbol: {name}')
        result[name] = value
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('config', type=Path, help='resolved image .config')
    parser.add_argument('--profile', required=True, help='exact reference profile name')
    parser.add_argument('--requirements', type=Path, default=(
        Path(__file__).resolve().parents[2] / 'tests/requirements/profiles/reference.json'))
    args = parser.parse_args()
    try:
        expected = requirements(args.requirements, args.profile)
        actual = configuration(args.config)
        mismatches = [f'{name}: expected {value}, got {actual.get(name, "n")}'
                      for name, value in expected.items() if actual.get(name, 'n') != value]
        if mismatches:
            raise ValueError('\n'.join(mismatches))
    except (OSError, UnicodeError, ValueError) as error:
        print(f'PROFILE CONFIG: FAIL {error}', file=sys.stderr)
        return 1
    print(f'PROFILE CONFIG {args.profile}: PASS ({len(expected)} checks)')
    return 0


if __name__ == '__main__':
    sys.exit(main())
