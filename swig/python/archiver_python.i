%{
#include "archiver_conv.h"
%}

%typemap(out) ARCHLIST
{
	%append_output(List_from_$basetype($1));
}

%apply ARCHLIST {ArchiveList, UserList};
