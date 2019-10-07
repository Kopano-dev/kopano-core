/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */

/* mapi.h – Defines structures and constants */

#ifndef __M4L_MAPI_H_
#define __M4L_MAPI_H_
#define MAPI_H

#include <kopano/platform.h>

struct MapiFileDesc {
	unsigned int ulReserved, flFlags, nPosition;
	char *lpszPathName, *lpszFileName;
    LPVOID lpFileType;
};
typedef struct MapiFileDesc *lpMapiFileDesc;

struct MapiFileTagExt {
	unsigned int ulReserved, cbTag;
    LPBYTE lpTag;
    ULONG cbEncoding;
    LPBYTE lpEncoding;
};
typedef struct MapiFileTagExt *lpMapiFileTagExt;

#define MAPI_ORIG	0x00000000
#define MAPI_TO		0x00000001
#define MAPI_CC		0x00000002
#define MAPI_BCC	0x00000003
/* from mapidefs.h */
#define MAPI_P1		0x10000000
#define MAPI_SUBMITTED	0x80000000
/* #define MAPI_AUTHORIZE	0x00000004 */
/* #define MAPI_DISCRETE	0x10000000 */
struct MapiRecepDesc {
    ULONG ulReserved;
    ULONG ulRecipClass;		/* MAPI_TO, MAPI_CC, MAPI_BCC, MAPI_ORIG    */
	char *lpszName, *lpszAddress;
    ULONG ulEIDSize;
    LPVOID lpEntryID;
};
typedef struct MapiRecipDesc *lpMapiRecipDesc;

#define MAPI_UNREAD             0x00000001
#define MAPI_RECEIPT_REQUESTED  0x00000002
#define MAPI_SENT               0x00000004
struct MapiMessage {
    ULONG ulReserved;
    LPSTR lpszSubject;
    LPSTR lpszNoteText;
    LPSTR lpszMessageType;	/* MAPI_UNREAD, MAPI_RECEIPT_REQUESTED, MAPI_SENT */
    LPSTR lpszDateReceived;
    LPSTR lpszConversationID;
    FLAGS flFlags;
    lpMapiRecipDesc lpOriginator;
    ULONG nRecipCount;
    lpMapiRecipDesc lpRecips;
    ULONG nFileCount;
    lpMapiFileDesc lpFiles;
};
typedef struct MapiMessage *lpMapiMessage;

/* flFlags for MAPI* Function calls */
#define MAPI_LOGON_UI           0x00000001
#define MAPI_PASSWORD_UI        0x00020000

#define MAPI_NEW_SESSION        0x00000002
#define MAPI_FORCE_DOWNLOAD     0x00001000
#define MAPI_EXTENDED           0x00000020

#define MAPI_DIALOG             0x00000008
#define MAPI_USE_DEFAULT	0x00000040

#define MAPI_UNREAD_ONLY        0x00000020
#define MAPI_GUARANTEE_FIFO     0x00000100
#define MAPI_LONG_MSGID         0x00004000

#define MAPI_PEEK               0x00000080
#define MAPI_SUPPRESS_ATTACH    0x00000800
#define MAPI_ENVELOPE_ONLY      0x00000040
#define MAPI_BODY_AS_FILE       0x00000200
#define MAPI_AB_NOMODIFY        0x00000400

extern "C" {

/* Function definitions */
extern ULONG MAPILogon(ULONG_PTR ulUIParam, const char *lpszProfileName, const char *lpszPassword, FLAGS flFlags, ULONG ulReserved, LPLHANDLE lplhSession);
extern ULONG MAPILogoff(LHANDLE lhSession, ULONG_PTR ulUIParam, FLAGS flFlags, ULONG ulReserved);

/* Simple MAPI functions */
extern ULONG MAPISendMail(LHANDLE lhSession, ULONG_PTR ulUIParam, lpMapiMessage lpMessage, FLAGS flFlags, ULONG ulReserved);
extern ULONG MAPISendDocuments(ULONG_PTR ulUIParam, const char *lpszDelimChar, const char *lpszFilePaths, const char *lpszFileNames, ULONG ulReserved);
extern ULONG MAPIFindNext(LHANDLE lhSession, ULONG_PTR ulUIParam, const char *lpszMessageType, const char *lpszSeedMessageID, FLAGS flFlags, ULONG ulReserved, const char *lpszMessageID);
extern ULONG MAPIReadMail(LHANDLE lhSession, ULONG_PTR ulUIParam, const char *lpszMessageID, FLAGS flFlags, ULONG ulReserved, lpMapiMessage *lppMessage);
extern ULONG MAPISaveMail(LHANDLE lhSession, ULONG_PTR ulUIParam, lpMapiMessage lpMessage, FLAGS flFlags, ULONG ulReserved, const char *lpszMessageID);
extern ULONG MAPIDeleteMail(LHANDLE lhSession, ULONG_PTR ulUIParam, const char *lpszMessageID, FLAGS flFlags, ULONG ulReserved);
ULONG MAPIFreeBuffer(
    LPVOID pv
);
extern ULONG MAPIAddress(LHANDLE lhSession, ULONG_PTR ulUIParam, const char *lpszCaption, ULONG nEditFields, const char *lpszLabels, ULONG nRecips, lpMapiRecipDesc lpRecips, FLAGS flFlags, ULONG ulReserved, LPULONG lpnNewRecips, lpMapiRecipDesc *lppNewRecips);
extern ULONG MAPIDetails(LHANDLE lhSession, ULONG_PTR ulUIParam, lpMapiRecipDesc lpRecip, FLAGS flFlags, ULONG ulReserved);
extern ULONG MAPIResolveName(LHANDLE lhSession, ULONG_PTR ulUIParam, const char *lpszName, FLAGS flFlags, ULONG ulReserved, lpMapiRecipDesc *lppRecip);

/* Return codes */
#define SUCCESS_SUCCESS                 0
#define MAPI_USER_ABORT                 1
#define MAPI_E_USER_ABORT               MAPI_USER_ABORT
#define MAPI_E_FAILURE                  2
#define MAPI_E_LOGON_FAILURE            3
//#define MAPI_E_LOGIN_FAILURE            MAPI_E_LOGON_FAILURE
#define MAPI_E_DISK_FULL                4
#define MAPI_E_INSUFFICIENT_MEMORY      5
#define MAPI_E_ACCESS_DENIED            6
#define MAPI_E_TOO_MANY_SESSIONS        8
#define MAPI_E_TOO_MANY_FILES           9
#define MAPI_E_TOO_MANY_RECIPIENTS      10
#define MAPI_E_ATTACHMENT_NOT_FOUND     11
#define MAPI_E_ATTACHMENT_OPEN_FAILURE  12
#define MAPI_E_ATTACHMENT_WRITE_FAILURE 13
#define MAPI_E_UNKNOWN_RECIPIENT        14
#define MAPI_E_BAD_RECIPTYPE            15
#define MAPI_E_NO_MESSAGES              16
#define MAPI_E_INVALID_MESSAGE          17
#define MAPI_E_TEXT_TOO_LARGE           18
#define MAPI_E_INVALID_SESSION          19
#define MAPI_E_TYPE_NOT_SUPPORTED       20
#define MAPI_E_AMBIGUOUS_RECIPIENT      21
//#define MAPI_E_AMBIG_RECIP              MAPI_E_AMBIGUOUS_RECIPIENT
#define MAPI_E_MESSAGE_IN_USE           22
#define MAPI_E_NETWORK_FAILURE          23
#define MAPI_E_INVALID_EDITFIELDS       24
#define MAPI_E_INVALID_RECIPS           25
#define MAPI_E_NOT_SUPPORTED            26

} // EXTERN "C"

#endif /* __M4L_MAPI_H_ */
