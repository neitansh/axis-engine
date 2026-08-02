#!/bin/bash
# Runs a multiplayer server and connects a headless client, devtest unittests are executed.

dir="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
gameid=${gameid:-devtest}
executable=$dir/../bin/luanti
testspath=$dir/../tests
# The engine reads a directory of config files; a test only needs one of them.
conf_dir_client1=$testspath/config-client1
conf_dir_server=$testspath/config-server
conf_client1=$conf_dir_client1/client/custom.conf
conf_server=$conf_dir_server/server/custom.conf
worldpath=$testspath/world

waitfor () {
	n=30
	while [ $n -gt 0 ]; do
		[ -f "$1" ] && return 0
		sleep 0.5
		((n-=1))
	done
	echo "Waiting for ${1##*/} timed out"
	pkill -P $$
	exit 1
}

[ -e "$executable" ] || { echo "executable $executable missing"; exit 1; }

rm -f "$testspath/log.txt"
rm -rf "$worldpath"
mkdir -p "$worldpath/worldmods"

mkdir -p "$(dirname "$conf_client1")" "$(dirname "$conf_server")"

printf '%s\n' >"$conf_client1" \
	video_driver=null name=client1 viewing_range=10 \
	enable_{minimap,post_processing}=false enable_client_modding=true

printf '%s\n' >"$conf_server" \
	max_block_send_distance=1 active_block_range=1 \
	devtest_unittests_autostart=true helper_mode=devtest \
	"${serverconf:-}"

ln -s "$dir/helper_mod" "$worldpath/worldmods/"

echo "Starting server"
"$executable" --debugger --server --config-dir "$conf_dir_server" --world "$worldpath" --gameid $gameid 2>&1 \
	| sed -u 's/^/(server) /' | tee -a "$testspath/log.txt" &
waitfor "$worldpath/startup"

echo "Starting client"
export ALSOFT_DRIVERS=null
"$executable" --debugger --config-dir "$conf_dir_client1" --go --address 127.0.0.1 2>&1 \
	| sed -u 's/^/(client) /' | tee -a "$testspath/log.txt" &
waitfor "$worldpath/done"

echo "Waiting for client and server to exit"
wait

if [ -f "$worldpath/test_failure" ]; then
	echo "There were test failures."
	exit 1
fi
# gdb|lldb
if grep -Eq "(Thread .* received signal|thread .* stop reason =)" "$testspath/log.txt"; then
	echo "Debugger reported error."
	exit 1
fi
echo "Success"
exit 0
