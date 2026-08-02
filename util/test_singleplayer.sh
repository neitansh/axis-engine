#!/bin/bash
# Runs a singleplayer session with software-rendering.

dir="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
gameid=${gameid:-mineclonia}
executable=$dir/../bin/axis
testspath=$dir/../tests
# The engine reads a directory of config files; a test only needs one of them.
conf_dir=$testspath/config-client
conf_client=$conf_dir/client/custom.conf
worldpath=$testspath/world

[ -e "$executable" ] || { echo "executable $executable missing"; exit 1; }

# The scripts need a game to run. It is not part of this repository, so say
# what is missing instead of failing halfway through.
if [ ! -d "$dir/../games/$gameid" ]; then
	echo "Game '$gameid' is not installed, skipping."
	exit 0
fi

rm -rf "$worldpath"
mkdir -p "$worldpath/worldmods"

# enable a lot of visual effects so we can catch shader errors and other obvious bugs
opts=(
	screen_w=384 screen_h=256 fps_max=5
	active_block_range=1 viewing_range=40 helper_mode=smoke
	opengl_debug=true mip_map=true enable_waving_{leaves,plants,water}=true
	antialiasing=ssaa node_highlighting=halo
	enable_{auto_exposure,bloom,dynamic_shadows,translucent_foliage,volumetric_lighting,water_reflections}=true
	shadow_map_color=true
)
mkdir -p "$(dirname "$conf_client")"
printf '%s\n' "${opts[@]}" "${clientconf:-}" >"$conf_client"

ln -s "$dir/helper_mod" "$worldpath/worldmods/"

export ALSOFT_DRIVERS=null
export LIBGL_ALWAYS_SOFTWARE=true
export MESA_DEBUG=1
timeout 25 "$executable" --config-dir "$conf_dir" --go --world "$worldpath" --gameid "$gameid" --info
r=$?
echo "Exit status: $r"
[ $r -eq 124 ] && echo "(timed out)"
[ $r -ne 0 ] && exit 1

echo "Success"
exit 0
