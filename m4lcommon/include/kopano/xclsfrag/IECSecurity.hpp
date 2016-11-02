virtual HRESULT GetOwner(ULONG *lpcbOwner, LPENTRYID *lppOwner) _kc_override;
virtual HRESULT GetPermissionRules(int ulType, ULONG *lpcPermissions, ECPERMISSION **lppECPermissions) _kc_override;
virtual HRESULT SetPermissionRules(ULONG cPermissions, ECPERMISSION *lpECPermissions) _kc_override;
virtual HRESULT GetUserList(ULONG cbCompanyId, LPENTRYID lpCompanyId, ULONG flags, ULONG *lpcUsers, ECUSER **lppsUsers) _kc_override;
virtual HRESULT GetGroupList(ULONG cbCompanyId, LPENTRYID lpCompanyId, ULONG flags, ULONG *lpcGroups, ECGROUP **lppsGroups) _kc_override;
virtual HRESULT GetCompanyList(ULONG flags, ULONG *lpcCompanies, ECCOMPANY **lppCompanies) _kc_override;
