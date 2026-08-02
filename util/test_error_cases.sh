#!/bin/bash
dir="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
gameid=${gameid:-mineclonia}
executable=$dir/../bin/axis
testspath=$dir/../tests
# The engine reads a directory of config files; a test only needs one of them.
conf_dir=$testspath/config-server
conf_server=$conf_dir/server/custom.conf
worldpath=$testspath/world

[ -e "$executable" ] || { echo "executable $executable missing"; exit 1; }

# The scripts need a game to run. It is not part of this repository, so say
# what is missing instead of failing halfway through.
if [ ! -d "$dir/../games/$gameid" ]; then
	echo "Game '$gameid' is not installed, skipping."
	exit 0
fi

write_config () {
	mkdir -p "$(dirname "$conf_server")"
	printf '%s\n' >"$conf_server" \
		helper_mode=error mg_name=singlenode "$@"
}

run () {
	# A full game takes a while to start up before it can reach the error
	timeout 60 "$@"
	r=$?
	echo "Exit status: $r"
	[ $r -eq 124 ] && echo "(timed out)"
	if [ $r -ne 1 ]; then
		echo "-> Test failed"
		exit 1
	fi
}

rm -rf "$worldpath"
mkdir -p "$worldpath/worldmods"

ln -s "$dir/helper_mod" "$worldpath/worldmods/"

args=(--server --config-dir "$conf_dir" --world "$worldpath" --gameid $gameid)

# make sure we can tell apart sanitizer and luanti errors
export ASAN_OPTIONS="exitcode=42"
export MSAN_OPTIONS="exitcode=42"

# see helper_mod/init.lua for the different types
for n in $(seq 1 6); do
	write_config error_type=$n
	run "$executable" "${args[@]}"
	echo "---------------"
done

echo "All done."
exit 0
