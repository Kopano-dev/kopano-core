/* SPDX-License-Identifier: AGPL-3.0-only */
%{
#include "archiver_conv.h"
%}

%typemap(out) ARCHLIST
{
	%append_output3(List_from_$basetype($1));
}

%apply ARCHLIST {ArchiveList, UserList};
