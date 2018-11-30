/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */

#ifndef ECDBDEF_H
#define ECDBDEF_H

/**
 * @page kopano_db Database
 *
 * @section kopano_db_layout Database layout
 *
 * Server tables:
 * @code
 * abchanges         | All addressbook changes
 * acl               | User permission objects
 * changes           | Object changes
 * hierarchy         | The hiearchy between the mapi objects
 * indexedproperties | Mapi object entryid and sourcekey
 * lob               | Attachment data. Only when the setting attachment in database is enabled
 * mvproperties      | The multi value properties of a mapi object. Store, folder and message
 * names             | Custom property defines
 * outgoingqueue     | Pending messages to send
 * properties        | The single value properties of a mapi object. Store, folder and message
 * receivefolder     | Specifies the mapi receivefolder, for example the inbox
 * searchresults     | Search folder results
 * settings          | Server dependent settings
 * singleinstances   | The relation between an attachment and one or more message objects
 * stores            | A list with data stores related to one user and includes the deleted stores.
 * syncedmessages    | Messages which are synced with a specific restriction
 * syncs             | Sync state of a folder
 * users             | User relation between the userplugin and kopano
 * versions          | Database update information
 * @endcode
 *
 * Database and unix user plugin tables:
 * @code
 * object            | Unique user object id and user type
 * objectmvproperty  | Multi value properties of a user
 * objectproperty    | Single value properties of a user
 * objectrelation    | User, group, company and sendas relations
 * @endcode
 *
 * @todo Add an image of the database layout
 *
 * @section kopano_db_update Database update system
 *
 * @todo describe the update system
 *
 *
 */

#define Z_TABLEDEF_ACL				"CREATE TABLE `acl` ( \
										`id` int(11) NOT NULL default '0', \
										`hierarchy_id` int(11) unsigned NOT NULL default '0', \
										`type` tinyint(4) unsigned NOT NULL default '0', \
										`rights` int(11) unsigned NOT NULL default '0', \
										PRIMARY KEY  (`hierarchy_id`,`id`,`type`) \
	) ENGINE=%s CHARACTER SET utf8mb4;"

#define Z_TABLEDEF_HIERARCHY		"CREATE TABLE `hierarchy` ( \
										`id` int(11) unsigned NOT NULL auto_increment, \
										`parent` int(11) unsigned default '0', \
										`type` tinyint(4) unsigned NOT NULL default '0', \
										`flags` smallint(6) unsigned NOT NULL default '0', \
										`owner` int(11) unsigned NOT NULL default '0', \
										PRIMARY KEY  (`id`), \
										KEY `parenttypeflags` (`parent`, `type`, `flags`) \
	) ENGINE=%s CHARACTER SET utf8mb4;"

#define Z_TABLEDEF_NAMES			"CREATE TABLE `names` ( \
										`id` int(11) NOT NULL auto_increment, \
										`nameid` int(11) default NULL, \
		`namestring` varchar(185) BINARY DEFAULT NULL, \
										`guid` binary(16) NOT NULL, \
										PRIMARY KEY  (`id`), \
										KEY `nameid` (`nameid`), \
										KEY `namestring` (`namestring`), \
										UNIQUE KEY `gni` (`guid`,`nameid`), \
										UNIQUE KEY `gns` (`guid`,`namestring`) \
	) ENGINE=%s CHARACTER SET utf8mb4;"

#define Z_TABLEDEF_MVPROPERTIES		"CREATE TABLE `mvproperties` ( \
										`hierarchyid` int(11) unsigned NOT NULL default '0', \
										`orderid` smallint(6) unsigned NOT NULL default '0', \
										`tag` smallint(6) unsigned NOT NULL default '0', \
										`type` smallint(6) unsigned NOT NULL default '0', \
										`val_ulong` int(11) unsigned default NULL, \
										`val_string` longtext, \
										`val_binary` longblob, \
										`val_double` double default NULL, \
										`val_longint` bigint(20) default NULL, \
										`val_hi` int(11) default NULL, \
										`val_lo` int(11) unsigned default NULL, \
										PRIMARY KEY (`hierarchyid`, `tag`, `type`, `orderid`) \
	) ENGINE=%s CHARACTER SET utf8mb4;"

#define Z_TABLEDEF_TPROPERTIES		"CREATE TABLE `tproperties` ( \
										`folderid` int(11) unsigned NOT NULL default '0', \
										`hierarchyid` int(11) unsigned NOT NULL default '0', \
										`tag` smallint(6) unsigned NOT NULL default '0', \
										`type` smallint(6) unsigned NOT NULL, \
										`val_ulong` int(11) unsigned default NULL, \
										`val_string` longtext, \
										`val_binary` longblob, \
										`val_double` double default NULL, \
										`val_longint` bigint(20) default NULL, \
										`val_hi` int(11) default NULL, \
										`val_lo` int(11) unsigned default NULL, \
										PRIMARY KEY `ht` (`folderid`,`tag`,`hierarchyid`,`type`), \
										KEY `hi` (`hierarchyid`) \
	) ENGINE=%s CHARACTER SET utf8mb4;"

#define Z_TABLEDEF_DELAYEDUPDATE	"CREATE TABLE `deferredupdate` (\
                                        `hierarchyid` int(11) unsigned NOT NULL, \
                                        `folderid` int(11) unsigned NOT NULL, \
                                        `srcfolderid` int(11) unsigned, \
                                        PRIMARY KEY(`hierarchyid`), \
                                        KEY `folderid` (`folderid`) \
	) ENGINE=%s CHARACTER SET utf8mb4;"

#define Z_TABLEDEF_PROPERTIES		"CREATE TABLE `properties` ( \
										`hierarchyid` int(11) unsigned NOT NULL default '0', \
										`tag` smallint(6) unsigned NOT NULL default '0', \
										`type` smallint(6) unsigned NOT NULL, \
										`val_ulong` int(11) unsigned default NULL, \
										`val_string` longtext, \
										`val_binary` longblob, \
										`val_double` double default NULL, \
										`val_longint` bigint(20) default NULL, \
										`val_hi` int(11) default NULL, \
										`val_lo` int(11) unsigned default NULL, \
										`comp` bool default false, \
										PRIMARY KEY `ht` (`hierarchyid`,`tag`,`type`) \
	) ENGINE=%s CHARACTER SET utf8mb4;"

#define Z_TABLEDEF_RECEIVEFOLDER	"CREATE TABLE `receivefolder` (  \
										`id` int(11) unsigned NOT NULL auto_increment, \
										`storeid` int(11) unsigned NOT NULL default '0', \
										`objid` int(11) unsigned NOT NULL default '0', \
		`messageclass` varchar(185) NOT NULL DEFAULT '', \
										PRIMARY KEY  (`id`), \
										UNIQUE KEY `storeid` (`storeid`,`messageclass`) \
	) ENGINE=%s CHARACTER SET utf8mb4;"

#define Z_TABLEDEF_STORES			"CREATE TABLE `stores` ( \
										`id` int(11) unsigned NOT NULL auto_increment, \
										`hierarchy_id` int(11) unsigned NOT NULL default '0', \
										`user_id` int(11) unsigned NOT NULL default '0', \
										`type` smallint(6) unsigned NOT NULL default '0', \
										`user_name` varchar(255) CHARACTER SET utf8 NOT NULL default '', \
										`company` int(11) unsigned NOT NULL default '0', \
										`guid` blob NOT NULL, \
										PRIMARY KEY  (`user_id`, `hierarchy_id`, `type`), \
										UNIQUE KEY `id` (`id`) \
	) ENGINE=%s CHARACTER SET utf8mb4;"

#define Z_TABLEDEF_USERS			"CREATE TABLE `users` ( \
										`id` int(11) unsigned NOT NULL auto_increment, \
										`externid` blob, \
										`objectclass` int(11) NOT NULL default '0', \
										`signature` varbinary(255) NOT NULL default '0', \
										`company` int(11) unsigned NOT NULL default '0', \
										PRIMARY KEY  (`id`), \
										UNIQUE KEY externid (`externid`(255), `objectclass`) \
	) ENGINE=%s CHARACTER SET utf8mb4;"

#define Z_TABLEDEF_OUTGOINGQUEUE	"CREATE TABLE `outgoingqueue` ( \
										`store_id` int(11) unsigned NOT NULL default '0', \
										`hierarchy_id` int(11) unsigned NOT NULL default '0', \
										`flags` tinyint(4) unsigned NOT NULL default '0', \
										PRIMARY KEY (`hierarchy_id`,`flags`,`store_id`) \
	) ENGINE=%s CHARACTER SET utf8mb4;"

#define Z_TABLEDEF_LOB				"CREATE TABLE `lob` ( \
										`instanceid` int(11) unsigned NOT NULL, \
										`chunkid` smallint(6) unsigned NOT NULL, \
										`tag` smallint(6) unsigned NOT NULL, \
										`val_binary` longblob, \
										PRIMARY KEY (`instanceid`,`tag`,`chunkid`) \
	) ENGINE=%s MAX_ROWS=1000000000 AVG_ROW_LENGTH=1750 CHARACTER SET utf8mb4;"

#define Z_TABLEDEF_REFERENCES "CREATE TABLE `singleinstances` ( \
	`instanceid` int(11) unsigned NOT NULL auto_increment, \
	`hierarchyid` int(11) unsigned NOT NULL default '0', \
	`tag` smallint(6) unsigned NOT NULL default '0', \
	`filename` varchar(255) DEFAULT NULL, \
	PRIMARY KEY (`instanceid`, `hierarchyid`, `tag`), \
	UNIQUE KEY `hkey` (`hierarchyid`, `tag`) \
) ENGINE=%s CHARACTER SET utf8mb4;"

#define Z_TABLEDEF_OBJECT			"CREATE TABLE object ( \
										`id` int(11) unsigned NOT NULL auto_increment, \
										`externid` blob, \
										`objectclass` int(11) unsigned NOT NULL default '0', \
										PRIMARY KEY (`id`, `objectclass`), \
										UNIQUE KEY id (`id`), \
										UNIQUE KEY externid (`externid`(255), `objectclass`) \
	) ENGINE=%s CHARACTER SET utf8mb4;"

#define Z_TABLEDEF_OBJECT_PROPERTY	"CREATE TABLE objectproperty ( \
										`objectid` int(11) unsigned NOT NULL default '0', \
		`propname` varchar(185) BINARY NOT NULL, \
										`value` text, \
										PRIMARY KEY  (`objectid`, `propname`) \
	) ENGINE=%s CHARACTER SET utf8mb4;"

#define Z_TABLEDEF_OBJECT_MVPROPERTY "CREATE TABLE objectmvproperty ( \
										`objectid` int(11) unsigned NOT NULL default '0', \
		`propname` varchar(185) BINARY NOT NULL, \
										`orderid` tinyint(11) unsigned NOT NULL default '0', \
										`value` text, \
										PRIMARY KEY (`objectid`, `orderid`, `propname`) \
	) ENGINE=%s CHARACTER SET utf8mb4;"

#define Z_TABLEDEF_OBJECT_RELATION	"CREATE TABLE objectrelation ( \
										`objectid` int(11) unsigned NOT NULL default '0', \
										`parentobjectid` int(11) unsigned NOT NULL default '0', \
										`relationtype` tinyint(11) unsigned NOT NULL, \
										PRIMARY KEY  (`objectid`, `parentobjectid`, `relationtype`) \
	) ENGINE=%s CHARACTER SET utf8mb4;"

#define Z_TABLEDEF_VERSIONS			"CREATE TABLE versions ( \
										`major` int(11) unsigned NOT NULL default '0', \
										`minor` int(11) unsigned NOT NULL default '0', \
										`micro` int(11) unsigned not null default 0, \
										`revision` int(11) unsigned NOT NULL default '0', \
										`databaserevision` int(11) unsigned NOT NULL default '0', \
										`updatetime` datetime NOT NULL, \
										PRIMARY KEY  (`major`, `minor`, `micro`, `revision`, `databaserevision`) \
	) ENGINE=%s CHARACTER SET utf8mb4;"

#define Z_TABLEDEF_SEARCHRESULTS	"CREATE TABLE searchresults ( \
										`folderid` int(11) unsigned NOT NULL default '0', \
										`hierarchyid` int(11) unsigned NOT NULL default '0', \
										`flags` int(11) unsigned NOT NULL default '0', \
										PRIMARY KEY (`folderid`, `hierarchyid`) \
	) ENGINE=%s CHARACTER SET utf8mb4;"

#define Z_TABLEDEF_CHANGES			"CREATE TABLE `changes` ( \
										`id` INT(11) UNSIGNED NOT NULL AUTO_INCREMENT, \
										`sourcekey` VARBINARY(64) NOT NULL, \
										`parentsourcekey` VARBINARY(64) NOT NULL, \
										`change_type` INT(11) UNSIGNED NOT NULL DEFAULT '0', \
										`flags` INT(11) UNSIGNED DEFAULT NULL, \
										`sourcesync` INT(11) UNSIGNED DEFAULT NULL, \
										PRIMARY KEY (`parentsourcekey`,`sourcekey`,`change_type`), \
										UNIQUE KEY `changeid` (`id`), \
										UNIQUE KEY `state` (`parentsourcekey`,`id`) \
	) ENGINE=%s CHARACTER SET utf8mb4;"

#define Z_TABLEDEF_ABCHANGES		"CREATE TABLE `abchanges` ( \
										`id` INT(11) UNSIGNED NOT NULL AUTO_INCREMENT, \
										`sourcekey` VARBINARY(255) NOT NULL, \
										`parentsourcekey` VARBINARY(255) NOT NULL, \
										`change_type` INT(11) UNSIGNED NOT NULL DEFAULT '0', \
										PRIMARY KEY (`parentsourcekey`,`change_type`,`sourcekey`), \
										UNIQUE KEY `changeid` (`id`) \
	) ENGINE=%s CHARACTER SET utf8mb4;"

#define Z_TABLEDEF_SYNCS			"CREATE TABLE `syncs` ( \
										`id` INT(11) UNSIGNED NOT NULL AUTO_INCREMENT, \
										`sourcekey` VARBINARY(64) NOT NULL, \
										`change_id` INT(11) UNSIGNED NOT NULL, \
										`sync_type` INT(11) UNSIGNED NULL, \
										`sync_time` DATETIME NOT NULL, \
										PRIMARY KEY(`id`), \
										KEY `foldersync` (`sourcekey`,`sync_type`), \
										KEY `changes` (`change_id`), \
										KEY `sync_time` (`sync_time`) \
	) ENGINE=%s CHARACTER SET utf8mb4;"

#define Z_TABLEDEFS_SYNCEDMESSAGES	"CREATE TABLE `syncedmessages` ( \
										`sync_id` int(11) unsigned NOT NULL, \
										`change_id` int(11) unsigned NOT NULL, \
										`sourcekey` varbinary(64) NOT NULL, \
										`parentsourcekey` varbinary(64) NOT NULL, \
										PRIMARY KEY  (`sync_id`,`change_id`,`sourcekey`), \
										KEY `sync_state` (`sync_id`,`change_id`) \
	) ENGINE=%s CHARACTER SET utf8mb4;"

#define Z_TABLEDEF_INDEXED_PROPERTIES	"CREATE TABLE indexedproperties ( \
											`hierarchyid` int(11) unsigned NOT NULL default '0', \
											`tag` smallint(6) unsigned NOT NULL default '0', \
											`val_binary` varbinary(255), \
											PRIMARY KEY (`hierarchyid`, `tag`), \
											UNIQUE KEY `bin` (`tag`, `val_binary`) \
	) ENGINE=%s CHARACTER SET utf8mb4;"

#define Z_TABLEDEF_SETTINGS		"CREATE TABLE settings ( \
		`name` varchar(185) BINARY NOT NULL, \
										`value` blob NOT NULL, \
										PRIMARY KEY  (`name`) \
	) ENGINE=%s CHARACTER SET utf8mb4;"

// Default mysql table data
#define Z_TABLEDATA_ACL				"INSERT INTO `acl` VALUES (2, 2, 2, 1531), \
										(2, 1, 2, 1531), \
										(1, 1, 2, 1531), \
										(1, 2, 2, 1531);"

#define Z_TABLEDATA_HIERARCHY		"INSERT INTO `hierarchy` VALUES \
										(1, NULL, 1, 0, 2),\
										(2, 1, 3, 0, 2);"

#define Z_TABLEDATA_PROPERTIES		"INSERT INTO `properties` VALUES \
										(1, 12289, 30, NULL, 'Admin store', NULL, NULL, NULL, NULL, NULL, false), \
										(2, 12289, 30, NULL, 'root Admin store', NULL, NULL, NULL, NULL, NULL, false);"

#define Z_TABLEDATA_STORES			"INSERT INTO `stores` VALUES (1, 1, 2, 0, 'SYSTEM', 0, 0x8962ffeffb7b4d639bc5967c4bb58234);"

//1=KOPANO_UID_EVERYONE, 0x30002=DISTLIST_SECURITY
//2=KOPANO_UID_SYSTEM, 0x10001=ACTIVE_USER
#define Z_TABLEDATA_USERS			"INSERT INTO `users` (`id`, `externid`, `objectclass`, `signature`, `company`) VALUES \
										(1, NULL, 0x30002, '', 0), \
										(2, NULL, 0x10001, '', 0);"

#define Z_TABLEDATA_INDEXED_PROPERTIES "INSERT INTO `indexedproperties` VALUES (1, 4095, 0x000000008962ffeffb7b4d639bc5967c4bb5823400000000010000000100000000000000), \
											(2, 4095, 0x000000008962ffeffb7b4d639bc5967c4bb5823400000000030000000200000000000000);"

#define Z_TABLEDATA_SETTINGS "INSERT INTO `settings` VALUES ('source_key_auto_increment' , CHAR(0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00)), \
	('imapseq', '3'), ('charset', 'utf8mb4');"

/*
 * The first population of the SQL tables can use both create-type and
 * update-type operations; %Z_UPDATE_RELEASE_ID specifies the schema
 * version that can be reached with creates only.
 * (This is never less than %Z_UPDATE_LAST.)
 */
#define Z_UPDATE_RELEASE_ID 118

// This is the last update ID always update this to the last ID
#define Z_UPDATE_LAST 118

#endif
