# SPDX-License-Identifier: AGPL-3.0-or-later
import bisect
import collections
import gc
import gdb
import struct

import Gnuplot

import tcstats

"""
tool to visualize the memory usage of a kopano server, including
'lost' memory.

uses tcstats.py to know about all memory blocks (allocations) in use.

scans all 12 kopano caches to determine which of these blocks are
used for caching.

produces a graph which summarizes:

-used memory (outside of cache, not 'lost')
-cached memory (used by one of the 12 caches)
-lost memory (unreachably by any (internal) pointer)
-free memory (reserved by tcmalloc to serve new allocations)

this may take a long time and a _lot_ of memory.

TODO: 
- not all caches are scanned 100%, so some 'cached' memory currently
  ends up in 'used' (still need to scan objectdetails_t, 
  serverdetails_t and quotadetails_t)

pretty printing techniques taken from:

https://gcc.gnu.org/svn/gcc/trunk/libstdc++-v3/python/libstdcxx/v6/printers.py

"""

cachemgr = gdb.parse_and_eval('g_lpSessionManager.m_lpECCacheManager')
vopo = gdb.lookup_type('void').pointer()

def std_map_nodes(map):
    nodetype = map['_M_t']['_M_impl'].type.fields()[0].type.template_argument(0).pointer()

    node = map['_M_t']['_M_impl']['_M_header']['_M_left']
    size = map['_M_t']['_M_impl']['_M_node_count']
    count = 0
    while count < size: # and count < 10000:
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

def unordered_map_buckets_(map):
    table_ = map['_M_h']
    return table_['_M_buckets']

def unordered_map_nodevals(map):
    global COUNT
    map = map['_M_h']
    node = map['_M_before_begin']['_M_nxt']
    node_type = gdb.lookup_type(map.type.strip_typedefs().name+'::'+'__node_type').pointer()
     
    COUNT = 0
    COUNT2 = 0
    while True:
        if node == 0:
            break
        plop = node.cast(node_type)
        elt = plop.dereference()
        valptr = elt['_M_storage'].address
        valptr = valptr.cast(elt.type.template_argument(0).pointer())
        yield plop, valptr.dereference()

        COUNT2 += 1
        node = elt['_M_nxt']
        if COUNT2 % 10000 == 0:
            print('counts:', COUNT2, COUNT)
#            break

def _quota_blocks(map, blocks, starts, start_block):
    found_blocks = []

    buckets_ = unordered_map_buckets_(map)
    if int(str(buckets_.cast(vopo)), 16) == 0:
        return found_blocks

    fb = tcstats.bisect_block(int(str(buckets_), 16), starts, start_block)
    found_blocks.append(fb)

    for node, value in unordered_map_nodevals(map):
        fb = tcstats.bisect_block(int(str(node), 16), starts, start_block)
        found_blocks.append(fb)

    return found_blocks

def quota_blocks(*args):
    print()
    print('QUOTA CACHE')

    map = cachemgr['m_QuotaCache']['m_map']
    return _quota_blocks(map, *args)

def uquota_blocks(*args):
    print()
    print('QUOTAUSERDEFAULT CACHE')

    map = cachemgr['m_QuotaUserDefaultCache']['m_map']
    return _quota_blocks(map, *args)

def store_blocks(blocks, starts, start_block):
    found_blocks = []

    map = cachemgr['m_StoresCache']['m_map']

    buckets_ = unordered_map_buckets_(map)
    fb = tcstats.bisect_block(int(str(buckets_), 16), starts, start_block)
    found_blocks.append(fb)

    for node, value in unordered_map_nodevals(map):
        fb = tcstats.bisect_block(int(str(node), 16), starts, start_block)
        found_blocks.append(fb)

    return found_blocks

def index1_blocks(blocks, starts, start_block):
    print()
    print('INDEX1 CACHE')

    map = cachemgr['m_PropToObjectCache']['m_map']

    found_blocks = []

    buckets_ = unordered_map_buckets_(map)
    fb = tcstats.bisect_block(int(str(buckets_), 16), starts, start_block)
    found_blocks.append(fb)

    for node, value in unordered_map_nodevals(map):
        fb = tcstats.bisect_block(int(str(node), 16), starts, start_block)
        found_blocks.append(fb)

        addr = value['first']['lpData'].cast(vopo)
        fb = tcstats.bisect_block(int(str(addr), 16), starts, start_block)
        found_blocks.append(fb)

    return found_blocks

def index2_blocks(blocks, starts, start_block):
    print()
    print('INDEX2 CACHE')

    found_blocks = []

    map = cachemgr['m_ObjectToPropCache']['m_map']

    for node in std_map_nodes(map):
        fb = tcstats.bisect_block(int(str(node.address), 16), starts, start_block)
        found_blocks.append(fb)

        addr = int(str(node['second']['lpData'].cast(vopo)), 16)
        fb = tcstats.bisect_block(addr, starts, start_block)
        found_blocks.append(fb)

    return found_blocks

def obj_blocks(blocks, starts, start_block):
    print()
    print('OBJECTS CACHE')

    map = cachemgr['m_ObjectsCache']['m_map']

    found_blocks = []

    buckets_ = unordered_map_buckets_(map)
    fb = tcstats.bisect_block(int(str(buckets_), 16), starts, start_block)
    found_blocks.append(fb)

    for node, value in unordered_map_nodevals(map):
        fb = tcstats.bisect_block(int(str(node), 16), starts, start_block)
        found_blocks.append(fb)

    return found_blocks

COUNT = 0
def _cell_stdmap_blocks(map, starts, start_block):
    global COUNT
    found_blocks = []

    for node in std_map_nodes(map):
        addr = int(str(node.address), 16)
        fb = tcstats.bisect_block(addr, starts, start_block)
        found_blocks.append(fb)

        COUNT += 1
        union = int(node['second']['__union'])
        if union in (1,2,3,4,5):
            pass
        elif union in (6,7,8): # lpszA, hilo, bin
            addr = int(str(node['second']['Value']['bin']), 16)
            fb = tcstats.bisect_block(addr, starts, start_block)
            found_blocks.append(fb)
        else:
            print('FOUT', union)
            fout

    return found_blocks

def cell_blocks(blocks, starts, start_block):
    print()
    print('CELL CACHE')

    map = cachemgr['m_CellCache']['m_map']

    found_blocks = []

    buckets_ = unordered_map_buckets_(map)
    fb = tcstats.bisect_block(int(str(buckets_), 16), starts, start_block)
    found_blocks.append(fb)

    for node, value in unordered_map_nodevals(map):
        fb = tcstats.bisect_block(int(str(node), 16), starts, start_block)
        found_blocks.append(fb)
        found_blocks.extend(_cell_stdmap_blocks(value['second']['mapPropVals'], starts, start_block))

    return found_blocks

def userid_blocks(blocks, starts, start_block):
    print()
    print('USEROBJECT CACHE')

    map = cachemgr['m_UserObjectCache']['m_map']

    found_blocks = []

    buckets_ = unordered_map_buckets_(map)
    fb = tcstats.bisect_block(int(str(buckets_), 16), starts, start_block)
    found_blocks.append(fb)

    for node, value in unordered_map_nodevals(map):
        fb = tcstats.bisect_block(int(str(node), 16), starts, start_block)
        found_blocks.append(fb)

        for field in ('strExternId', 'strSignature'):
            addr = value['second'][field]['_M_dataplus']['_M_p'].cast(vopo)
            fb = tcstats.bisect_block(int(str(addr), 16), starts, start_block)
            found_blocks.append(fb)

    return found_blocks

def usereid_blocks(blocks, starts, start_block):
    print()
    print('USEREID BLOCKS')

    found_blocks = []

    map = cachemgr['m_UEIdObjectCache']['m_map']

    for node in std_map_nodes(map):
        fb = tcstats.bisect_block(int(str(node.address), 16), starts, start_block)
        found_blocks.append(fb)

        addr = int(str(node['first']['strExternId']['_M_dataplus']['_M_p'].cast(vopo)), 16)
        fb = tcstats.bisect_block(addr, starts, start_block)
        found_blocks.append(fb)

        addr = int(str(node['second']['strSignature']['_M_dataplus']['_M_p'].cast(vopo)), 16)
        fb = tcstats.bisect_block(addr, starts, start_block)
        found_blocks.append(fb)

    return found_blocks

def userdetail_blocks(blocks, starts, start_block):
    print()
    print('USERDETAIL CACHE')

    map = cachemgr['m_UserObjectDetailsCache']['m_map']

    found_blocks = []

    buckets_ = unordered_map_buckets_(map)
    fb = tcstats.bisect_block(int(str(buckets_), 16), starts, start_block)
    found_blocks.append(fb)

    for node, value in unordered_map_nodevals(map):
        fb = tcstats.bisect_block(int(str(node), 16), starts, start_block)
        found_blocks.append(fb)

    return found_blocks

def acl_blocks(blocks, starts, start_block):
    print()
    print('ACL CACHE')

    map = cachemgr['m_AclCache']['m_map']

    found_blocks = []

    buckets_ = unordered_map_buckets_(map)
    fb = tcstats.bisect_block(int(str(buckets_), 16), starts, start_block)
    found_blocks.append(fb)

    for node, value in unordered_map_nodevals(map):
        fb = tcstats.bisect_block(int(str(node), 16), starts, start_block)
        found_blocks.append(fb)

        addr = value['second']['aACL'].cast(vopo)
        fb = tcstats.bisect_block(int(str(addr), 16), starts, start_block)
        found_blocks.append(fb)

    return found_blocks


def server_blocks(blocks, starts, start_block):
    print()
    print('SERVERDETAIL CACHE')

    found_blocks = []

    map = cachemgr['m_ServerDetailsCache']['m_map']

    for node in std_map_nodes(map):
        fb = tcstats.bisect_block(int(str(node.address), 16), starts, start_block)
        found_blocks.append(fb)

        addr = int(str(node['first']['_M_dataplus']['_M_p'].cast(vopo)), 16)
        fb = tcstats.bisect_block(addr, starts, start_block)
        found_blocks.append(fb)

    return found_blocks

def prepare_data(name, blocks):
    print()
    print(name.upper()+' FREQ:')
#    tcstats.dump_blocks(blocks)
    summary, count, size = tcstats.freq_blocks(blocks)
    size = '%.2fGB' % (float(size) / (10**9))
    return Gnuplot.Data(sorted(summary.items()), with_="linespoints", title='%s blocks (%s)' % (name, size))

def main(blocks=None, used=None, free=None, starts=None, start_block=None):
    blocks = blocks or list(tcstats.pagemap_blocks())
    used = used or list(tcstats.used_blocks(blocks))
    free = free or list(tcstats.free_blocks(blocks))
    blocks = None # don't keep in memory
    starts = starts or sorted(b.address for b in used)
    start_block = start_block or {b.address: b for b in used}

    print('calc caches')
    args = (used, starts, start_block)
    c1 = cell_blocks(*args)
    c2 = obj_blocks(*args)
    c3 = index1_blocks(*args)
    c4 = index2_blocks(*args)
    c5 = store_blocks(*args)
    c6 = quota_blocks(*args)
    c7 = uquota_blocks(*args)
    c8 = userid_blocks(*args)
    c9 = usereid_blocks(*args)
    c10 = userdetail_blocks(*args)
    c11 = acl_blocks(*args)
    c12 = server_blocks(*args)
    caches = (c1, c2, c3, c4, c5, c6, c7, c8, c9, c10, c11, c12)
    for i, c in enumerate(caches):
        print('cache size', i, len(c), sum(b.size for b in c))

    print('calc unreached')
    lost = tcstats.unreachable_blocks(used)
    used = tcstats.subtract_blocks(used, free)

    print('prep data')
    for i, c in enumerate(caches):
        used = tcstats.subtract_blocks(used, c)

    d1 = prepare_data('used', used)
    d2 = prepare_data('cached', sum(caches, [])) 
    d3 = prepare_data('free', free)
    d4 = prepare_data('lost', lost)

    g = Gnuplot.Gnuplot()
    g('set logscale x')
    g('set logscale y')
    g.plot(d1, d2, d3, d4)
    g.hardcopy(filename='kopano_cache.png', terminal='png')

if __name__ == '__main__':
    main()
