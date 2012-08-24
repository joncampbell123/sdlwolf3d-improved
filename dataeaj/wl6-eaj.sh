#!/bin/bash
do_rename() {
	j=`echo "$1" | sed -e 's/\.wl6/.eaj/'`
	if test x"$1" != x"$j"; then
		mv -vn "$1" "$j" || exit 1
	fi
}

find -iname \*.wl6 | while read X; do do_rename "$X"; done

