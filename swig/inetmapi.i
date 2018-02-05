%module inetmapi

%{
#include <mapix.h>
#include <mapidefs.h>
#include <inetmapi/options.h>
#include <inetmapi/inetmapi.h>
%}

%include <std_string.i>
%include <kopano/typemap.i>

/* Finalize output parameters */
%typemap(in,numinputs=0) (std::string *) (std::string temp) {
	$1 = &temp;
}
%typemap(argout) (std::string *) {
	/* @todo fix this not to go through a cstring */
	%append_output(SWIG_FromCharPtrAndSize($1->c_str(), $1->length()));
}

%typemap(in) (const std::string &input) (std::string temp, char *buf=NULL, Py_ssize_t size)
{
    if(PyBytes_AsStringAndSize($input, &buf, &size) == -1)
        %argument_fail(SWIG_ERROR,"$type",$symname, $argnum);

    temp = std::string(buf, size);
    $1 = &temp;
}

%typemap(freearg) (const std::string &input) {
}

%typemap(in,numinputs=0) (char** lppchardelete) (char *temp) {
	$1 = &temp;
}
%typemap(argout) (char** lppchardelete)
{
  if (*$1) {
    %append_output(PyBytes_FromString(*$1));
    delete[](*$1);
  }
}

struct sending_options {
        char *alternate_boundary;               // Specifies a specific boundary prefix to use when creating MIME boundaries
        bool no_recipients_workaround;  // Specified that we wish to accepts messages with no recipients (for example, when converting an attached email with no recipients)
        bool msg_in_msg;
        bool headers_only;
        bool add_received_date;
        int use_tnef;
        char *charset_upgrade;
        bool allow_send_to_everyone;
        bool enable_dsn;
        %extend {
			sending_options() {
				auto sopt = new sending_options;
				imopt_default_sending_options(sopt);
				char *temp = sopt->charset_upgrade;
				sopt->charset_upgrade = new char[strlen(temp)+1]; /* avoid free problems */
				strcpy(sopt->charset_upgrade, temp);
				return sopt;
			}
			~sending_options() {
				delete[] self->alternate_boundary;
				delete[] self->charset_upgrade;
				delete(self);
			}
		}
};

struct delivery_options {
        bool use_received_date;         // Use the 'received' date instead of the current date as delivery date
        bool mark_as_read;              // Deliver the message 'read' instead of unread
        bool add_imap_data;				// Save IMAP optimized data to the server
	bool parse_smime_signed;        // Parse actual S/MIME content instead of just writing out the S/MIME data to a single attachment
        /* LPSBinary user_entryid;         // If not NULL, specifies the entryid of the user for whom we are delivering. If set, allows generating PR_MESSAGE_*_ME properties. */
	char *ascii_upgrade;
	bool header_strict_rfc;

        %extend {
            delivery_options() {
				auto dopt = new delivery_options;
				imopt_default_delivery_options(dopt);
				return dopt;
			}
			~delivery_options() {
				delete[] self->ascii_upgrade;
				delete(self);
			}
        }
};

HRESULT IMToMAPI(IMAPISession *lpSession, IMsgStore *lpMsgStore, IAddrBook *lpAddrBook, IMessage *lpMessage, const std::string &input, delivery_options dopt);
HRESULT IMToINet(IMAPISession *lpSession, IAddrBook *lpAddrBook, IMessage *lpMessage, char** lppchardelete, sending_options sopt);

HRESULT createIMAPProperties(const std::string &input, std::string *lpEnvelope, std::string *lpBody, std::string *lpBodyStructure);
