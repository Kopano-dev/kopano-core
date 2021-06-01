extern "C" {
	#include "php.h"
   	#include "php_globals.h"
   	#include "php_ini.h"
   	#include "zend_exceptions.h"
	#include "ext/standard/info.h"
	#include "ext/standard/php_string.h"
}

ZEND_BEGIN_ARG_INFO_EX(arginfo_mapi_last_hresult, 0, 0, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_mapi_prop_type, 0, 0, 1)
	ZEND_ARG_INFO(0, proptag)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_mapi_prop_id, 0, 0, 1)
	ZEND_ARG_INFO(0, proptag)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_mapi_is_error, 0, 0, 1)
	ZEND_ARG_INFO(0, errorcode)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_mapi_make_scode, 0, 0, 2)
	ZEND_ARG_INFO(0, sev)
	ZEND_ARG_INFO(0, code)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_mapi_prop_tag, 0, 0, 2)
	ZEND_ARG_INFO(0, proptype)
	ZEND_ARG_INFO(0, propid)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_mapi_createoneoff, 0, 0, 3)
	ZEND_ARG_INFO(0, displayname)
	ZEND_ARG_INFO(0, displaytype)
	ZEND_ARG_INFO(0, emailaddress)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_mapi_parseoneoff, 0, 0, 1)
	ZEND_ARG_INFO(0, entryid)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_mapi_logon_zarafa, 0, 0, 8)
	ZEND_ARG_INFO(0, username)
	ZEND_ARG_INFO(0, password)
	ZEND_ARG_INFO(0, server)
	ZEND_ARG_INFO(0, sslcert)
	ZEND_ARG_INFO(0, sslpass)
	ZEND_ARG_INFO(0, flags)
	ZEND_ARG_INFO(0, wa_version)
	ZEND_ARG_INFO(0, misc_version)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_mapi_getmsgstorestable, 0, 0, 1)
	ZEND_ARG_INFO(0, resource)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_mapi_openmsgstore, 0, 0, 2)
	ZEND_ARG_INFO(0, session)
	ZEND_ARG_INFO(0, storeentryid)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_mapi_openprofilesection, 0, 0, 2)
	ZEND_ARG_INFO(0, session)
	ZEND_ARG_INFO(0, profilesectionguid)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_mapi_openaddressbook, 0, 0, 2)
	ZEND_ARG_INFO(0, session)
	ZEND_ARG_INFO(0, entryid)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_mapi_openentry, 0, 0, 3)
	ZEND_ARG_INFO(0, resource)
	ZEND_ARG_INFO(0, entryid)
	ZEND_ARG_INFO(0, flags)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_mapi_ab_openentry, 0, 0, 3)
	ZEND_ARG_INFO(0, resource)
	ZEND_ARG_INFO(0, entryid)
	ZEND_ARG_INFO(0, flags)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_mapi_ab_resolvename, 0, 0, 3)
	ZEND_ARG_INFO(0, resource)
	ZEND_ARG_INFO(0, properties)
	ZEND_ARG_INFO(0, flags)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_mapi_ab_getdefaultdir, 0, 0, 1)
	ZEND_ARG_INFO(0, resource)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_mapi_msgstore_createentryid, 0, 0, 2)
	ZEND_ARG_INFO(0, store)
	ZEND_ARG_INFO(0, username)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_mapi_msgstore_getarchiveentryid, 0, 0, 2)
	ZEND_ARG_INFO(0, user)
	ZEND_ARG_INFO(0, server)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_mapi_msgstore_openentry, 0, 0, 2)
	ZEND_ARG_INFO(0, msgstore)
	ZEND_ARG_INFO(0, entryid)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_mapi_msgstore_getreceivefolder, 0, 0, 1)
	ZEND_ARG_INFO(0, store)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_mapi_msgstore_entryidfromsourcekey, 0, 0, 2)
	ZEND_ARG_INFO(0, store)
	ZEND_ARG_INFO(0, folderid)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_mapi_msgstore_advise, 0, 0, 4)
	ZEND_ARG_INFO(0, store)
	ZEND_ARG_INFO(0, folderentryid)
	ZEND_ARG_INFO(0, mask)
	ZEND_ARG_INFO(0, sink)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_mapi_msgstore_unadvise, 0, 0, 2)
	ZEND_ARG_INFO(0, store)
	ZEND_ARG_INFO(0, sink)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_mapi_msgstore_abortsubmit, 0, 0, 2)
	ZEND_ARG_INFO(0, store)
	ZEND_ARG_INFO(0, entryid)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_mapi_sink_create, 0, 0, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_mapi_sink_timedwait, 0, 0, 2)
	ZEND_ARG_INFO(0, sink)
	ZEND_ARG_INFO(0, time)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_mapi_table_queryallrows, 0, 0, 3)
	ZEND_ARG_INFO(0, mapitable)
	ZEND_ARG_INFO(0, properties)
	ZEND_ARG_INFO(0, restriction)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_mapi_table_queryrows, 0, 0, 4)
	ZEND_ARG_INFO(0, mapitable)
	ZEND_ARG_INFO(0, columns)
	ZEND_ARG_INFO(0, start)
	ZEND_ARG_INFO(0, rowcount)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_mapi_table_getrowcount, 0, 0, 1)
	ZEND_ARG_INFO(0, table)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_mapi_table_setcolumns, 0, 0, 2)
	ZEND_ARG_INFO(0, table)
	ZEND_ARG_INFO(0, properties)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_mapi_table_seekrow, 0, 0, 3)
	ZEND_ARG_INFO(0, table)
	ZEND_ARG_INFO(0, bookmark)
	ZEND_ARG_INFO(0, flags)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_mapi_table_sort, 0, 0, 3)
	ZEND_ARG_INFO(0, table)
	ZEND_ARG_INFO(0, properties)
	ZEND_ARG_INFO(0, flags)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_mapi_table_restrict, 0, 0, 3)
	ZEND_ARG_INFO(0, table)
	ZEND_ARG_INFO(0, restriction)
	ZEND_ARG_INFO(0, flags)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_mapi_folder_gethierarchytable, 0, 0, 2)
	ZEND_ARG_INFO(0, folder)
	ZEND_ARG_INFO(0, flags)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_mapi_folder_getcontentstable, 0, 0, 2)
	ZEND_ARG_INFO(0, folder)
	ZEND_ARG_INFO(0, flags)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_mapi_folder_createmessage, 0, 0, 2)
	ZEND_ARG_INFO(0, folder)
	ZEND_ARG_INFO(0, flags)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_mapi_folder_createfolder, 0, 0, 4)
	ZEND_ARG_INFO(0, folder)
	ZEND_ARG_INFO(0, name)
	ZEND_ARG_INFO(0, comment)
	ZEND_ARG_INFO(0, flags)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_mapi_folder_deletemessages, 0, 0, 3)
	ZEND_ARG_INFO(0, folder)
	ZEND_ARG_INFO(0, entryids)
	ZEND_ARG_INFO(0, flags)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_mapi_folder_copymessages, 0, 0, 3)
	ZEND_ARG_INFO(0, folder)
	ZEND_ARG_INFO(0, entryids)
	ZEND_ARG_INFO(0, flags)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_mapi_folder_emptyfolder, 0, 0, 2)
	ZEND_ARG_INFO(0, folder)
	ZEND_ARG_INFO(0, flags)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_mapi_folder_copyfolder, 0, 0, 5)
	ZEND_ARG_INFO(0, sourcefolder)
	ZEND_ARG_INFO(0, sourceentryid)
	ZEND_ARG_INFO(0, destfolder)
	ZEND_ARG_INFO(0, foldername)
	ZEND_ARG_INFO(0, flags)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_mapi_folder_deletefolder, 0, 0, 3)
	ZEND_ARG_INFO(0, folder)
	ZEND_ARG_INFO(0, entryid)
	ZEND_ARG_INFO(0, flags)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_mapi_folder_setreadflags, 0, 0, 3)
	ZEND_ARG_INFO(0, folder)
	ZEND_ARG_INFO(0, entries)
	ZEND_ARG_INFO(0, flags)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_mapi_folder_openmodifytable, 0, 0, 2)
	ZEND_ARG_INFO(0, folder)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_mapi_folder_setsearchcriteria, 0, 0, 4)
	ZEND_ARG_INFO(0, folder)
	ZEND_ARG_INFO(0, restriction)
	ZEND_ARG_INFO(0, entryids)
	ZEND_ARG_INFO(0, subfolder)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_mapi_folder_getsearchcriteria, 0, 0, 2)
	ZEND_ARG_INFO(0, folder)
	ZEND_ARG_INFO(0, flags)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_mapi_message_getattachmenttable, 0, 0, 1)
	ZEND_ARG_INFO(0, folder)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_mapi_message_getrecipientttable, 0, 0, 1)
	ZEND_ARG_INFO(0, folder)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_mapi_message_openattach, 0, 0, 2)
	ZEND_ARG_INFO(0, message)
	ZEND_ARG_INFO(0, attachnum)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_mapi_message_createattach, 0, 0, 2)
	ZEND_ARG_INFO(0, message)
	ZEND_ARG_INFO(0, flags)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_mapi_message_deleteattach, 0, 0, 3)
	ZEND_ARG_INFO(0, message)
	ZEND_ARG_INFO(0, attachnum)
	ZEND_ARG_INFO(0, flags)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_mapi_message_modifyrecipients, 0, 0, 3)
	ZEND_ARG_INFO(0, message)
	ZEND_ARG_INFO(0, flags)
	ZEND_ARG_INFO(0, addresslist)
ZEND_END_ARG_INFO()


ZEND_BEGIN_ARG_INFO_EX(arginfo_mapi_message_submitmessage, 0, 0, 1)
	ZEND_ARG_INFO(0, message)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_mapi_message_setreadflag, 0, 0, 2)
	ZEND_ARG_INFO(0, message)
	ZEND_ARG_INFO(0, flags)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_mapi_stream_write, 0, 0, 2)
	ZEND_ARG_INFO(0, stream)
	ZEND_ARG_INFO(0, body)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_mapi_stream_read, 0, 0, 2)
	ZEND_ARG_INFO(0, stream)
	ZEND_ARG_INFO(0, size)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_mapi_stream_stat, 0, 0, 1)
	ZEND_ARG_INFO(0, stream)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_mapi_stream_seek, 0, 0, 2)
	ZEND_ARG_INFO(0, message)
	ZEND_ARG_INFO(0, flags)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_mapi_stream_commit, 0, 0, 1)
	ZEND_ARG_INFO(0, stream)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_mapi_stream_setsize, 0, 0, 2)
	ZEND_ARG_INFO(0, stream)
	ZEND_ARG_INFO(0, size)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_mapi_stream_create, 0, 0, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_mapi_attach_openobj, 0, 0, 2)
	ZEND_ARG_INFO(0, attachment)
	ZEND_ARG_INFO(0, flags)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_mapi_savechanges, 0, 0, 2)
	ZEND_ARG_INFO(0, message)
	ZEND_ARG_INFO(0, flags)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_mapi_getprops, 0, 0, 2)
	ZEND_ARG_INFO(0, message)
	ZEND_ARG_INFO(0, properties)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_mapi_setprops, 0, 0, 2)
	ZEND_ARG_INFO(0, message)
	ZEND_ARG_INFO(0, properties)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_mapi_copyto, 0, 0, 5)
	ZEND_ARG_INFO(0, resource)
	ZEND_ARG_INFO(0, exludeentryids)
	ZEND_ARG_INFO(0, excludeprops)
	ZEND_ARG_INFO(0, destination)
	ZEND_ARG_INFO(0, flags)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_mapi_openproperty, 0, 0, 2)
	ZEND_ARG_INFO(0, resource)
	ZEND_ARG_INFO(0, flags)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_mapi_deleteprops, 0, 0, 2)
	ZEND_ARG_INFO(0, resource)
	ZEND_ARG_INFO(0, properties)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_mapi_getnamesfromids, 0, 0, 2)
	ZEND_ARG_INFO(0, store)
	ZEND_ARG_INFO(0, array)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_mapi_getidsfromnames, 0, 0, 3)
	ZEND_ARG_INFO(0, store)
	ZEND_ARG_INFO(0, propertynames)
	ZEND_ARG_INFO(0, guids)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_mapi_decompressrtf, 0, 0, 1)
	ZEND_ARG_INFO(0, rtf)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_mapi_createconversationindex, 0, 0, 1)
	ZEND_ARG_INFO(0, blob)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_mapi_rules_gettable, 0, 0, 1)
	ZEND_ARG_INFO(0, table)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_mapi_rules_modifytable, 0, 0, 3)
	ZEND_ARG_INFO(0, table)
	ZEND_ARG_INFO(0, rows)
	ZEND_ARG_INFO(0, flags)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_mapi_zarafa_getuser_by_id, 0, 0, 2)
	ZEND_ARG_INFO(0, store)
	ZEND_ARG_INFO(0, userid)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_mapi_zarafa_getuser_by_name, 0, 0, 2)
	ZEND_ARG_INFO(0, store)
	ZEND_ARG_INFO(0, username)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_mapi_zarafa_getuserlist, 0, 0, 2)
	ZEND_ARG_INFO(0, store)
	ZEND_ARG_INFO(0, companyid)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_mapi_zarafa_getgrouplist, 0, 0, 2)
	ZEND_ARG_INFO(0, store)
	ZEND_ARG_INFO(0, companyid)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_mapi_zarafa_getgrouplistofuser, 0, 0, 2)
	ZEND_ARG_INFO(0, store)
	ZEND_ARG_INFO(0, userid)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_mapi_zarafa_getgrouplistofgroup, 0, 0, 2)
	ZEND_ARG_INFO(0, store)
	ZEND_ARG_INFO(0, groupid)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_mapi_zarafa_getuserlistofgroup, 0, 0, 2)
	ZEND_ARG_INFO(0, store)
	ZEND_ARG_INFO(0, groupid)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_mapi_zarafa_getcompanylist, 0, 0, 1)
	ZEND_ARG_INFO(0, store)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_mapi_zarafa_getpermissionsrules, 0, 0, 2)
	ZEND_ARG_INFO(0, resource)
	ZEND_ARG_INFO(0, flags)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_mapi_zarafa_setpermissionsrules, 0, 0, 2)
	ZEND_ARG_INFO(0, resource)
	ZEND_ARG_INFO(0, rules)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_mapi_freebusy_openmsg, 0, 0, 1)
	ZEND_ARG_INFO(0, store)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_mapi_freebusysupport_open, 0, 0, 2)
	ZEND_ARG_INFO(0, session)
	ZEND_ARG_INFO(0, store)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_mapi_freebusysupport_close, 0, 0, 1)
	ZEND_ARG_INFO(0, freebusysupport)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_mapi_freebusysupport_loaddata, 0, 0, 2)
	ZEND_ARG_INFO(0, freebusysupport)
	ZEND_ARG_INFO(0, data)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_mapi_freebusysupport_loadupdate, 0, 0, 2)
	ZEND_ARG_INFO(0, freebusysupport)
	ZEND_ARG_INFO(0, data)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_mapi_freebusydata_enumblocks, 0, 0, 3)
	ZEND_ARG_INFO(0, fbdata)
	ZEND_ARG_INFO(0, start)
	ZEND_ARG_INFO(0, end)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_mapi_freebusydata_getpublishrange, 0, 0, 2)
	ZEND_ARG_INFO(0, fbdata)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_mapi_freebusydata_setrange, 0, 0, 3)
	ZEND_ARG_INFO(0, fbdata)
	ZEND_ARG_INFO(0, start)
	ZEND_ARG_INFO(0, end)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_mapi_freebusyenumblock_reset, 0, 0, 1)
	ZEND_ARG_INFO(0, enumblock)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_mapi_freebusyenumblock_next, 0, 0, 2)
	ZEND_ARG_INFO(0, enumblock)
	ZEND_ARG_INFO(0, count)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_mapi_freebusyenumblock_skip, 0, 0, 2)
	ZEND_ARG_INFO(0, enumblock)
	ZEND_ARG_INFO(0, count)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_mapi_freebusyenumblock_restrict, 0, 0, 3)
	ZEND_ARG_INFO(0, enumblock)
	ZEND_ARG_INFO(0, start)
	ZEND_ARG_INFO(0, end)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_mapi_freebusyenumblock_ical, 0, 0, 7)
	ZEND_ARG_INFO(0, enumblock)
	ZEND_ARG_INFO(0, count)
	ZEND_ARG_INFO(0, start)
	ZEND_ARG_INFO(0, end)
	ZEND_ARG_INFO(0, organiser)
	ZEND_ARG_INFO(0, user)
	ZEND_ARG_INFO(0, uid)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_mapi_freebusyupdate_publish, 0, 0, 2)
	ZEND_ARG_INFO(0, fbupdate)
	ZEND_ARG_INFO(0, blocks)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_mapi_freebusyupdate_reset, 0, 0, 1)
	ZEND_ARG_INFO(0, fbupdate)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_mapi_freebusyupdate_savechanges, 0, 0, 3)
	ZEND_ARG_INFO(0, fbupdate)
	ZEND_ARG_INFO(0, start)
	ZEND_ARG_INFO(0, end)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_mapi_exportchanges_config, 0, 0, 8)
	ZEND_ARG_INFO(0, exportchanges)
	ZEND_ARG_INFO(0, stream)
	ZEND_ARG_INFO(0, flags)
	ZEND_ARG_INFO(0, importchanges)
	ZEND_ARG_INFO(0, restriction)
	ZEND_ARG_INFO(0, includeprops)
	ZEND_ARG_INFO(0, excludeprops)
	ZEND_ARG_INFO(0, buffersize)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_mapi_exportchanges_synchronize, 0, 0, 1)
	ZEND_ARG_INFO(0, exportchanges)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_mapi_exportchanges_updatestate, 0, 0, 2)
	ZEND_ARG_INFO(0, exportchanges)
	ZEND_ARG_INFO(0, stream)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_mapi_exportchanges_getchangecount, 0, 0, 1)
	ZEND_ARG_INFO(0, exportchanges)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_mapi_importcontentschanges_config, 0, 0, 3)
	ZEND_ARG_INFO(0, importcontentschanges)
	ZEND_ARG_INFO(0, stream)
	ZEND_ARG_INFO(0, flags)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_mapi_importcontentschanges_updatestate, 0, 0, 2)
	ZEND_ARG_INFO(0, importcontentschanges)
	ZEND_ARG_INFO(0, stream)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_mapi_importcontentschanges_importmessagechange, 0, 0, 4)
	ZEND_ARG_INFO(0, importer)
	ZEND_ARG_INFO(0, props)
	ZEND_ARG_INFO(0, flags)
	ZEND_ARG_INFO(1, mapimessage)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_mapi_importcontentschanges_importmessagedeletion, 0, 0, 3)
	ZEND_ARG_INFO(0, importcontentschanges)
	ZEND_ARG_INFO(0, flags)
	ZEND_ARG_INFO(0, messages)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_mapi_importcontentschanges_importperuserreadstatechange, 0, 0, 2)
	ZEND_ARG_INFO(0, importcontentschanges)
	ZEND_ARG_INFO(0, readstates)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_mapi_importcontentschanges_importmessagemove, 0, 0, 6)
	ZEND_ARG_INFO(0, importcontentschanges)
	ZEND_ARG_INFO(0, sourcekeyfolder)
	ZEND_ARG_INFO(0, sourcekeymessage)
	ZEND_ARG_INFO(0, message)
	ZEND_ARG_INFO(0, sourcekey)
	ZEND_ARG_INFO(0, changenumdestmessage)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_mapi_importhierarchychanges_config, 0, 0, 3)
	ZEND_ARG_INFO(0, importhierarchychanges)
	ZEND_ARG_INFO(0, stream)
	ZEND_ARG_INFO(0, flags)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_mapi_importhierarchychanges_updatestate, 0, 0, 2)
	ZEND_ARG_INFO(0, importhierarchychanges)
	ZEND_ARG_INFO(0, stream)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_mapi_importhierarchychanges_importfolderchange, 0, 0, 2)
	ZEND_ARG_INFO(0, importhierarchychanges)
	ZEND_ARG_INFO(0, props)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_mapi_importhierarchychanges_importfolderdeletion, 0, 0, 3)
	ZEND_ARG_INFO(0, importhierarchychanges)
	ZEND_ARG_INFO(0, flags)
	ZEND_ARG_INFO(0, folders)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_mapi_wrap_importcontentschanges, 0, 0, 3)
	ZEND_ARG_INFO(1, phpwrapper)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_mapi_wrap_importhierarchychanges, 0, 0, 1)
	ZEND_ARG_INFO(1, phpwrapper)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_mapi_inetmapi_imtoinet, 0, 0, 4)
	ZEND_ARG_INFO(0, session)
	ZEND_ARG_INFO(0, addrbook)
	ZEND_ARG_INFO(0, message)
	ZEND_ARG_INFO(0, options)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_mapi_inetmapi_imtomapi, 0, 0, 6)
	ZEND_ARG_INFO(0, session)
	ZEND_ARG_INFO(0, store)
	ZEND_ARG_INFO(0, addrbook)
	ZEND_ARG_INFO(0, messsage)
	ZEND_ARG_INFO(0, data)
	ZEND_ARG_INFO(0, options)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_mapi_icaltomapi, 0, 0, 6)
	ZEND_ARG_INFO(0, session)
	ZEND_ARG_INFO(0, store)
	ZEND_ARG_INFO(0, addrbook)
	ZEND_ARG_INFO(0, message)
	ZEND_ARG_INFO(0, data)
	ZEND_ARG_INFO(0, noRecipients)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_mapi_icaltomapi2, 0, 0, 3)
	ZEND_ARG_INFO(0, addrbook)
	ZEND_ARG_INFO(0, folder)
	ZEND_ARG_INFO(0, ics_data)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_mapi_numinvalidicalproperties, 0, 0 ,0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_mapi_numinvalidicalcomponents, 0, 0 ,0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_mapi_mapitoical, 0, 0, 3)
	ZEND_ARG_INFO(0, ignored)
	ZEND_ARG_INFO(0, addrbook)
	ZEND_ARG_INFO(0, message)
	ZEND_ARG_INFO(0, options)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_mapi_vcftomapi, 0, 0, 3)
	ZEND_ARG_INFO(0, ignored)
	ZEND_ARG_INFO(0, message)
	ZEND_ARG_INFO(0, data)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_mapi_vcftomapi2, 0, 0, 2)
	ZEND_ARG_INFO(0, folder)
	ZEND_ARG_INFO(0, data)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_mapi_mapitovcf, 0, 0, 4)
	ZEND_ARG_INFO(0, ignored)
	ZEND_ARG_INFO(0, addrbook)
	ZEND_ARG_INFO(0, message)
	ZEND_ARG_INFO(0, options)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_mapi_enable_exceptions, 0, 0, 1)
	ZEND_ARG_INFO(0, str_class)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_mapi_feature, 0, 0, 1)
	ZEND_ARG_INFO(0, feature)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_kc_session_save, 0, 0, 2)
	ZEND_ARG_INFO(0, session)
	ZEND_ARG_INFO(1, data)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_kc_session_restore, 0, 0, 2)
	ZEND_ARG_INFO(0, session)
	ZEND_ARG_INFO(1, data)
ZEND_END_ARG_INFO()
