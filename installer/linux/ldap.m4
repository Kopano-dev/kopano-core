dnl m4 defines:
dnl    TYPE == OPENLDAP or ADS
dnl
define(`S', `ifelse(TYPE,`OPENLDAP',$1,$2)')
##############################################################
#  LDAP/ACTIVE DIRECTORY USER PLUGIN SETTINGS
#
# Any of these directives that are required, are only required if the
# userplugin parameter is set to ldap.

# When an object (user/group/company) is changed, this attribute will also change:
# Active directory: uSNChanged
# LDAP: modifyTimestamp
ldap_last_modification_attribute = S(`modifyTimestamp',`uSNChanged')

##########
# Object settings

# attribute name which is/(should: was) used in ldap_user_search_filter
ldap_object_type_attribute = objectClass
ldap_user_type_attribute_value = S(`posixAccount',`user')
ldap_group_type_attribute_value = S(`posixGroup',`group')
ldap_contact_type_attribute_value = S(`kopano-contact',`contact')
ldap_company_type_attribute_value = organizationalUnit
ldap_addresslist_type_attribute_value = S(`kopano-addresslist',`kopanoAddresslist')
ldap_dynamicgroup_type_attribute_value = S(`kopano-dynamicgroup',`kopanoDynamicGroup')
ldap_server_type_attribute_value = S(`ipHost',`computer')

##########
# There should be no need to edit any values below this line
##########

##########
# User settings

# Extra search for users using this LDAP filter.  See ldap_search(3) or RFC
# 2254 for details on the filter syntax.
#
# Hint: Use the kopanoAccount attribute in the filter to differentiate
# between non-kopano and kopano users.
#
# Note: This filter should include contacts.
#
# Optional, default = empty (match everything)
# For active directory, use:
#   (objectCategory=Person)
# For LDAP with posix users:
#   no need to use the search filter.
ldap_user_search_filter = S(`',`(objectCategory=Person)')

# unique user id for find the user
# Required
# For active directory, use:
#    objectGUID ** WARNING: This WAS: objectSid ** Updates *WILL* fail! **
# For LDAP with posixAccount, use:
#    uidNumber
ifelse(TYPE,`OPENLDAP',`dnl
# Note: contacts also use this field for uniqueness. If you change this,
# you might need to update the kopano.schema file too, and change
# the MUST uidNumber to whatever you set here.')dnl
ldap_user_unique_attribute = S(`uidNumber',`objectGUID')

# Type of unique user id
# default: text
# For active directory, use:
#		binary
# For LDAP with posix user, use:
#		text
ldap_user_unique_attribute_type = S(`text',`binary')

# Optional, default = cn
# For active directory, use:
#   cn or displayName
# For LDAP with posix user, use:
#   cn
ldap_fullname_attribute = S(`cn',`cn')

# Optional, default = uid
# Active directory: sAMAccountName
# LDAP: uid
ldap_loginname_attribute = S(`uid',`sAMAccountName')

# Optional, default = userPassword
# Active directory: unicodePwd
# LDAP: userPassword
ldap_password_attribute = S(`userPassword',`unicodePwd')

# If set to bind, users are authenticated by trying to bind to the
# LDAP tree using their username + password.  Otherwise, the
# ldap_password_attribute is requested and checked.
# Optional, default = bind
# Choices: bind, password
# Active directory: bind
# LDAP: bind
ldap_authentication_method = bind

# Optional, default = mail
# Active directory: mail
# LDAP: mail
ldap_emailaddress_attribute = mail

# Optional, default = kopanoAliases
# Active directory: kopanoAliases
# LDAP: kopanoAliases
ldap_emailaliases_attribute = S(`kopanoAliases',`otherMailbox')

# Whether the user is an admin.  The field is interpreted as a
# boolean, 0 and false (case insensitive) meaning no, all other values
# yes.
# Optional, default = kopanoAdmin
# Active directory: kopanoAdmin
# LDAP: kopanoAdmin
ldap_isadmin_attribute = kopanoAdmin

# Whether a user is a non-active user. This means that the user will
# not count towards your user count, but the user will also not be
# able to log in
# Optional, default = kopanoSharedStoreOnly
# Active directory: kopanoSharedStoreOnly
# LDAP: kopanoSharedStoreOnly
ldap_nonactive_attribute = kopanoSharedStoreOnly

# A nonactive store, or resource, can be specified to be a user, room or equipment.
# Set it to 'room' or 'equipment' to make such types. If set to empty,
# or wrong word, or 'user' it will be a nonactive user.
# Optional, default = kopanoResourceType
# Active directory: kopanoResourceType
# LDAP: kopanoResourceType
ldap_resource_type_attribute = kopanoResourceType

# Numeric resource capacity
# Optional, default = kopanoResourceCapacity
# Active directory: kopanoResourceCapacity
# LDAP: kopanoResourceCapacity
ldap_resource_capacity_attribute = kopanoResourceCapacity

# Optional
# The attribute which indicates which users are allowed
# to send on behalf of the selected user
ldap_sendas_attribute = kopanoSendAsPrivilege

# Optional, default = text
# Active directory: dn
# LDAP: text
ldap_sendas_attribute_type = S(`text',`dn')

# The attribute of the user and group which is listed in 
# the ldap_sendas_attribute
# Empty default, using ldap_user_unique_attribute
ldap_sendas_relation_attribute = S(`',`distinguishedName')

# Optional, default = userCertificate
# Active directory: userCertificate
# LDAP: userCertificate;binary
ldap_user_certificate_attribute = userCertificate`'S(`;binary',`')

# Load extra user properties from the propmap file
!propmap /usr/share/kopano/ldap.propmap.cfg

##########
# Group settings

# Search for groups using this LDAP filter.  See ldap_search(3) for
# details on the filter syntax.
# Hint: Use the kopanoAccount attribute in the filter to differentiate
# between non-kopano and kopano groups.
# Optional, default = empty (match everything)
# For active directory, use:
#   (objectCategory=Group)
# For LDAP with posix groups, use:
#   no need to set the search filter
ldap_group_search_filter = S(`',`(objectCategory=Group)')

# unique group id for find the group
# Required
# For active directory, use:
#    objectSid
# For LDAP with posix group, use:
#    gidNumber
ldap_group_unique_attribute = S(`gidNumber',`objectSid')

# Type of unique group id
# default: text
# For active directory, use:
#		binary
# For LDAP with posix group, use:
#		text
ldap_group_unique_attribute_type = S(`text',`binary')

# Optional, default = cn
# Active directory: cn
# LDAP: cn
ldap_groupname_attribute = cn

# Optional, default = member
# Active directory: member
# LDAP: memberUid
ldap_groupmembers_attribute = S(`memberUid',`member')

# Optional, default = text
# Active directory: dn
# LDAP: text
ldap_groupmembers_attribute_type = S(`text',`dn')

# The attribute of the user which is listed in ldap_groupmember_attribute
# Active directory: empty, matching DNs
# LDAP: uid, matching users in ldap_loginname_attribute
ldap_groupmembers_relation_attribute = S(`uid',`')

# A group can also be used for security, e.g. setting permissions on folders.
# This makes a group a security group. The kopanoSecurityGroup value is boolean.
# Optional, default = kopanoSecurityGroup
# Active directory = groupType
# LDAP: kopanoSecurityGroup
ldap_group_security_attribute = S(`kopanoSecurityGroup',`groupType')

# In ADS servers, a special bitmask action is required on the groupType field.
# This is actived by setting the ldap_group_security_attribute_type to `''ads`''
# Otherwise, just the presence of the field will make the group security enabled.
# Optional, default = boolean
# Active directory = ads
# LDAP: boolean
ldap_group_security_attribute_type = S(`boolean',`ads')

##########
# Company settings

# Search for companies using this LDAP filter.
# Hint: Use the kopanoAccount attribute in the filter to differentiate
# between non-kopano and kopano companies.
# Optional, default = empty (match everything)
# For active directory, use:
#   (objectCategory=Company)
# For LDAP with posix users, use:
#   no need to set the filter
ldap_company_search_filter =

# unique company id for find the company
# Active directory: objectGUID
# LDAP: ou
ldap_company_unique_attribute = S(`ou',`objectGUID')

# Optional, default = text
# Active directory: binary
# LDAP: text
ldap_company_unique_attribute_type = S(`text',`binary')

# Optional, default = ou
# Active directory: ou
# LDAP: ou
ldap_companyname_attribute = ou

# Optional
# The attribute which indicates which companies are allowed
# to view the members of the selected company
ldap_company_view_attribute = kopanoViewPrivilege

# Optional, default = text
ldap_company_view_attribute_type = S(`text',`dn')

# The attribute of the company which is listed in the
# ldap_company_view_attribute
# Empty default, using ldap_company_unique_attribute
ldap_company_view_relation_attribute =

# Optional
# The attribute which indicates which users from different companies
# are administrator over the selected company.
ldap_company_admin_attribute = kopanoAdminPrivilege

# Optional, default = text
# Active directory: dn
# LDAP: text
ldap_company_admin_attribute_type = S(`text',`dn')

# The attribute of the company which is listed in the
# ldap_company_admin_attribute
# Empty default, using ldap_user_unique_attribute
ldap_company_admin_relation_attribute = 

# The attribute which indicates which user is the system administrator
# for the specified company.
ldap_company_system_admin_attribute = kopanoSystemAdmin

# Optional, default = text
# Active directory: dn
# LDAP: text
ldap_company_system_admin_attribute_type = S(`text',`dn')

# The attribute of the company which is listed in the
# ldap_company_system_admin attribute
# Empty default, using ldap_user_unique_attribute
ldap_company_system_admin_relation_attribute =


##########
# Addresslist settings

# Add a filter to the addresslist search
# Hint: Use the kopanoAccount attribute in the filter to differentiate
# between non-kopano and kopano addresslists.
# Optional, default = empty (match everything)
ldap_addresslist_search_filter = 

# This is the unique attribute of a addresslist which is never going
# to change, unless the addresslist is removed from LDAP. When this
# value changes, Kopano will remove the previous addresslist from the
# database, and create a new addresslist with this unique value
ldap_addresslist_unique_attribute = cn

# This value can be 'text' or 'binary'. For OpenLDAP, only text is used.
ldap_addresslist_unique_attribute_type = text

# This is the name of the attribute on the addresslist object that
# specifies the filter to be applied for this addresslist. All users
# matching this filter AND matching the default
# ldap_user_search_filter will be included in the addresslist
ldap_addresslist_filter_attribute = kopanoFilter

# This is the name of the attribute on the addresslist object that
# specifies the search base to be applied for this addresslist.
ldap_addresslist_search_base_attribute = kopanoBase

# The attribute containing the name of the addresslist
ldap_addresslist_name_attribute = cn


##########
# Dynamicgroup settings

# Add a filter to the dynamicgroup search
# Hint: Use the kopanoAccount attribute in the filter to differentiate
# between non-kopano and kopano dynamic groups.
# Optional, default = empty (match everything)
ldap_dynamicgroup_search_filter = 

# This is the unique attribute of a dynamicgroup which is never going
# to change, unless the dynamicgroup is removed from LDAP. When this
# value changes, Kopano will remove the previous dynamicgroup from the
# database, and create a new dynamicgroup with this unique value
ldap_dynamicgroup_unique_attribute = cn

# This value can be 'text' or 'binary'. For OpenLDAP, only text is used.
ldap_dynamicgroup_unique_attribute_type = text

# This is the name of the attribute on the dynamicgroup object that
# specifies the filter to be applied for this dynamicgroup. All users
# matching this filter AND matching the default
# ldap_user_search_filter will be included in the dynamicgroup
ldap_dynamicgroup_filter_attribute = kopanoFilter

# This is the name of the attribute on the dynamicgroup object that
# specifies the search base to be applied for this dynamicgroup.
ldap_dynamicgroup_search_base_attribute = kopanoBase

# The attribute containing the name of the dynamicgroup
ldap_dynamicgroup_name_attribute = cn


##########
# Quota settings

# Optional
# The attribute which indicates which users (besides the user who exceeds his quota)
# should also receive a warning mail when a user exceeds his quota.
ldap_quota_userwarning_recipients_attribute = kopanoQuotaUserWarningRecipients

# Optional, default = text
# Active directory: dn
# LDAP: text
ldap_quota_userwarning_recipients_attribute_type = text

# Optional, default empty
ldap_quota_userwarning_recipients_relation_attribute =

# Optional
# The attribute which indicates which users should receive a warning mail
# when a company exceeds his quota.
ldap_quota_companywarning_recipients_attribute = kopanoQuotaCompanyWarningRecipients

# Optional, default = text
# Active directory: dn
# LDAP: text
ldap_quota_companywarning_recipients_attribute_type = text

# Optional, default empty
ldap_quota_companywarning_recipients_relation_attribute =

# Whether to override the system wide quota settings
ldap_quotaoverride_attribute = kopanoQuotaOverride

ldap_warnquota_attribute = kopanoQuotaWarn
ldap_softquota_attribute = kopanoQuotaSoft
ldap_hardquota_attribute = kopanoQuotaHard

# Whether to override the system wide quota settings for all users within the company
ldap_userdefault_quotaoverride_attribute = kopanoUserDefaultQuotaOverride

ldap_userdefault_warnquota_attribute = kopanoUserDefaultQuotaWarn
ldap_userdefault_softquota_attribute = kopanoUserDefaultQuotaSoft
ldap_userdefault_hardquota_attribute = kopanoUserDefaultQuotaHard

# Mapping from the quota attributes to a number of bytes.  Qmail-LDAP
# schema uses bytes (1), ADS uses kilobytes (1024*1024).
ldap_quota_multiplier = S(`1',`1048576')

##########
# Misc. settings

# Attribute which indicates if the user should be hidden from addressbook
ldap_addressbook_hide_attribute = kopanoHidden 

# LDAP object search filter. %s in this filter will be replaced with
# the object being searched.
# Hint: Use the kopanoAccount attribute in the filter to differentiate
# between non-kopano and kopano objects.
# Default: empty
# ADS recommended: (anr=%s)
# OpenLDAP optional: (|(mail=%s*)(uid=%s*)(givenName=*%s*)(sn=*%s*))
ldap_object_search_filter = S(`(|(mail=*%s*)(givenName=*%s*)(sn=*%s*))',`(anr=%s)')

# If a request want more objects than this value, it will download the
# full ldap tree (from the base with the search filter) and discard
# wat was not required. This is faster for large requests.
# Default: 1000
ldap_filter_cutoff_elements = 1000

##########
# Multi-server settings

# Users will be created on this named server
# Optional, default kopanoUserServer
ldap_user_server_attribute = kopanoUserServer

# The public store of the company will be created on this named server
# Optional, default kopanoCompanyServer
ldap_company_server_attribute = kopanoCompanyServer

# Optional
# Active directory: kopanoHostAddress
# LDAP: ipHostNumber
ldap_server_address_attribute = S(`ipHostNumber',`kopanoHostAddress')

# Optional, default = kopanoHttpPort
# Active directory: kopanoHttpPort
# LDAP: kopanoHttpPort
ldap_server_http_port_attribute = kopanoHttpPort

# Optional, default = kopanoSslPort
# Active directory: kopanoSslPort
# LDAP: kopanoSslPort
ldap_server_ssl_port_attribute = kopanoSslPort

# Optional, default = kopanoFilePath
# Active directory: kopanoFilePath
#LDAP: kopanoFilePath
ldap_server_file_path_attribute = kopanoFilePath

# Determines if a server contains the public store of a non-hosted
# environment. Only one server is allowed to host the public store.
# Optional, default = kopanoContainsPublic
# Active directory: kopanoContainsPublic
# LDAP: kopanoContainsPublic
ldap_server_contains_public_attribute = kopanoContainsPublic

# The Proxy URL of the node; the node must be available to clients
# using this Proxy URL if the server detects that original connection
# was received via a proxy. See server.cfg(5)'s proxy_header setting
ldap_server_proxy_path_attribute = kopanoProxyURL

# Search for servers using this LDAP filter.  See ldap_search(3) or RFC
# 2254 for details on the filter syntax.
# Optional, default = empty (match everything)
# For active directory, use:
#   (objectCategory=Computer)
# For LDAP with posix users, use:
#   
ldap_server_search_filter = S(`',`(objectCategory=Computer)')

# Unique user id to find the server
# Required
# For active directory, use:
#    CN
# For LDAP with posixAccount, use:
#    cn
ldap_server_unique_attribute = cn
