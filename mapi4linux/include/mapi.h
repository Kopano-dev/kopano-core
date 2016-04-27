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

#ifndef __M4L_MAPI_H_
#define __M4L_MAPI_H_
#define MAPI_H

#include <kopano/platform.h>

/* seems unused? */
#define    lhSessionNull    ((LHANDLE)0)

/* seems unused? */
/* used by attachments? */
typedef struct _s_MapiFileDesc {
    ULONG ulReserved;
    ULONG flFlags;
    ULONG nPosition;
    LPSTR lpszPathName;
    LPSTR lpszFileName;
    LPVOID lpFileType;
} MapiFileDesc, *lpMapiFileDesc;

typedef struct _s_MapiFileTagExt {
    ULONG ulReserved;
    ULONG cbTag;
    LPBYTE lpTag;
    ULONG cbEncoding;
    LPBYTE lpEncoding;
} MapiFileTagExt, *lpMapiFileTagExt;


#define MAPI_ORIG	0x00000000
#define MAPI_TO		0x00000001
#define MAPI_CC		0x00000002
#define MAPI_BCC	0x00000003
/* from mapidefs.h */
#define MAPI_P1		0x10000000
#define MAPI_SUBMITTED	0x80000000
/* #define MAPI_AUTHORIZE	0x00000004 */
/* #define MAPI_DISCRETE	0x10000000 */
typedef struct _s_MapiRecepDesc {
    ULONG ulReserved;
    ULONG ulRecipClass;		/* MAPI_TO, MAPI_CC, MAPI_BCC, MAPI_ORIG    */
    LPSTR lpszName;
    LPSTR lpszAddress;
    ULONG ulEIDSize;
    LPVOID lpEntryID;
} MapiRecipDesc, *lpMapiRecipDesc;


#define MAPI_UNREAD             0x00000001
#define MAPI_RECEIPT_REQUESTED  0x00000002
#define MAPI_SENT               0x00000004
typedef struct _s_MapiMessage {
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
} MapiMessage, *lpMapiMessage;


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
ULONG MAPILogon(
    ULONG ulUIParam,
    LPSTR lpszProfileName,
    LPSTR lpszPassword,
    FLAGS flFlags,
    ULONG ulReserved,
    LPLHANDLE lplhSession
);

ULONG MAPILogoff(
    LHANDLE lhSession,
    ULONG ulUIParam,
    FLAGS flFlags,
    ULONG ulReserved
);



/* Simple MAPI functions */
ULONG MAPISendMail(
    LHANDLE lhSession,
    ULONG ulUIParam,
    lpMapiMessage lpMessage,
    FLAGS flFlags,
    ULONG ulReserved
);

ULONG MAPISendDocuments(
    ULONG ulUIParam,
    LPSTR lpszDelimChar,
    LPSTR lpszFilePaths,
    LPSTR lpszFileNames,
    ULONG ulReserved
);

ULONG MAPIFindNext(
    LHANDLE lhSession,
    ULONG ulUIParam,
    LPSTR lpszMessageType,
    LPSTR lpszSeedMessageID,
    FLAGS flFlags,
    ULONG ulReserved,
    LPSTR lpszMessageID
);

ULONG MAPIReadMail(
    LHANDLE lhSession,
    ULONG ulUIParam,
    LPSTR lpszMessageID,
    FLAGS flFlags,
    ULONG ulReserved,
    lpMapiMessage *lppMessage
);

ULONG MAPISaveMail(
    LHANDLE lhSession,
    ULONG ulUIParam,
    lpMapiMessage lpMessage,
    FLAGS flFlags,
    ULONG ulReserved,
    LPSTR lpszMessageID
);

ULONG MAPIDeleteMail(
    LHANDLE lhSession,
    ULONG ulUIParam,
    LPSTR lpszMessageID,
    FLAGS flFlags,
    ULONG ulReserved
);

ULONG MAPIFreeBuffer(
    LPVOID pv
);

ULONG MAPIAddress(
    LHANDLE lhSession,
    ULONG ulUIParam,
    LPSTR lpszCaption,
    ULONG nEditFields,
    LPSTR lpszLabels,
    ULONG nRecips,
    lpMapiRecipDesc lpRecips,
    FLAGS flFlags,
    ULONG ulReserved,
    LPULONG lpnNewRecips,
    lpMapiRecipDesc *lppNewRecips
);

ULONG MAPIDetails(
    LHANDLE lhSession,
    ULONG ulUIParam,
    lpMapiRecipDesc lpRecip,
    FLAGS flFlags,
    ULONG ulReserved
);

ULONG MAPIResolveName(
    LHANDLE lhSession,
    ULONG ulUIParam,
    LPSTR lpszName,
    FLAGS flFlags,
    ULONG ulReserved,
    lpMapiRecipDesc *lppRecip
);

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
