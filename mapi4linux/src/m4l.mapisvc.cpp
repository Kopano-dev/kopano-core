/*
 * Copyright 2005 - 2016 Zarafa and its licensors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License, version 3,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */
#include <kopano/platform.h>
#include <memory>
#include <string>
#include <vector>
#include <dirent.h>
#include <sys/stat.h>
#include <kopano/stringutil.h>
#include "m4l.mapisvc.h"
#include <mapix.h>
#include <mapidefs.h>
#include <mapicode.h>
#include <mapitags.h>
#include <mapiutil.h>
#include <kopano/Util.h>
#include <iostream>
#include <arpa/inet.h>
#include <climits>
#include <kopano/UnixUtil.h>

// linux version of PR_SERVICE_DLL_NAME
#define PR_SERVICE_SO_NAME                         PROP_TAG( PT_TSTRING,   0x3D13)
#define PR_SERVICE_SO_NAME_W                       PROP_TAG( PT_UNICODE,   0x3D13)
#define PR_SERVICE_SO_NAME_A                       PROP_TAG( PT_STRING8,   0x3D13)

using namespace KC;
using std::cerr;
using std::cout;
using std::endl;

INFLoader::INFLoader()
{
	// only the properties used in mapisvc.inf
	m_mapDefs["PR_AB_PROVIDER_ID"] = PR_AB_PROVIDER_ID;			// HEX2BIN
	m_mapDefs["PR_DISPLAY_NAME"] = PR_DISPLAY_NAME_A;			// STRING8
	m_mapDefs["PR_MDB_PROVIDER"] = PR_MDB_PROVIDER;				// HEX2BIN
	m_mapDefs["PR_PROVIDER_DISPLAY"] = PR_PROVIDER_DISPLAY_A;	// STRING8
	m_mapDefs["PR_RESOURCE_FLAGS"] = PR_RESOURCE_FLAGS;			// ULONG
	m_mapDefs["PR_RESOURCE_TYPE"] = PR_RESOURCE_TYPE;			// ULONG
	m_mapDefs["PR_SERVICE_DLL_NAME"] = PR_SERVICE_DLL_NAME_A;	// STRING8
	m_mapDefs["PR_SERVICE_SO_NAME"] = PR_SERVICE_SO_NAME_A;		// STRING8, custom property
	m_mapDefs["PR_SERVICE_ENTRY_NAME"] = PR_SERVICE_ENTRY_NAME; // Always STRING8

	// only the definitions used in mapisvc.inf
	m_mapDefs["SERVICE_SINGLE_COPY"] = SERVICE_SINGLE_COPY;
	m_mapDefs["SERVICE_PRIMARY_IDENTITY"] = SERVICE_PRIMARY_IDENTITY;

	m_mapDefs["MAPI_AB_PROVIDER"] = MAPI_AB_PROVIDER;
	m_mapDefs["MAPI_STORE_PROVIDER"] = MAPI_STORE_PROVIDER;

	m_mapDefs["STATUS_PRIMARY_IDENTITY"] = STATUS_PRIMARY_IDENTITY;
	m_mapDefs["STATUS_DEFAULT_STORE"] = STATUS_DEFAULT_STORE;
	m_mapDefs["STATUS_PRIMARY_STORE"] = STATUS_PRIMARY_STORE;
	m_mapDefs["STATUS_NO_DEFAULT_STORE"] = STATUS_NO_DEFAULT_STORE;
}

/** 
 * Loads all *.inf files in the paths returned by GetINFPaths()
 * 
 * @return MAPI Error code
 */
HRESULT INFLoader::LoadINFs()
{
	for (const auto &path : GetINFPaths()) {
		std::unique_ptr<DIR, fs_deleter> dh(opendir(path.c_str()));
		if (dh == nullptr)
			continue;

		for (const struct dirent *dentry = readdir(dh.get());
		     dentry != nullptr; dentry = readdir(dh.get())) {
			struct stat sb;
			std::string strFilename = path + PATH_SEPARATOR + dentry->d_name;
			if (stat(strFilename.c_str(), &sb) < 0 || !S_ISREG(sb.st_mode))
				continue;

			auto pos = strFilename.rfind(".inf", strFilename.size(), strlen(".inf"));
			if (pos == std::string::npos || strFilename.size() - pos != strlen(".inf"))
				// silently skip files not ending in pos
				continue;
			auto hr = LoadINF(strFilename.c_str());
			if (hr != hrSuccess)
				return hr;
		}
	}
	return hrSuccess;
}

/** 
 * Read the contents of a "mapisvc.inf" file
 * 
 * @param filename The filename (including path) to load
 *
 * @retval MAPI_E_NOT_FOUND given filename does not exist on disk, or no access to this file (more likely)
 * @return MAPI Error code
 */
#define MAXLINELEN 4096
HRESULT INFLoader::LoadINF(const char *filename)
{
	inf::iterator iSection = m_mapSections.end();
	std::string strName, strValue;

	auto fp = fopen(filename, "r");
	if (fp == nullptr)
		return MAPI_E_NOT_FOUND;

	while (!feof(fp)) {
		char cBuffer[MAXLINELEN] = {0};
		if (!fgets(cBuffer, sizeof(cBuffer), fp))
			continue;
		auto strLine = trim(cBuffer, " \t");
		/* Skip empty lines any lines which start with # */
		if (strLine.empty() || strLine[0] == '#')
 			continue;

		/* Get setting name */
		auto pos = strLine.find('=');
		if (pos != std::string::npos) {
			strName = strLine.substr(0, pos);
			strValue = strLine.substr(pos + 1);
		} else {
			if (strLine[0] == '[') {
				pos = strLine.find(']');
				if (pos == std::string::npos)
					continue;	// skip line
				strName = strLine.substr(1, pos-1);
				auto rv = m_mapSections.emplace(strName, inf_section());
				iSection = rv.first;
			}
			// always continue with next line.
			continue;
		}

		if (iSection == m_mapSections.cend())
			continue;

		// Parse strName in a property, else leave name?
		iSection->second.emplace(trim(strName, " \t\r\n"), trim(strValue, " \t\r\n"));
	}

	if (fp)
		fclose(fp);
	return hrSuccess;
}

/** 
 * Get the inf_section (provider info) for a given section name
 * 
 * @param strSectionName name of the section to find in the inf file
 * 
 * @return corresponding info, or empty inf_section;
 */
const inf_section *INFLoader::GetSection(const std::string &strSectionName) const
{
	inf::const_iterator iSection = m_mapSections.find(strSectionName);
	if (iSection == m_mapSections.cend()) {
		static inf_section empty;
		return &empty;
	}
	return &iSection->second;
}

/** 
 * The filename of the config file to load.
 * 
 * @return path + filename of mapisvc.inf
 */
std::vector<std::string> INFLoader::GetINFPaths()
{
	const char *env = getenv("MAPI_CONFIG_PATH");
	if (env)
		return tokenize(env, ':', true);
	return tokenize(MAPICONFIGDIR, ':', true);
}

/** 
 * Create a SPropValue from 2 strings found in the mapisvc.inf file.
 * 
 * @param[in] strTag The property tag
 * @param[in] strData The data for the property
 * @param[in] base MAPIAllocateMore base pointer
 * @param[in,out] lpProp Already allocated pointer
 * 
 * @return MAPI error code
 */
HRESULT INFLoader::MakeProperty(const std::string& strTag, const std::string& strData, void *base, LPSPropValue lpProp) const
{
	SPropValue sProp;

	memset(&sProp, 0, sizeof(sProp));
	sProp.ulPropTag = DefinitionFromString(strTag, true);
	switch (PROP_TYPE(sProp.ulPropTag)) {
	case PT_LONG:
		// either a definition, or a hexed network order value
		sProp.Value.ul = 0;
		for (const auto &val : tokenize(strData, "| \t"))
			sProp.Value.ul |= DefinitionFromString(val, false);
		break;
	case PT_UNICODE:
		sProp.ulPropTag = CHANGE_PROP_TYPE(sProp.ulPropTag, PT_STRING8);
		/* fallthru */
	case PT_STRING8: {
		auto hr = MAPIAllocateMore(strData.length() + 1, base, (void**)&sProp.Value.lpszA);
		if (hr != hrSuccess)
			return hr;
		strcpy(sProp.Value.lpszA, strData.c_str());
		break;
	}
	case PT_BINARY: {
		auto hr = Util::hex2bin(strData.data(), strData.length(), &sProp.Value.bin.cb, &sProp.Value.bin.lpb, base);
		if (hr != hrSuccess)
			return hr;
		break;
	}
	default:
		return MAPI_E_INVALID_PARAMETER;
	}

	*lpProp = sProp;
	return hrSuccess;
}

/** 
 * Convert a string as C-defined value to the defined value. This can
 * be properties, prop values, or hex values in network order.
 * 
 * @param strDef the string to convert
 * @param bProp strDef is a propvalue or not
 * 
 * @return 
 */
ULONG INFLoader::DefinitionFromString(const std::string& strDef, bool bProp) const
{
	char *end;

	std::map<std::string, unsigned int>::const_iterator i = m_mapDefs.find(strDef);
	if (i != m_mapDefs.cend())
		return i->second;
	// parse strProp as hex
	auto hex = strtoul(strDef.c_str(), &end, 16);
	if (end < strDef.c_str()+strDef.length())
		return bProp ? PR_NULL : 0;
	return (ULONG)ntohl(hex);
}

SVCProvider::~SVCProvider()
{
	MAPIFreeBuffer(m_lpProps);
}

/** 
 * Return the properties of this provider section
 * 
 * @param[out] lpcValues number of properties in lppPropValues
 * @param[out] lppPropValues pointer to internal properties
 */
void SVCProvider::GetProps(ULONG *lpcValues, LPSPropValue *lppPropValues)
{
	*lpcValues = m_cValues;
	*lppPropValues = m_lpProps;
}

HRESULT SVCProvider::Init(const INFLoader& cINF, const inf_section* infProvider)
{
	HRESULT hr = MAPIAllocateBuffer(sizeof(SPropValue) * infProvider->size(),
		reinterpret_cast<void **>(&m_lpProps));
	if (hr != hrSuccess)
		return hr;

	m_cValues = 0;
	for (const auto &sp : *infProvider)
		// add properties to list
		if (cINF.MakeProperty(sp.first, sp.second, m_lpProps, &m_lpProps[m_cValues]) == hrSuccess)
			++m_cValues;
	return hrSuccess;
}

SVCService::~SVCService()
{
#ifndef VALGRIND
	if (m_dl)
		dlclose(m_dl);
#endif
	MAPIFreeBuffer(m_lpProps);
	for (const auto &i : m_sProviders)
		delete i.second;
}

/** 
 * Process a service section from the read inf file. Converts all
 * properties for the section, reads the associated shared library,
 * and find the entry point functions.
 * 
 * @param[in] cINF the INFLoader class which read the mapisvc.inf file
 * @param[in] infService the service section to initialize
 * 
 * @return MAPI Error code
 */
HRESULT SVCService::Init(const INFLoader& cINF, const inf_section* infService)
{
	char filename[PATH_MAX + 1];

	auto hr = MAPIAllocateBuffer(sizeof(SPropValue) * infService->size(), reinterpret_cast<void **>(&m_lpProps));
	if (hr != hrSuccess)
		return hr;

	m_cValues = 0;
	for (const auto &sp : *infService) {
		// convert section to class
		if (sp.first.compare("Providers") == 0) {
			// make new providers list
			// *new function, new loop
			for (const auto &i : tokenize(sp.second, ", \t")) {
				auto infProvider = cINF.GetSection(i);
				auto prov = m_sProviders.emplace(i, new SVCProvider);
				if (!prov.second)
					continue;	// already exists

				prov.first->second->Init(cINF, infProvider);
			}
		} else if (cINF.MakeProperty(sp.first, sp.second, m_lpProps, &m_lpProps[m_cValues]) == hrSuccess) {
			// add properties to list
			++m_cValues;
		}
	}

	// find PR_SERVICE_SO_NAME / PR_SERVICE_DLL_NAME, load library
	auto lpSO = PCpropFindProp(m_lpProps, m_cValues, PR_SERVICE_SO_NAME_A);
	if (!lpSO)
		lpSO = PCpropFindProp(m_lpProps, m_cValues, PR_SERVICE_DLL_NAME_A);
	if (lpSO == NULL)
		return MAPI_E_NOT_FOUND;
	m_dl = dlopen(lpSO->Value.lpszA, RTLD_NOW | RTLD_GLOBAL);
	if (m_dl == nullptr && strchr(lpSO->Value.lpszA, '/') == nullptr) {
		snprintf(filename, PATH_MAX + 1, "%s%c%s", PKGLIBDIR, PATH_SEPARATOR, lpSO->Value.lpszA);
		m_dl = dlopen(filename, RTLD_NOW | RTLD_GLOBAL);
	}
	if (!m_dl) {
		cerr << "Unable to load " << lpSO->Value.lpszA << ": " << dlerror() << endl;
		return MAPI_E_NOT_FOUND;
	}

	// @todo use PR_SERVICE_ENTRY_NAME
	auto cf = reinterpret_cast<void **>(&m_fnMSGServiceEntry);
	*cf = dlsym(m_dl, "MSGServiceEntry");
	if (!m_fnMSGServiceEntry) {
		// compulsary function in provider
		cerr << "Unable to find MSGServiceEntry in " << lpSO->Value.lpszA << ": " << dlerror() << endl;
		return MAPI_E_NOT_FOUND;
	}

	cf = (void**)&m_fnMSProviderInit;
	*cf = dlsym(m_dl, "MSProviderInit");

	cf = (void**)&m_fnABProviderInit;
	*cf = dlsym(m_dl, "ABProviderInit");
	return hrSuccess;
}

/** 
 * Calls the CreateProvider on the given IProviderAdmin object to
 * create all providers of this service in your profile.
 * 
 * @param[in] lpProviderAdmin  IProviderAdmin object where all providers should be created
 * 
 * @return MAPI Error code
 */
HRESULT SVCService::CreateProviders(IProviderAdmin *lpProviderAdmin)
{
	for (const auto &i : m_sProviders) {
		// CreateProvider will find the provider properties itself. the property parameters can be used for other properties.
		HRESULT hr = lpProviderAdmin->CreateProvider(const_cast<TCHAR *>(reinterpret_cast<const TCHAR *>(i.first.c_str())), 0, NULL, 0, 0, NULL);
		if (hr != hrSuccess)
			return hr;
	}
	return hrSuccess;
}

const SPropValue *SVCService::GetProp(ULONG ulPropTag)
{
	return PCpropFindProp(m_lpProps, m_cValues, ulPropTag);
}

SVCProvider* SVCService::GetProvider(const TCHAR *lpszProvider, ULONG ulFlags)
{
	auto i = m_sProviders.find(reinterpret_cast<const char *>(lpszProvider));
	if (i == m_sProviders.cend())
		return NULL;
	return i->second;
}

std::vector<SVCProvider *> SVCService::GetProviders()
{
	std::vector<SVCProvider *> ret;

	for (const auto &i : m_sProviders)
		ret.emplace_back(i.second);
	return ret;
}

SVC_MSGServiceEntry SVCService::MSGServiceEntry()
{
	return m_fnMSGServiceEntry;
}

SVC_MSProviderInit SVCService::MSProviderInit()
{
	return m_fnMSProviderInit;
}

SVC_ABProviderInit SVCService::ABProviderInit()
{
	return m_fnABProviderInit;
}

MAPISVC::~MAPISVC()
{
	for (const auto &i : m_sServices)
		delete i.second;
}

HRESULT MAPISVC::Init()
{
	INFLoader inf;
	auto hr = inf.LoadINFs();
	if (hr != hrSuccess)
		return hr;
	auto infServices = inf.GetSection("Services");
	for (const auto &sp : *infServices) {
		// ZARAFA6, ZCONTACTS
		auto infService = inf.GetSection(sp.first);
		auto i = m_sServices.emplace(sp.first, new SVCService);
		if (!i.second)
			continue;			// already exists

		hr = i.first->second->Init(inf, infService);
		if (hr != hrSuccess) {
			// remove this service provider since it doesn't work
			delete i.first->second;
			m_sServices.erase(i.first);
			hr = hrSuccess;
		}
	}
	return hr;
}

/** 
 * Returns the service class of the requested service
 * 
 * @param[in] lpszService us-ascii service name
 * @param[in] ulFlags unused, could be used for MAPI_UNICODE in lpszService
 * @param lppService 
 * 
 * @return 
 */
HRESULT MAPISVC::GetService(const TCHAR *lpszService, ULONG ulFlags, SVCService **lppService)
{
	auto i = m_sServices.find(reinterpret_cast<const char *>(lpszService));
	if (i == m_sServices.cend())
		return MAPI_E_NOT_FOUND;

	*lppService = i->second;

	return hrSuccess;
}

/** 
 * Finds the service object for a given dll name.
 * 
 * @param[in] lpszDLLName dll name of the service provider
 * @param[out] lppService the service object for the provider, or untouched on error
 * 
 * @return MAPI Error code
 * @retval MAPI_E_NOT_FOUND no service object for the given dll name
 */
HRESULT MAPISVC::GetService(const char *lpszDLLName, SVCService **lppService)
{
	for (const auto &i : m_sServices) {
		const SPropValue *lpDLLName = i.second->GetProp(PR_SERVICE_DLL_NAME_A);
		if (!lpDLLName || !lpDLLName->Value.lpszA)
			continue;
		if (strcmp(lpDLLName->Value.lpszA, lpszDLLName) == 0) {
			*lppService = i.second;
			return hrSuccess;
		}
	}

	return MAPI_E_NOT_FOUND;
}
