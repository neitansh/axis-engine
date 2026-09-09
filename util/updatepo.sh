#!/bin/sh

# Update/create axis po files

# an auxiliary function to abort processing with an optional error
# message
abort() {
	test -n "$1" && echo >&2 "$1"
	exit 1
}

# The po/ directory is assumed to be parallel to the directory where
# this script is. Relative paths are fine for us so we can just
# use the following trick (works both for manual invocations and for
# script found from PATH)
scriptisin="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"

# The script is executed from the parent of po/, which is also the
# parent of the script directory and of the src/ directory.
# We go through $scriptisin so that it can be executed from whatever
# directory and still work correctly
cd "$scriptisin/.."

test -e po || abort "po/ directory not found"
test -d po || abort "po/ is not a directory!"

# Get a list of the languages we have to update/create

cd po || abort "couldn't change directory to po!"

# This assumes that we won't have dirnames with space, which is
# the case for language codes, which are the only subdirs we expect to
# find in po/ anyway. If you put anything else there, you need to suffer
# the consequences of your actions, so we don't do sanity checks
#
# Языки можно перечислить доводами: `util/updatepo.sh ru` трогает только
# русский. Остальные приходят переводом со стороны, и переписывать все восемь
# десятков ради одной правки значит топить её в чужом шуме.
langs="$*"

if test -z "$langs"; then
	for lang in * ; do
		if test ! -d $lang; then
			continue
		fi
		langs="$langs $lang"
	done
fi

# go back
cd ..

# First thing first, update the .pot template. We place it in the po/
# directory at the top level. You a recent enough xgettext that supports
# --package-name
potfile=po/axis.pot
echo "updating pot"
# Выборка строк вынесена: тем же скриптом сверяет шаблон с кодом сторож
# util/check_translations.sh. Она же собирает src/settings_translation_file.cpp,
# без которого тексты настроек до перевода не доходят вовсе.
"$scriptisin/extract_pot.sh" "$potfile" || abort "не собрался шаблон перевода"

# Now iterate on all languages and create the po file if missing, or update it
# if it exists already
for lang in $langs ; do # note the missing quotes around $langs
	pofile=po/$lang/axis.po
	if test -e $pofile; then
		echo "[$lang]: updating strings"
		# Drop old strings *before* updating such that they can be re-used
		# until this script is run again.
		msgattrib --output-file=$pofile --no-obsolete $pofile
		msgmerge --update --backup=none --sort-by-file $pofile $potfile
	else
		# This will ask for the translator identity
		echo "[$lang]: NEW strings"
		msginit --locale=$lang --output-file=$pofile --input=$potfile
	fi

done
