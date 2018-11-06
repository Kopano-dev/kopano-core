# SPDX-License-Identifier: AGPL-3.0-only
import re


multipliers = {
    's': 1,
    'm': 60,
    'h': 60*60,
    'd': 24*60*60,
    'w': 7*24*60*60,
    'y': 365*24*60*60
    }

def parse(s):
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
    ( '10s', 10),
    ('1m 1s', 61),
    ('20m 20s', 1220),
    ('1w 3d 2h 4m 5s', 10*24*60*60+7200+240+5),
    ('1W 3D 2H 4M 5S', 10*24*60*60+7200+240+5),
    ('', 0),
    (' ', 0),
    ('    1m        3s  ', 63)
    ]


if __name__ == '__main__':
    for input, output in testcases:
        real = parse(input)
        if real != output:
            print("%r gives %r, not %r" % (input, real, output))
    print('done')
