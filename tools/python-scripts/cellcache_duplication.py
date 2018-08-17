# SPDX-License-Identifier: AGPL-3.0-or-later
from collections import defaultdict
import gdb

"""
very rough script to determine the amount of duplication present in
the kopano-server cellcache.

to use, load a kopano-server coredump into gdb, or attach to a running
server, and run:

source cellcache_duplication.py

this will provide a summary of the most redundant property data (count
plus data), and estimate the total size of memory used, with and without
full deduplication.

it may take some time to run for a large core file..

"""

# TODO why am I getting gdb memory errors for many string/binary props?

cachemgr = gdb.parse_and_eval('KC::g_lpSessionManager.m_lpECCacheManager')
vopo = gdb.lookup_type('void').pointer()

inferior = gdb.selected_inferior()

def std_map_nodes(map):
    nodetype = map['_M_t']['_M_impl'].type.fields()[0].type.template_argument(0).pointer()

    node = map['_M_t']['_M_impl']['_M_header']['_M_left']
    size = map['_M_t']['_M_impl']['_M_node_count']
    count = 0
    while count < size:
        result = node
        count += 1

        if node.dereference()['_M_right']:
            node = node.dereference()['_M_right']
            while node.dereference()['_M_left']:
                node = node.dereference()['_M_left']
        else:
            parent = node.dereference()['_M_parent']
            while node == parent.dereference()['_M_right']:
                node = parent
                parent = parent.dereference()['_M_parent']
            if node.dereference()['_M_right'] != parent:
                node = parent

        nodus = result.cast(nodetype).dereference()
        valtype = nodus.type.template_argument(0)
        yield nodus['_M_storage'].address.cast(valtype.pointer()).dereference()

def unordered_map_nodevals(map):
    global COUNT
    map = map['_M_h']
    node = map['_M_before_begin']['_M_nxt']
    node_type = gdb.lookup_type(map.type.strip_typedefs().name+'::'+'__node_type').pointer()
    while True:
        if node == 0:
            break
        plop = node.cast(node_type)
        elt = plop.dereference()
        valptr = elt['_M_storage'].address
        valptr = valptr.cast(elt.type.template_argument(0).pointer())
        yield plop, valptr.dereference()

        node = elt['_M_nxt']

map = cachemgr['m_CellCache']['m_map']

data_count = defaultdict(int)

objects = 0
props = 0
unknown = 0
skips = 0

for a, b in unordered_map_nodevals(map):
    objects += 1
    if objects % 10000 == 0:
        print 'objects:', objects

    objid = b['first']
    for x in std_map_nodes(b['second']['mapPropVals']):
        props += 1
        if props % 100000 == 0:
            print 'count:', props

        proptag = int(x['first'])
        value = x['second']['Value']
        type = int(proptag) & 0xffff

        if type == 0x1e: # PT_STRING8
            try:
                s = 'PT_STRING8:' + value['lpszA'].string()
                data_count[s] += 1
            except gdb.MemoryError: # XXX
                skips += 1

        elif type == 0x40: # PT_SYSTIME
            try:
                s = 'PT_SYSTIME:%d%d' % (int(value['hilo']['hi']), int(value['hilo']['lo']))
                data_count[s] += 1
            except gdb.MemoryError:
                skips += 1

        elif type == 0xb: # PT_BOOLEAN
            s = 'PT_BOOLEAN:%s' % value['b']
            data_count[s] += 1

        elif type == 0x102: # PT_BINARY
            try:
                addr = value['bin']['__ptr'].cast(vopo)
                s = 'PT_BINARY:' + bytes(inferior.read_memory(addr, int(value['bin']['__size']))).encode('hex').upper()
                data_count[s] += 1
            except gdb.MemoryError: # XXX
                skips += 1

        elif type == 0x3: # PT_LONG
            s = 'PT_LONG:%d' % value['ul']
            data_count[s] += 1

        else:
            unknown += 1
            skips += 1

print 'total objects:', objects
print 'total props:', props
print 'skips:', skips
print 'of which unknown', unknown

sorted_d = sorted(data_count, reverse=True, key=lambda x: data_count[x])

for d in sorted_d[:50]:
    print data_count[d], repr(d)

total_size_min = 0
total_size = 0
for d in sorted_d:
    data_size = len(d) - (d.find(':')+1)
    total_size += data_size * data_count[d]
    total_size_min += data_size

print 'total_size', total_size
print 'total_size_min', total_size_min
