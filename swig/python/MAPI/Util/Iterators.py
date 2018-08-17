# SPDX-License-Identifier: AGPL-3.0-only
import MAPI
import MAPI.Util.Generators

class StoreIterator:
    def __init__(self, session, users = None, flags = MAPI.MDB_WRITE):
            self.session = session
            self.users = users
            self.flags = flags

    def Iterate(self, callable):
        for store in MAPI.Util.Generators.GetStores(self.session, self.users, self.flags):
            callable(store)


class FolderIterator:
    def __init__(self, store, **kwargs):
        self.store = store
        self.kwargs = kwargs

    def Iterate(self, callable):
        for folder, depth in MAPI.Util.Generators.GetFolders(self.store, **self.kwargs):
            callable(folder, depth)
