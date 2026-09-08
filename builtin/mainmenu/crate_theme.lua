-- Luanti
-- Copyright (C) 2013 sapier
-- SPDX-License-Identifier: LGPL-2.1-or-later

mm_crate_theme = {}

local COLORS = {
	dark = { clouds = "#1c2a47", sky = "#090b1a" },
	light = { clouds = "#f0f0ff", sky = "#8cbafa" },
}

--------------------------------------------------------------------------------
function mm_crate_theme.init()
	mm_crate_theme.texturepack = core.settings:get("texture_path")

	mm_crate_theme.crateid = nil

	mm_crate_theme.music_handle = nil
end

--------------------------------------------------------------------------------
function mm_crate_theme.set_engine(hide_decorations)
	mm_crate_theme.crateid = nil
	mm_crate_theme.stop_music()

	core.set_topleft_text("")

	local have_bg = false
	local have_overlay = mm_crate_theme.set_engine_single("overlay")

	if not have_overlay then
		have_bg = mm_crate_theme.set_engine_single("background")
	end

	mm_crate_theme.clear_single("header")
	mm_crate_theme.clear_single("footer")
	core.set_clouds(false)

	if not hide_decorations then
		mm_crate_theme.set_engine_single("header")
		mm_crate_theme.set_engine_single("footer")
	end

	local c = COLORS[core.settings:get("menu_theme")]
	if not c then
		core.log("warning", "Invalid menu theme: " .. core.settings:get("menu_theme"))
	else
		core.set_clouds_color(c.clouds)
		core.set_sky_color(c.sky)
	end

	if not have_bg then
		core.set_clouds(core.settings:get_bool("menu_clouds"))
	end
end

--------------------------------------------------------------------------------
function mm_crate_theme.set_crate(cratedetails)
	assert(cratedetails ~= nil)

	if mm_crate_theme.crateid == cratedetails.id then
		return
	end
	mm_crate_theme.crateid = cratedetails.id
	mm_crate_theme.set_music(cratedetails)

	core.set_topleft_text(cratedetails.name)

	local have_bg = false
	local have_overlay = mm_crate_theme.set_crate_single("overlay", cratedetails)

	if not have_overlay then
		have_bg = mm_crate_theme.set_crate_single("background", cratedetails)
	end

	mm_crate_theme.clear_single("header")
	mm_crate_theme.clear_single("footer")
	core.set_clouds(false)

	mm_crate_theme.set_crate_single("header", cratedetails)
	mm_crate_theme.set_crate_single("footer", cratedetails)

	local c = COLORS[core.settings:get("menu_theme")]
	if not c then
		core.log("warning", "Invalid menu theme: " .. core.settings:get("menu_theme"))
	else
		core.set_clouds_color(c.clouds)
		core.set_sky_color(c.sky)
	end

	if not have_bg then
		core.set_clouds(core.settings:get_bool("menu_clouds"))
	end
end

--------------------------------------------------------------------------------
function mm_crate_theme.clear_single(identifier)
	core.set_background(identifier, "")
end

--------------------------------------------------------------------------------
local valid_image_extensions = {
	".png",
	".jpg",
	".jpeg",
}

function mm_crate_theme.set_engine_single(identifier)
	--try texture pack first
	if mm_crate_theme.texturepack ~= nil then
		for _, extension in pairs(valid_image_extensions) do
			local path = mm_crate_theme.texturepack .. DIR_DELIM .. "menu_" .. identifier .. extension
			if core.set_background(identifier, path) then
				return true
			end
		end
	end

	local path = defaulttexturedir .. DIR_DELIM .. "menu_" .. identifier .. ".png"
	if core.set_background(identifier, path) then
		return true
	end

	return false
end

--------------------------------------------------------------------------------
function mm_crate_theme.set_crate_single(identifier, cratedetails)
	local extensions_randomised = table.copy(valid_image_extensions)
	table.shuffle(extensions_randomised)
	for _, extension in pairs(extensions_randomised) do
		assert(cratedetails ~= nil)

		if mm_crate_theme.texturepack ~= nil then
			local path = mm_crate_theme.texturepack .. DIR_DELIM .. cratedetails.id .. "_menu_" .. identifier .. extension
			if core.set_background(identifier, path) then
				return true
			end
		end

		-- Find out how many randomized textures the crate provides
		local n = 0
		local filename
		local menu_files = core.get_dir_list(cratedetails.path .. DIR_DELIM .. "menu", false)
		for i = 1, #menu_files do
			filename = identifier .. "." .. i .. extension
			if table.indexof(menu_files, filename) == -1 then
				n = i - 1
				break
			end
		end
		-- Select random texture, 0 means standard texture
		n = math.random(0, n)
		if n == 0 then
			filename = identifier .. extension
		else
			filename = identifier .. "." .. n .. extension
		end

		local path = cratedetails.path .. DIR_DELIM .. "menu" .. DIR_DELIM .. filename
		if core.set_background(identifier, path) then
			return true
		end

	end
	return false
end

--------------------------------------------------------------------------------
function mm_crate_theme.stop_music()
	if mm_crate_theme.music_handle ~= nil then
		core.sound_stop(mm_crate_theme.music_handle)
	end
end

--------------------------------------------------------------------------------
function mm_crate_theme.set_music(cratedetails)
	mm_crate_theme.stop_music()

	assert(cratedetails ~= nil)

	local music_path = cratedetails.path .. DIR_DELIM .. "menu" .. DIR_DELIM .. "theme"
	mm_crate_theme.music_handle = core.sound_play(music_path, true)
end
