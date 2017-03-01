import atexit
import gc
import sys

from MAPICore import *

unicode = False

mod_struct = sys.modules.get('MAPI.Struct')
if mod_struct:
    mod_struct.MAPIError._initialize_errors()

# make sure that modules are cleaned up before calling MAPIUninitialize,
# so for example the store cache in kopano.lru_cache is indirectly emptied,
# and we won't have open/half-dead stores in memory in the end, which
# valgrind will have 10,000 things to say about..

def clear_modules():
    sys.modules.clear()
    gc.collect()

atexit.register(clear_modules)
