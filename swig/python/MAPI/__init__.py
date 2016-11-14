import sys

from MAPICore import *

MAPIInitialize_Multithreaded()

class mapiUninitialize:
    def __del__(self):
        MAPIUninitialize()

_mapiUninitialize = mapiUninitialize()

unicode = False

mod_struct = sys.modules.get('MAPI.Struct')
if mod_struct:
    mod_struct.MAPIError._initialize_errors()
