# SPDX-License-Identifier: AGPL-3.0-or-later
import bisect
import collections
import gc
import gdb
import struct

"""
tool to parse/inspect tcmalloc internals, and detect 'lost' memory,
meaning memory which isn't reachable via any (internal) pointers.

http://goog-perftools.sourceforge.net/doc/tcmalloc.html

assumes a 64-bit system, and a recent version of GDB to avoid memleaks
in this tool itself.. still it may use a _lot_ of memory.

"""

# tcmalloc

pageheap_ = gdb.parse_and_eval('tcmalloc::Static::pageheap_')
central_cache_ = gdb.parse_and_eval('tcmalloc::Static::central_cache_')
thread_heaps_ = gdb.parse_and_eval('tcmalloc::ThreadCache::thread_heaps_')
sizemap_ = gdb.parse_and_eval('tcmalloc::Static::sizemap_') # XXX cache

spantype = gdb.lookup_type('tcmalloc::Span').pointer()

knumclasses = int(gdb.parse_and_eval('kNumClasses')) # XXX skip 0?
kmaxpages = int(gdb.parse_and_eval('kMaxPages'))
pagesize = 1 << int(gdb.parse_and_eval('kPageShift'))

inferior = gdb.selected_inferior()

LOCATION = {
    0 : 'IN_USE',
    1 : 'NORMAL',
    2 : 'RETURNED',
}

# tcmalloc datastructures

class Span(object):
    """ multiple pages combined into a single 'span' """

    def __init__(self, val):
        self.val = val
        self.start = int(val['start']) * pagesize
        self.length = int(val['length']) * pagesize
        self.next_ = val['next']
        self.location = LOCATION[int(val['location'])]
        self.sizeclass = int(val['sizeclass'])
        self.size = int(sizemap_['class_to_size_'][self.sizeclass])
        self.objects_ = val['objects']

    @property
    def objects(self):
        for object in ObjectList(self.objects_):
            yield object

    @property
    def ll_next(self):
        return Span(self.val['next'])

    def __repr__(self):
        return 'Span(%x-%x)' % (self.start, self.start+self.length)

class SpanList(object):
    """ list of spans """

    def __init__(self, val):
        self.val = val

    def __iter__(self):
        span = Span(self.val)
        while span:
            if span.next_ == self.val.address:
                break
            span = span.ll_next
            yield span

class Object(object):
    """ 'free' object """

    def __init__(self, addr):
        self.addr = addr
        try:
            self.next_ = int(str(gdb.parse_and_eval('*(void **)'+hex(addr))), 16)
        except gdb.MemoryError: # XXX span type NORMAL, check elsewhere?
            print('MEMORY ERROR!!')
            self.next_ = 0

    @property
    def ll_next(self):
        if self.next_ != 0:
            return Object(self.next_)

    def __repr__(self):
        return 'Object(%x)' % self.addr

class ObjectList(object):
    """ linked-list of 'free' objects """

    def __init__(self, val):
        self.val = val

    def __iter__(self):
        addr = int(str(self.val), 16)
        if addr != 0:
            object = Object(addr)
            while object:
                yield object
                object = object.ll_next

# non-tcmalloc

class Block(object):
    __slots__ = ['address', 'size', 'used']

    """ unit of memory allocated by program """

    def __init__(self, address, size, used):
        self.address = address
        self.size = size
        self.used = used

    def __repr__(self):
        return 'Block(%x-%x)' % (self.address, self.address+self.size)

# tc-malloc tools

def dump_pageheap():
    """ summarize page cache """

    print('')
    print('PAGE HEAP')

    for kind in ('normal', 'returned'):
        print('(%s)' % kind)

        print('pageheap_.large_:')
        val = pageheap_['large_'][kind]
        for span in SpanList(val):
            print(span, span.length)

        print('pageheap_.free_:')
        for x in range(kmaxpages):
            val = pageheap_['free_'][x][kind]
            for span in SpanList(val):
                print('[%d/%d]' % (x, kmaxpages), span, span.length, span.location)

    print('')
    print('PAGE MAP RADIX')

def pagemap_spans():
    """ parse 3-level radix tree containing 'spans', covering all memory used by tcmalloc """

    prev_end = None
    root = pageheap_['pagemap_']['root_']

    for a in range(2**12):
        node = root['ptrs'][a]
        if int(str(node), 16) != 0:
            for b in range(2**12):
                node2 = node['ptrs'][b]
                if int(str(node2), 16) != 0:
                    for c in range(2**11):
                        node3 = node2['ptrs'][c]
                        if int(str(node3), 16) != 0:
                            pagestart = ((a<<23)+(b<<11)+c)*pagesize
                            if prev_end is None or pagestart >= prev_end:
                                span = Span(node3.cast(spantype))
                                prev_end = pagestart + span.length
                                yield span

def dump_pagemap():
    """ summarizes above page map """

    size = 0
    for span in pagemap_spans():
        size += span.length
        print(hex(span.start), span.length, span.size, span.location, '(%d objects)' % len(list(span.objects)), size)

def dump_central_cache():
    """ summarizes central free cache """

    print('')
    print('CENTRAL CACHE')

    for kind in ('empty_', 'nonempty_'):
        print('(%s)' % kind)

        for x in range(knumclasses):
            val = central_cache_[x][kind]
            for span in SpanList(val):
                print('[%d(%d/%d)]' % (span.size, x, knumclasses), span, span.length, span.location, '(%d objects)' % len(list(span.objects)))

def dump_thread_caches():
    """ summarizes thread free caches """

    print('')
    print('THREAD CACHES')
    print('thread heaps:', gdb.parse_and_eval("'tcmalloc::ThreadCache::thread_heap_count_'"))
    heap = thread_heaps_
    while heap:
        if int(str(heap), 16) == 0:
            break

        print('')
        print('[thread %s]' % hex(int(heap['tid_'])))

        for x in range(knumclasses):
            val = heap['list_'][x]
            objects = list(ObjectList(val['list_']))
            if objects:
                size = int(sizemap_['class_to_size_'][x])
                print('[%d(%d/%d)] (%d objects)' % (size, x, knumclasses, len(objects)))
        print('')

        heap = heap['next_']

def thread_cache_objaddrs():
    """ addresses of free objects in thread cache, used in pagemap_blocks """

    result = set()
    heap = thread_heaps_
    count = 0
    while heap:
        if int(str(heap), 16) == 0:
            break
        for x in range(knumclasses):
            val = heap['list_'][x]
            objects = list(ObjectList(val['list_']))
            for object_ in objects:
                result.add(object_.addr)
                count += 1
        heap = heap['next_']
    return result

def pagemap_blocks():
    """ determines memory 'blocks', allocated by the program """

    thread_objs = thread_cache_objaddrs() # free objects are in central cache _or_ thread cache

    for span in pagemap_spans():
        if span.location == 'IN_USE':
            if int(span.sizeclass) != 0:
                chunks = span.length // span.size
                starts = set([span.start+x*span.size for x in range(chunks)])
                for obj in span.objects:
                    starts.remove(obj.addr)
                starts -= thread_objs
                for start in starts:
                    yield Block(start, span.size, True)
                for obj in span.objects:
                    yield Block(obj.addr, span.size, False)
            else:
                yield Block(span.start, span.length, True)
        elif span.location == 'NORMAL':
            yield Block(span.start, span.length, False)

# non-tc-malloc tools

def data_stacks():
    """ yield all stack data, as it may contain pointers """

    for thread in inferior.threads():
        thread.switch()
        frame = gdb.newest_frame()
        frame.select()
        stack_pointer = int(str(gdb.parse_and_eval('$sp')), 16)
        while frame:
            wa = str(frame) # XXX
            base_pointer = int(wa[wa.find('=')+1:wa.find(',')], 16)
            frame = frame.older()
        data = bytes(inferior.read_memory(stack_pointer, base_pointer-stack_pointer))
        yield data

def data_segments(): # XXX ugly, overkill?
    """ yield all global segments, as they may contain pointers """

    part = None
    s = gdb.execute('info files', to_string=True)
    for line in s.splitlines():
        line = line.strip()
        if line.startswith('Local core'):
            part = 'core'
        elif line.startswith('Local exec'):
            part = 'exec'
        elif part == 'exec' and line.startswith('0x'):
            parts = line.split()
            if True: #parts[4] in ('.data', '.bss') and not 'tcmalloc' in parts[-1]:
                start= int(parts[0], 16)
                size = int(parts[2], 16) - start
                data = bytes(inferior.read_memory(start, size))
                yield data

def bisect_block(address, starts, start_block):
    """ return block to which address belongs or None """

    b = bisect.bisect_right(starts, address)
    if b > 0:
        block = start_block[starts[b-1]]
        if block.address <= address < block.address+block.size:
            return block

def search_reachable(reachable, starts, start_block):
    """ return addresses of blocks reachable from initial set of addresses """

    front = reachable.copy()
    vopo = gdb.lookup_type('void').pointer()

    scanned = 0
    while front:
        new = set()
        for addr in front:
            block = start_block[addr]
            data = inferior.read_memory(block.address, block.size)
            for x in range(block.size-8):
                addr = struct.unpack_from('Q', data, x)[0]
                block2 = bisect_block(addr, starts, start_block)
                if block2 and block2.address not in reachable:
                    new.add(block2.address)
                    reachable.add(block2.address)
            scanned += block.size
#            print('scanned', scanned)
        front = new

    return reachable

def reachable_blocks(blocks):
    """ follow pointer chain from stack and global data to determine unreachable blocks """

    starts = sorted(b.address for b in blocks)
    start_block = {b.address: b for b in blocks}

    data = []
    data.extend(data_stacks())
    data.extend(data_segments())

    reachable = set()
    for d in data:
        for x in range(len(d)-8):
            addr = struct.unpack_from('Q', d, x)[0]
            block = bisect_block(addr, starts, start_block)
            if block:
                reachable.add(block.address)

    for addr in search_reachable(reachable, starts, start_block):
        yield start_block[addr]

def used_blocks(blocks):
    for block in blocks:
        if block.used:
            yield block

def free_blocks(blocks):
    for block in blocks:
        if not block.used:
            yield block

def freq_blocks(blocks):
    summary = collections.defaultdict(int)
    count = 0
    size = 0
    for block in blocks:
        count += 1
        size += block.size
        summary[block.size] += 1
    return summary, count, size

def dump_blocks(blocks):
    """ summarizes given blocks """

    print('')
    print('BLOCK SUMMARY')

    summary, count, size = freq_blocks(blocks)

    print('%d blocks, %d total size' % (count, size))

    print('size frequencies:')
    for a,b in summary.items():
        print(a, b)

def unreachable_blocks(used_blocks):
    """ determine unreachable blocks """

    start_block = {b.address: b for b in used_blocks}
    reachable = set(b.address for b in reachable_blocks(used_blocks))

    return [start_block[addr] for addr in (set(start_block)-reachable)]

def subtract_blocks(ax, bx):
    astart_block = {a.address: a for a in ax}
    bstart_block = {b.address: b for b in bx}
    return [astart_block[addr] for addr in (set(astart_block) - set(bstart_block))]

def main():
    # tcmalloc
#    dump_pageheap()
#    dump_pagemap()
#    dump_central_cache()
#    dump_thread_caches()
    blocks = list(pagemap_blocks())
    used = list(used_blocks(blocks))
    free = list(free_blocks(blocks))
    print('')
    print('USED:')
    dump_blocks(used)
    print('')
    print('FREE:')
    dump_blocks(free)
    # reachability
    print('')
    print('LOST:')
    unreached = unreachable_blocks(used)
    dump_blocks(unreached)

if __name__ == '__main__':
    main()
