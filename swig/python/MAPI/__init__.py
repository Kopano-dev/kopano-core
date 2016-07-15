import atexit
import gc
import sys

from MAPICore import *

def mapi_uninitialize():
    gc.collect() # do this early, so we don't touch objects after uninitialization (avoiding a crash for certain tests)
    MAPIUninitialize()

MAPIInitialize_Multithreaded()
atexit.register(mapi_uninitialize)

unicode = False

mod_struct = sys.modules.get('MAPI.Struct')
if mod_struct:
    mod_struct.MAPIError._initialize_errors()
