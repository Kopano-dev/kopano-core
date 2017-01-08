A quick overview of the Kopano code structure
=============================================

Overview
========

The codebase has a lot of libraries, components and for example scripts.
Each top level directory is listed below with a clear description of what it does.


autoconf     - helpers for autoconf: php and swig
caldav       - Ical server implementation which uses kopano-server as backend
common       - commonly used libraries for everything except server/client. There are three different "common" utils: util, ssl and mapi.
doc          - Kopano documentation and manpages
ECtools      - ECtools consists of a lot of seperate tools which use the MAPI library.
gateway      - POP3/IMAP gateway for Kopano
inetmapi     - VMIME to MAPI and MAPI to VMIME library (should be called libinetmapi)
libfreebusy  - Freebusy library from windows and from Linux
libicalmapi  - Ical to MAPI and MAPI to Ical library
mapi4linux   - Source compatbile Windows MAPI library
php-ext      - PHP-MAPI module
php7-ext     - PHP-MAPI module (PHP 7)
po           - Translations
provider     - Directory where the kopano-server and client is located
spooler      - Kopano-dagent/spooler
swig         - Python bindings for MAPI
tools        - Collection of PHP and Python tools
libsync      - Shared library that is used by the synchronisation agent in Windows for offline synchronisation

MAPI Properties
===============

All initial MAPI properties can be found in mapi4linux/include/mapi.h, later added property definitions are found in common/mapiext.h.
Kopano defined properties (all properties which start with PR_EC_*) can be found in common/ECTags.h.

Provider
========

Provider consists of a few directories:
client     - Generates mapi client exposes MAPI interface. (Providers GAB, all your stores, private and delegate)
libserver  - Splitted for historical reasons from server
server     - the kopano-server
common     - soap ulitlities for provider / client (ECSearchClient <-> Server and Indexer)
contacts   - addressbookprovier, displays one or multiple addressbooks in for example webapp as contacts.
plugins    - Userplugins: ldap, ad, db and unix.
