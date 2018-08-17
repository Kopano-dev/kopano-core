# SPDX-License-Identifier: AGPL-3.0-only
import re


multipliers = {
    'b': 1,
    'kb': 1000,
    'mb': 1000*1000,
    'gb': 1000*1000*1000,
    'tb': 1000*1000*1000*1000,
    'pb': 1000*1000*1000*1000*1000,
    'eb': 1000*1000*1000*1000*1000*1000,
    'yb': 1000*1000*1000*1000*1000*1000*1000,
    'kib': 1024,
    'mib': 1024*1024,
    'gib': 1024*1024*1024,
    'tib': 1024*1024*1024*1024,
    'pib': 1024*1024*1024*1024*1024,
    'eib': 1024*1024*1024*1024*1024*1024,
    'yib': 1024*1024*1024*1024*1024*1024*1024,
    }

def parseUnitByte(s):
    secs = 0.0
    items = re.split(r'\s', s)
    for item in items:
        match = re.match('^(?P<amount>[0-9]+)(?P<unit>.*)$', item)
        if not match:
            continue  # ignore
        unit = match.group('unit').lower()
        if unit not in multipliers:
            continue  # ignore
        multiplier = multipliers[unit]
        secs += float(match.group('amount')) * multiplier

    return secs


testcases = [
    ( '10b', 10),
    ('1kb', 1000),
    ('1kb 1b', 1001),
    ('1tb 2gb 3mb 4kb 5b', (1000*1000*1000*1000) + (1000*1000*1000*2) + (1000*1000*3) + (1000*4) + 5),
    ('1TB 2GB 3MB 4KB 5B', (1000*1000*1000*1000) + (1000*1000*1000*2) + (1000*1000*3) + (1000*4) + 5),
    ('', 0),
    (' ', 0),
    ('2yib', 2361183241434822606848),
    ('2kib 4mib', (2*1024) + (4 * 1024*1024) ),
    ('    1mb        3b  ', (1000*1000) + 3)
    ]


if __name__ == '__main__':
    for input, output in testcases:
        real = parse(input)
        if real != output:
            print "%r gives %r, not %r" % (input, real, output)
        print real
    print 'done'
