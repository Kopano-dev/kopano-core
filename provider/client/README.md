Kopano basic tech. information
===============================

The Kopano Message store provider works with the following components (layered top-to-bottom):

Client side:
- Full MAPI implementation to the client
- Local client handling of data structures, formats, and anything needing caching or local processing
- Abstract 'storage' classes for doing the actual transport of data to and from the server
- Currently only the WS (WebService) Transport is implemented, using gSOAP for serialization

Network:
- HTTP / SOAP network transport

Server side:
- Server deserialisation and RPC with gSOAP (through either Apache or standalone server)
- Storage layer for database communication
- MySQL database storage
