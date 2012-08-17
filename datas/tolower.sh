#!/bin/bash
do_rename() {
	j=`echo "$1" | tr '[A-Z]' '[a-z]'`
	if test x"$1" != x"$j"; then
		mv -vn "$1" "$j" || exit 1
	fi
}

find -iname \*.sod | while read X; do do_rename "$X"; done

