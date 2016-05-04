#! /usr/bin/python

import math
import sys

agg = dict()

for line in sys.stdin:
	parts = line.rstrip().split(' ')

	if len(parts) == 2:
		record = None

		if parts[1] in agg:
			record = agg[parts[1]]

		else:
			record = [ 0, 0, 0 ]

		record[0] += 1
		i_ms = int(parts[0])
		record[1] += i_ms
		record[2] += i_ms * i_ms

		agg[parts[1]] = record

for key in agg.keys():
	record = agg[key]

	cnt = record[0]
	avg = float(record[1]) / cnt
	sd = math.sqrt((float(record[2]) / cnt) - avg * avg)

	print '%s\t%d\t%.2f\t%.2f' % (key, cnt, avg, sd)
