#!/bin/sh

abs_top_builddir="$1"
bd=$(readlink -f "$abs_top_builddir")
cwd=$(readlink -f .)
if [ "$cwd" != "$abs_top_builddir" ]; then
	echo "***"
	echo "*** WARNING: You moved the build tree away"
	if [ "$abs_top_builddir" = "$bd" ]; then
		echo "***    from  $abs_top_builddir"
	else
		echo "***    from  $bd ($abs_top_builddir)"
	fi
	echo "***      to  $cwd"
	echo "***"
	echo "*** As such, just-built programs may no longer find their"
	echo "*** libraries without a relink."
	echo "***"
fi
