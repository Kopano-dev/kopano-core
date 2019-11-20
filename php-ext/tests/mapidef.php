<?php
define('PT_BINARY'                               ,258);
define('PT_BOOLEAN'                              ,11);
define('PT_STRING8'                              ,30);
define('PT_SYSTIME'                              ,64);
define('PT_TSTRING'                              ,PT_STRING8);

define('PR_EC_BASE'                              ,0x6700);

define('PR_CONTAINER_CLASS'                      ,mapi_prop_tag(PT_TSTRING,     0x3613));
define('PR_CREATION_TIME'                        ,mapi_prop_tag(PT_SYSTIME,     0x3007));
define('PR_DEFAULT_STORE'                        ,mapi_prop_tag(PT_BOOLEAN,     0x3400));
define('PR_DISPLAY_NAME'                         ,mapi_prop_tag(PT_TSTRING,     0x3001));
define('PR_ENTRYID'                              ,mapi_prop_tag(PT_BINARY,      0x0FFF));
define('PR_SUBJECT'                              ,mapi_prop_tag(PT_TSTRING,     0x0037));
define('PR_EC_WEBACCESS_SETTINGS_JSON'           ,mapi_prop_tag(PT_STRING8,     PR_EC_BASE+0x72));

define('TABLE_SORT_ASCEND'                       ,(0x00000000));
define('TABLE_SORT_DESCEND'                      ,(0x00000001));

define('FL_PREFIX'                               ,0x00000002);
define('FL_IGNORECASE'                           ,0x00010000);
define('FUZZYLEVEL'                              ,2);

define('ULPROPTAG'                               ,6);

define('RES_CONTENT'                             ,3);
define('RES_PROPERTY'                            ,4);

define('VALUE'                                   ,0);

define('OPEN_IF_EXISTS'                          ,0x00000001);

define('fnevObjectCreated'                       ,0x00000004);

define('MAPI_MODIFY'                             ,0x00000001);
define('MAPI_CREATE'                             ,0x00000002);

define('ACCESS_TYPE_GRANT'                       ,2);

define('MODRECIP_ADD'                            ,0x00000002);

define('STGM_TRANSACTED'			 ,0x00010000);

define('IID_IStream',                                                         makeguid("{0000000c-0000-0000-c000-000000000046}"));

