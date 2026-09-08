#!/bin/bash
dir="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
crateid=${crateid:-mineclonia}
executable=$dir/../bin/axis
testspath=$dir/../tests
# The engine reads a directory of config files; a test only needs one of them.
conf_dir=$testspath/config-server
conf_server=$conf_dir/server/custom.conf
worldpath=$testspath/world

run () {
	if [ -n "$PERF" ]; then
		perf record -z --call-graph dwarf -- "$@"
	else
		"$@"
	fi
}

[ -e "$executable" ] || { echo "executable $executable missing"; exit 1; }

# The scripts need a crate to run. It is not part of this repository, so say
# what is missing instead of failing halfway through.
if [ ! -d "$dir/../depot/$crateid" ]; then
	echo "Crate '$crateid' is not installed, skipping."
	exit 0
fi

rm -rf "$worldpath"
mkdir -p "$worldpath/worldmods"

settings=(sqlite_synchronous=0 helper_mode=mapgen)
[ -n "$PROFILER" ] && settings+=(profiler_print_interval=15)
mkdir -p "$(dirname "$conf_server")"
printf '%s\n' "${settings[@]}" >"$conf_server" \

ln -s "$dir/helper_mod" "$worldpath/worldmods/"

args=(--config-dir "$conf_dir" --world "$worldpath" --crateid $crateid)
[ -n "$PROFILER" ] && args+=(--verbose)
run "$executable" --server "${args[@]}"
