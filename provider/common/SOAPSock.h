/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */

#ifndef SOAPSOCK_H
#define SOAPSOCK_H

#include <string>
#include <openssl/ssl.h>

class KCmdProxy;

/* A simpler form of profile props (which are stashed in SPropValues) */
struct sGlobalProfileProps {
	std::string strServerPath, strProfileName;
	std::wstring strUserName, strPassword, strImpersonateUser;
	std::string strSSLKeyFile, strSSLKeyPass;
	std::string strProxyHost;
	std::string strProxyUserName, strProxyPassword;
	std::string strClientAppVersion, strClientAppMisc;
	unsigned int ulProfileFlags = 0, ulConnectionTimeOut = 10;
	unsigned int ulProxyFlags = 0, ulProxyPort = 0;
};

int ssl_verify_callback_kopano_silent(int ok, X509_STORE_CTX *store);
int ssl_verify_callback_kopano(int ok, X509_STORE_CTX *store);
int ssl_verify_callback_kopano_control(int ok, X509_STORE_CTX *store, BOOL bShowDlg);

HRESULT LoadCertificatesFromRegistry();
extern HRESULT CreateSoapTransport(const sGlobalProfileProps &, KCmdProxy **);
extern void DestroySoapTransport(KCmdProxy *lpCmd);

#endif
