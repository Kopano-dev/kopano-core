# SPDX-License-Identifier: AGPL-3.0-only
import sys

from MAPICore import *

from .version import __version__

unicode = False

mod_struct = sys.modules.get('MAPI.Struct')
if mod_struct:
    mod_struct.MAPIError._initialize_errors()
