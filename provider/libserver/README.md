Libserver
=========

ECCacheManager
--------------

The ECCacheManager caches some specific MySQL table items in a hash map.
For example for the hierarchtable it is stored as <id, (hierarchyid, parentid, fooid>.
When the cache limit is hit, we randomly remove 5% of the items.

The sort() operation on unordered hashmaps is very expensive
so you might notice cache spikes every time the cache is cleaned.
A possible solution is randomly removing the cache entries.

             +-------------------------------------------------------------+
             |                                                             |
             |                                                             |
    cache --+|                  xxxxxxxx xxxxxxxx xxxxxxxxxx xxxxxxxxxxxxxx|
    limit    |               xxx       x x      x x        x x             |
             |            xxxx          x        x          x              |
             |         xxxx                                                |
             |        xx                                                   |
             |     xxx                                                     |
             |  xxxx                                                       |
             |xxx                                                          |
             xx------------------------------------------------------------+
