/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#pragma once
#include <map>
#include <memory>
#include <vector>
#include <string>
#include <mapidefs.h>
#include <mapispi.h>
#include <kopano/memory.hpp>

typedef std::map<std::string, std::string> inf_section;
typedef std::map<std::string, inf_section> inf_file;

/* MAPI Providers EntryPoint functions */
typedef HRESULT (*SVC_MSGServiceEntry)(HINSTANCE, IMalloc *, IMAPISupport *, ULONG ui_param, ULONG flags, ULONG context, ULONG nprops, const SPropValue *props, IProviderAdmin *, MAPIERROR **errout);
typedef HRESULT (*SVC_MSProviderInit)(HINSTANCE hInstance, LPMALLOC pmalloc, LPALLOCATEBUFFER pfnAllocBuf, LPALLOCATEMORE pfnAllocMore, LPFREEBUFFER pfnFreeBuf, unsigned int flags, unsigned int mapiver, unsigned int *mdbver, IMSProvider **);
typedef HRESULT (*SVC_ABProviderInit)(HINSTANCE hInstance, LPMALLOC lpMalloc, LPALLOCATEBUFFER lpAllocateBuffer, LPALLOCATEMORE lpAllocateMore, LPFREEBUFFER lpFreeBuffer, unsigned int flags, unsigned int mapiver, unsigned int *provver, IABProvider **);

class INFLoader final {
public:
	INFLoader();
	HRESULT LoadINFs();
	const inf_section* GetSection(const std::string& strSectionName) const;
	HRESULT MakeProperty(const std::string& strTag, const std::string& strData, void *base, LPSPropValue lpProp) const;

private:
	std::vector<std::string> GetINFPaths();
	HRESULT LoadINF(const char *filename);
	ULONG DefinitionFromString(const std::string& strDef, bool bProp) const;

	inf_file m_mapSections;
	std::map<std::string, unsigned int> m_mapDefs;
};

class SVCProvider final {
public:
	/* ZARAFA6_ABP, ZARAFA6_MSMDB_private, ZARAFA6_MSMDB_public */
	HRESULT Init(const INFLoader& cINF, const inf_section* infService);
	void GetProps(unsigned int *nvals, const SPropValue **);

private:
	ULONG m_cValues = 0;
	KC::memory_ptr<SPropValue> m_lpProps; /* PR_* tags from file */
};

class SVCService final {
public:
	/* ZARAFA6, ZCONTACTS */
	~SVCService();
	HRESULT Init(const INFLoader& cINF, const inf_section* infService);
	HRESULT CreateProviders(IProviderAdmin *lpProviderAdmin);
	const SPropValue *GetProp(ULONG tag);
	SVCProvider* GetProvider(const TCHAR *name, ULONG flags);
	std::vector<SVCProvider*> GetProviders();
	SVC_MSGServiceEntry MSGServiceEntry();
	/* move to SVCProvider ? */
	SVC_MSProviderInit MSProviderInit();
	SVC_ABProviderInit ABProviderInit();

private:
	DLIB m_dl = nullptr;
	SVC_MSGServiceEntry m_fnMSGServiceEntry = nullptr;
	SVC_MSProviderInit m_fnMSProviderInit = nullptr;
	SVC_ABProviderInit m_fnABProviderInit = nullptr;

	/* PR_* tags from file */
	KC::memory_ptr<SPropValue> m_lpProps;
	ULONG m_cValues = 0;
	std::map<std::string, std::unique_ptr<SVCProvider>> m_sProviders;
};

class MAPISVC final {
public:
	HRESULT Init();
	HRESULT GetService(const TCHAR *service, unsigned int flags, std::shared_ptr<SVCService> &);
	HRESULT GetService(const char *dll_name, std::shared_ptr<SVCService> &);

private:
	std::map<std::string, std::shared_ptr<SVCService>> m_sServices;
};
