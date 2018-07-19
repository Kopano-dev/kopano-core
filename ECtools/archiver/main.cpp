/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */

#include <kopano/platform.h>
#include <iostream>
#include <memory>
#include <sstream>
#include <climits>
#include <getopt.h>
#include <kopano/stringutil.h>
#include <kopano/archiver-common.h>
#include <kopano/charset/convert.h>
#include <kopano/ECConfig.h>
#include <kopano/ECLogger.h>
#include <kopano/ecversion.h>
#include <kopano/UnixUtil.h>
#include "Archiver.h"

namespace KC {

using std::cerr;
using std::cout;
using std::endl;

enum modes {
    MODE_INVALID = 0,
    MODE_ATTACH,
    MODE_DETACH,
    MODE_DETACH_IDX,
    MODE_LIST,
    MODE_LIST_ARCHUSER,
    MODE_ARCHIVE,
    MODE_CLEANUP,
    MODE_AUTO_ATTACH,
	MODE_TEST,
};

static const char *modename(modes mode)
{
    switch (mode) {
	case MODE_INVALID: return "Invalid mode";
	case MODE_ATTACH: return "Attach";
	case MODE_DETACH: return "Detach";
	case MODE_DETACH_IDX: return "Detach by index";
	case MODE_LIST: return "List";
	case MODE_LIST_ARCHUSER: return "List archive users";
	case MODE_ARCHIVE: return "Archive";
	case MODE_CLEANUP: return "Clean-up";
	case MODE_AUTO_ATTACH: return "Auto attach";
	default: return "Undefined mode";
    }
}

/**
 * Print a help message.
 *
 * @param[in]	ostr
 *					std::ostream where the message is written to.
 * @param[in]	lpszName
 *					The name of the application to use in the output.
 */
static void print_help(std::ostream &ostr, const char *lpszName)
{
    ostr << endl;
    ostr << "Usage:" << endl;
    ostr << lpszName << " [options]" << endl << endl;
    ostr << "Options:" << endl;
    ostr << "  -u <name>                        : Select user" << endl;
    ostr << "  -l|--list                        : List archives for the specified user" << endl;
    ostr << "  -L|--list-archiveusers           : List users that have an archived attached" << endl;
    ostr << "  -A|--archive                     : Perform archive operation" << endl;
    ostr << "                                     If no user is specified all user stores will" << endl;
    ostr << "                                     be archived." << endl;
    ostr << "  -C|--cleanup                     : Perform a cleanup of the archive stores attached" << endl;
    ostr << "                                     to the user specified with -u. If no user is" << endl;
    ostr << "                                     specified, all archives are cleanedup." << endl;
    ostr << "     --local-only                  : Archive or cleanup only those users that have" << endl;
    ostr << "                                     their store on the server on which the archiver" << endl;
    ostr << "                                     is invoked." << endl;
    ostr << "  -a|--attach-to <archive store>   : Attach an archive to the specified user." << endl;
    ostr << "                                     By default a subfolder will be created with" << endl;
    ostr << "                                     the same name as the specified user. This" << endl;
    ostr << "                                     folder will be the root of the archive." << endl;
    ostr << "  -d|--detach-from <archive store> : Detach an archive from the specified user." << endl;
    ostr << "                                     If a user has multiple archives in the same" << endl;
    ostr << "                                     archive store, the folder needs to be" << endl;
    ostr << "                                     specified with --archive-folder." << endl;
    ostr << "  -D|--detach <archive no>         : Detach the archive specified by archive no. This" << endl;
    ostr << "                                     number can be found by running kopano-archiver -l" << endl;
    ostr << "     --auto-attach                 : When no user is specified with -u, all users" << endl;
    ostr << "                                     will have their archives attached or detached" << endl;
    ostr << "                                     based on the LDAP/ADS settings. If a user is" << endl;
    ostr << "                                     specified only that user's store will be processed." << endl;
    ostr << "                                     This option can be combined with -A/--archive to" << endl;
    ostr << "                                     force an auto-attach run regardless of the" << endl;
    ostr << "                                     enable_auto_attach configuration option." << endl;
    ostr << "  -f|--archive-folder <name>       : Specify an alternate name for the subfolder" << endl;
    ostr << "                                     that acts as the root of the archive." << endl;
    ostr << "  -s|--archive-server <path>       : Specify the server on which the archive should" << endl;
    ostr << "                                     be found." << endl;
    ostr << "  -N|--no-folder                   : Don't use a subfolder that acts as the root" << endl;
    ostr << "                                     of the archive. This implies that only one" << endl;
    ostr << "                                     archive can be made in the specified archive" << endl;
    ostr << "                                     store." << endl;
    ostr << "  -w                               : Grant write permissions on the archive. This will" << endl;
    ostr << "                                     override the auto_attach_writable config option." << endl;
    ostr << "     --writable <yes|no>           : Enable or disable write permissions. This will" << endl;
    ostr << "                                     override the auto_attach_writable config option." << endl;
    ostr << "  -c|--config                      : Use alternate config file." << endl;
    ostr << "                                     Default: archiver.cfg" << endl;
    ostr << "     --help                        : Show this help message." << endl;
    ostr << endl;
}

/**
 * Print an error message when multiple operational modes are selected by the user.
 *
 * @param[in]	modeSet
 *					The currently set mode.
 * @param[in]	modeReq
 *					The requested mode.
 * @param[in]	lpszName
 *					The name of the application.
 *
 * @todo	Make a nicer message about what went wrong based on modeSet and modeReq.
 */
static void print_mode_error(modes modeSet, modes modeReq,
    const char *lpszName)
{
    cerr << "Cannot select more than one mode!" << endl;
    print_help(cerr, lpszName);
}

enum cmdOptions {
    OPT_USER = UCHAR_MAX + 1,
    OPT_ATTACH,
    OPT_DETACH,
    OPT_DETACH_IDX,
    OPT_AUTO_ATTACH,
    OPT_FOLDER,
    OPT_ASERVER,
    OPT_NOFOLDER,
    OPT_LIST,
    OPT_LIST_ARCHUSER,
    OPT_ARCHIVE,
    OPT_CLEANUP,
    OPT_CONFIG,
    OPT_LOCAL,
    OPT_WRITABLE,
    OPT_FORCE_CLEANUP,
    OPT_HELP,
	OPT_DUMP_CONFIG,
};

static const struct option long_options[] = {
    { "user", 			   required_argument,   NULL, OPT_USER		       },
    { "attach-to",		   required_argument,	NULL, OPT_ATTACH	       },
    { "detach-from",	   required_argument,	NULL, OPT_DETACH	       },
    { "detach",			   required_argument,	NULL, OPT_DETACH_IDX       },
    { "auto-attach",	   no_argument,		    NULL, OPT_AUTO_ATTACH      },
    { "archive-folder",	   required_argument,	NULL, OPT_FOLDER	       },
    { "archive-server",    required_argument,   NULL, OPT_ASERVER	       },
    { "no-folder",		   no_argument,		    NULL, OPT_NOFOLDER	       },
    { "list",			   no_argument,		    NULL, OPT_LIST		       },
    { "list-archiveusers", no_argument,		    NULL, OPT_LIST_ARCHUSER    },
    { "archive",		   no_argument,		    NULL, OPT_ARCHIVE	       },
    { "cleanup",		   no_argument,		    NULL, OPT_CLEANUP	       },
    { "local-only",		   no_argument,		    NULL, OPT_LOCAL		       },
    { "help", 			   no_argument, 		NULL, OPT_HELP		       },
    { "config", 		   required_argument, 	NULL, OPT_CONFIG 	       },
    { "writable",		   required_argument,	NULL, OPT_WRITABLE	       },
    { "force-cleanup",	   no_argument,		    NULL, OPT_FORCE_CLEANUP    },
	{"dump-config", no_argument, nullptr, OPT_DUMP_CONFIG},
    { NULL, 			   no_argument, 		NULL, 0				       }
};

static inline LPTSTR toLPTST(const char* lpszString, convert_context& converter) { return lpszString ? converter.convert_to<LPTSTR>(lpszString) : NULL; }
static inline const char *yesno(bool bValue) { return bValue ? "yes" : "no"; }

} /* namespace */

using namespace KC;

/**
 * Program entry point
 */
int main(int argc, char *argv[])
{
    modes mode = MODE_INVALID;
    tstring strUser;
	const char *lpszArchive = nullptr, *lpszFolder = nullptr, *lpszArchiveServer = nullptr;
	bool bLocalOnly = false, bAutoAttach = false, bForceCleanup = false;
	unsigned int ulArchive = 0, ulAttachFlags = 0, ulFlags = 0;
	std::unique_ptr<Archiver> ptrArchiver;
    convert_context converter;

    const char *lpszConfig = Archiver::GetConfigPath();

    static const configsetting_t lpDefaults[] = {
        { "pid_file", "/var/run/kopano/archiver.pid" },
        { NULL, NULL }
    };

    setlocale(LC_CTYPE, "");

    while (1) {
		auto c = getopt_long(argc, argv, "u:c:lLACwa:d:D:f:s:N", long_options, NULL);
        if (c == -1)
            break;
        switch (c) {
        case 'u':
        case OPT_USER:
            strUser = converter.convert_to<tstring>(optarg);
            break;

        case 'a':
        case OPT_ATTACH:
            if (mode != MODE_INVALID) {
                print_mode_error(mode, MODE_ATTACH, argv[0]);
                return 1;
            }
            mode = MODE_ATTACH;
            lpszArchive = optarg;
            break;

        case 'd':
        case OPT_DETACH:
            if (mode != MODE_INVALID) {
                print_mode_error(mode, MODE_DETACH, argv[0]);
                return 1;
            }
            mode = MODE_DETACH;
            lpszArchive = optarg;
            break;

        case 'D':
        case OPT_DETACH_IDX: {
            char *res = NULL;

            if (mode != MODE_INVALID) {
                print_mode_error(mode, MODE_DETACH_IDX, argv[0]);
                return 1;
            }
            mode = MODE_DETACH_IDX;
            ulArchive = strtoul(optarg, &res, 10);
            if (!res || *res != '\0') {
                cerr << "Please specify a valid archive number." << endl;
                return 1;
            }
        }
        break;
        case OPT_AUTO_ATTACH:
            if (mode == MODE_ARCHIVE)
                bAutoAttach = true;
            else if(mode != MODE_INVALID) {
                print_mode_error(mode, MODE_AUTO_ATTACH, argv[0]);
                return 1;
            }
            mode = MODE_AUTO_ATTACH;
            break;

        case 'f':
        case OPT_FOLDER:
            if ((ulAttachFlags & ArchiveManage::UseIpmSubtree)) {
                cerr << "You cannot mix --archive-folder and --nofolder." << endl;
                print_help(cerr, argv[0]);
                return 1;
            }
            lpszFolder = optarg;
            break;

        case 's':
        case OPT_ASERVER:
            lpszArchiveServer = optarg;
            break;

        case 'N':
        case OPT_NOFOLDER:
            if (lpszFolder) {
                cerr << "You cannot mix --archive-folder and --nofolder." << endl;
                print_help(cerr, argv[0]);
                return 1;
            }
            ulAttachFlags |= ArchiveManage::UseIpmSubtree;
            break;

        case 'l':
        case OPT_LIST:
            if (mode != MODE_INVALID) {
                print_mode_error(mode, MODE_LIST, argv[0]);
                return 1;
            }
            mode = MODE_LIST;
            break;

        case 'L':
        case OPT_LIST_ARCHUSER:
            if (mode != MODE_INVALID) {
                print_mode_error(mode, MODE_LIST_ARCHUSER, argv[0]);
                return 1;
            }
            mode = MODE_LIST_ARCHUSER;
            break;

        case 'A':
        case OPT_ARCHIVE:
            if (mode == MODE_AUTO_ATTACH)
                bAutoAttach = true;
            else if (mode != MODE_INVALID) {
                print_mode_error(mode, MODE_ARCHIVE, argv[0]);
                return 1;
            }
            mode = MODE_ARCHIVE;
            break;

        case 'C':
        case OPT_CLEANUP:
            if (mode != MODE_INVALID) {
                print_mode_error(mode, MODE_CLEANUP, argv[0]);
                return 1;
            }
            mode = MODE_CLEANUP;
            break;

        case OPT_LOCAL:
            bLocalOnly = true;
            break;

        case 'c':
        case OPT_CONFIG:
            lpszConfig = optarg;
            ulFlags |= Archiver::RequireConfig;
            break;

        case 'w':
            ulAttachFlags |= ArchiveManage::Writable;
            break;

        case OPT_WRITABLE:
            if (parseBool(optarg))
                ulAttachFlags |= ArchiveManage::Writable;
            else
                ulAttachFlags |= ArchiveManage::ReadOnly;
            break;

        case OPT_FORCE_CLEANUP:
            bForceCleanup = true;
            break;
		case OPT_DUMP_CONFIG:
			ulFlags |= Archiver::DumpConfig;
			mode = MODE_TEST;
			break;
        case OPT_HELP:
            print_help(cout, argv[0]);
            return 1;

        case '?':
            // Invalid option, or required argument missing
            // getopt_long outputs the error message.
            print_help(cerr, argv[0]);
            return 1;
        default:
            break;
        };
    }

    if (mode == MODE_INVALID) {
        cerr << "Nothing to do!" << endl;
        print_help(cerr, argv[0]);
        return 1;
    }

    else if (mode == MODE_ATTACH) {
        if (strUser.empty()) {
            cerr << "Username cannot be empty" << endl;
            print_help(cerr, argv[0]);
            return 1;
        }

        if (lpszFolder != NULL && *lpszFolder == '\0')
            lpszFolder = NULL;
    }

    else if (mode == MODE_DETACH) {
        if (strUser.empty()) {
            cerr << "Username cannot be empty" << endl;
            print_help(cerr, argv[0]);
            return 1;
        }

        if (lpszFolder != NULL && *lpszFolder == '\0')
            lpszFolder = NULL;
    }

    else if (mode == MODE_LIST) {
        if (strUser.empty()) {
            cerr << "Username cannot be empty" << endl;
            print_help(cerr, argv[0]);
            return 1;
        }
    }

    else if (bForceCleanup && mode != MODE_CLEANUP) {
        cerr << "--force-cleanup is only valid in cleanup mode." << endl;
        return 1;
    }

	auto r = Archiver::Create(&ptrArchiver);
    if (r != Success) {
        cerr << "Failed to instantiate archiver object" << endl;
        return 1;
    }

    ulFlags |= Archiver::AttachStdErr;
    r = ptrArchiver->Init(argv[0], lpszConfig, lpDefaults, ulFlags);
	if (ulFlags & Archiver::DumpConfig)
		return r;
    if (r == FileNotFound) {
        cerr << "Unable to open configuration file " << lpszConfig << endl;
        return 1;
    } else if (r != Success) {
        cerr << "Failed to initialize" << endl;
        return 1;
    }

	ec_log_info("Startup command: \"%s\"", kc_join(&argv[0], &argv[argc], "\" \"").c_str());
	ptrArchiver->GetLogger(Archiver::LogOnly)->log(EC_LOGLEVEL_INFO, "Version " PROJECT_VERSION);
	auto lSettings = ptrArchiver->GetConfig()->GetAllSettings();
    ECLogger* filelogger = ptrArchiver->GetLogger(Archiver::LogOnly);
    ptrArchiver->GetLogger(Archiver::LogOnly)->Log(EC_LOGLEVEL_FATAL, "Config settings:");
	for (const auto &s : lSettings)
		if (strcmp(s.szName, "sslkey_pass") == 0 || strcmp(s.szName, "mysql_password") == 0)
			filelogger->logf(EC_LOGLEVEL_FATAL, "*  %s = '********'", s.szName);
		else
			filelogger->logf(EC_LOGLEVEL_FATAL, "*  %s = '%s'", s.szName, s.szValue);

    if (mode == MODE_ARCHIVE || mode == MODE_CLEANUP)
        if (unix_create_pidfile(argv[0], ptrArchiver->GetConfig(), false) != 0)
            return 1;

    ec_log_debug("Archiver mode: %d: (%s)", mode, modename(mode));
    switch (mode) {
    case MODE_ATTACH: {
        ArchiveManagePtr ptr;
        r = ptrArchiver->GetManage(strUser.c_str(), &ptr);
        if (r != Success)
            return 1;

		filelogger->logf(EC_LOGLEVEL_DEBUG, "Archiver action: Attach archive \"%s\" in server \"%s\" using folder \"%s\"", lpszArchive, lpszArchiveServer, lpszFolder);
        r = ptr->AttachTo(lpszArchiveServer, toLPTST(lpszArchive, converter), toLPTST(lpszFolder, converter), ulAttachFlags);
		filelogger->logf(EC_LOGLEVEL_DEBUG, "Archiver result %d (%s)", r, ArchiveResultString(r));
    }
    break;

    case MODE_DETACH_IDX:
    case MODE_DETACH: {
        ArchiveManagePtr ptr;
        r = ptrArchiver->GetManage(strUser.c_str(), &ptr);
        if (r != Success)
            return 1;

        if (mode == MODE_DETACH_IDX) {
			filelogger->logf(EC_LOGLEVEL_DEBUG, "Archiver action: Detach archive %u", ulArchive);
            r = ptr->DetachFrom(ulArchive);
			filelogger->logf(EC_LOGLEVEL_DEBUG, "Archiver result %d (%s)", r, ArchiveResultString(r));
        } else {
			filelogger->logf(EC_LOGLEVEL_DEBUG, "Archiver action: Detach archive \"%s\" on server \"%s\", folder \"%s\"", lpszArchive, lpszArchiveServer, lpszFolder);
            r = ptr->DetachFrom(lpszArchiveServer, toLPTST(lpszArchive, converter), toLPTST(lpszFolder, converter));
        }
    }
    break;

    case MODE_AUTO_ATTACH: {
        if (strUser.size()) {
            ArchiveManagePtr ptr;
            r = ptrArchiver->GetManage(strUser.c_str(), &ptr);
            if (r != Success)
                return 1;
			filelogger->logf(EC_LOGLEVEL_DEBUG, "Archiver action: Autoattach for user \"%ls\", flags: %u", strUser.c_str(), ulAttachFlags);
            r = ptr->AutoAttach(ulAttachFlags);
        } else {
			filelogger->logf(EC_LOGLEVEL_DEBUG, "Archiver action: Autoattach flags: %u", ulAttachFlags);
            r = ptrArchiver->AutoAttach(ulAttachFlags);
        }
		filelogger->logf(EC_LOGLEVEL_DEBUG, "Archiver result %d (%s)", r, ArchiveResultString(r));
    }
    break;

    case MODE_LIST: {
        ArchiveManagePtr ptr;
        r = ptrArchiver->GetManage(strUser.c_str(), &ptr);
        if (r != Success)
            return 1;

        filelogger->Log(EC_LOGLEVEL_DEBUG, "Archiver action: List archives");
        r = ptr->ListArchives(cout);
		filelogger->logf(EC_LOGLEVEL_DEBUG, "Archiver result %d (%s)", r, ArchiveResultString(r));
    }
    break;

    case MODE_LIST_ARCHUSER: {
        ArchiveManagePtr ptr;
        r = ptrArchiver->GetManage(KC_T("SYSTEM"), &ptr);
        if (r != Success)
            return 1;

        filelogger->Log(EC_LOGLEVEL_DEBUG, "Archiver action: List archive users");
        r = ptr->ListAttachedUsers(cout);
		filelogger->logf(EC_LOGLEVEL_DEBUG, "Archiver result %d (%s)", r, ArchiveResultString(r));
    }
    break;

    case MODE_ARCHIVE: {
        ArchiveControlPtr ptr;
        r = ptrArchiver->GetControl(&ptr);
        if (r != Success)
            return 1;

        if (strUser.size()) {
			filelogger->logf(EC_LOGLEVEL_DEBUG, "Archiver action: archive user \"%ls\" (autoattach: %s, flags %u)", strUser.c_str(), yesno(bAutoAttach), ulAttachFlags);
            r = ptr->Archive(strUser, bAutoAttach, ulAttachFlags);
        } else {
			filelogger->logf(EC_LOGLEVEL_DEBUG, "Archiver action: archive all users (local only: %s autoattach: %s, flags %u)", yesno(bLocalOnly), yesno(bAutoAttach), ulAttachFlags);
            r = ptr->ArchiveAll(bLocalOnly, bAutoAttach, ulAttachFlags);
        }
		filelogger->logf(EC_LOGLEVEL_DEBUG, "Archiver result %d (%s)", r, ArchiveResultString(r));
    }
    break;

    case MODE_CLEANUP: {
        ArchiveControlPtr ptr;
        r = ptrArchiver->GetControl(&ptr, bForceCleanup);
        if (r != Success)
            return 1;

        if (strUser.size()) {
			filelogger->logf(EC_LOGLEVEL_DEBUG, "Archiver action: Cleanup user \"%ls\"", strUser.c_str());
            r = ptr->Cleanup(strUser);
        } else {
			filelogger->logf(EC_LOGLEVEL_DEBUG, "Archiver action: Cleanup all (local only): %s", yesno(bLocalOnly));
            r = ptr->CleanupAll(bLocalOnly);
        }
		filelogger->logf(EC_LOGLEVEL_DEBUG, "Archiver result %d (%s)", r, ArchiveResultString(r));
    }
    break;

    case MODE_INVALID:
	case MODE_TEST:
        filelogger->Log(EC_LOGLEVEL_DEBUG, "Archiver action: invalid");
        break;
    }
    return 0;
}
