/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */

#ifndef SOAPSOCK_H
#define SOAPSOCK_H

#include <openssl/ssl.h>

class KCmdProxy;

int ssl_verify_callback_kopano_silent(int ok, X509_STORE_CTX *store);
int ssl_verify_callback_kopano(int ok, X509_STORE_CTX *store);
int ssl_verify_callback_kopano_control(int ok, X509_STORE_CTX *store, BOOL bShowDlg);

HRESULT LoadCertificatesFromRegistry();

HRESULT CreateSoapTransport(ULONG ulUIFlags,
	const char *strServerPath,
	const char *strSSLKeyFile,
	const char *strSSLKeyPass,
	ULONG ulConnectionTimeOut,
	const char *strProxyHost,
	WORD wProxyPort,
	const char *strProxyUserName,
	const char *strProxyPassword,
	ULONG ulProxyFlags,
	int				iSoapiMode,
	int				iSoapoMode,
	KCmdProxy **lppCmd);

extern void DestroySoapTransport(KCmdProxy *lpCmd);

#endif
