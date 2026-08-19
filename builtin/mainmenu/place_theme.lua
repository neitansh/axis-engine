-- Luanti
-- Copyright (C) 2013 sapier
-- SPDX-License-Identifier: LGPL-2.1-or-later

mm_place_theme = {}

local COLORS = {
	dark = { clouds = "#1c2a47", sky = "#090b1a" },
	light = { clouds = "#f0f0ff", sky = "#8cbafa" },
}

--------------------------------------------------------------------------------
function mm_place_theme.init()
	mm_place_theme.texturepack = core.settings:get("texture_path")

	mm_place_theme.placeid = nil

	mm_place_theme.music_handle = nil
end

--------------------------------------------------------------------------------
function mm_place_theme.set_engine(hide_decorations)
	mm_place_theme.placeid = nil
	mm_place_theme.stop_music()

	core.set_topleft_text("")

	local have_bg = false
	local have_overlay = mm_place_theme.set_engine_single("overlay")

	if not have_overlay then
		have_bg = mm_place_theme.set_engine_single("background")
	end

	mm_place_theme.clear_single("header")
	mm_place_theme.clear_single("footer")
	core.set_clouds(false)

	if not hide_decorations then
		mm_place_theme.set_engine_single("header")
		mm_place_theme.set_engine_single("footer")
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
function mm_place_theme.set_place(placedetails)
	assert(placedetails ~= nil)

	if mm_place_theme.placeid == placedetails.id then
		return
	end
	mm_place_theme.placeid = placedetails.id
	mm_place_theme.set_music(placedetails)

	core.set_topleft_text(placedetails.name)

	local have_bg = false
	local have_overlay = mm_place_theme.set_place_single("overlay", placedetails)

	if not have_overlay then
		have_bg = mm_place_theme.set_place_single("background", placedetails)
	end

	mm_place_theme.clear_single("header")
	mm_place_theme.clear_single("footer")
	core.set_clouds(false)

	mm_place_theme.set_place_single("header", placedetails)
	mm_place_theme.set_place_single("footer", placedetails)

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
function mm_place_theme.clear_single(identifier)
	core.set_background(identifier, "")
end

--------------------------------------------------------------------------------
local valid_image_extensions = {
	".png",
	".jpg",
	".jpeg",
}

function mm_place_theme.set_engine_single(identifier)
	--try texture pack first
	if mm_place_theme.texturepack ~= nil then
		for _, extension in pairs(valid_image_extensions) do
			local path = mm_place_theme.texturepack .. DIR_DELIM .. "menu_" .. identifier .. extension
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
function mm_place_theme.set_place_single(identifier, placedetails)
	local extensions_randomised = table.copy(valid_image_extensions)
	table.shuffle(extensions_randomised)
	for _, extension in pairs(extensions_randomised) do
		assert(placedetails ~= nil)

		if mm_place_theme.texturepack ~= nil then
			local path = mm_place_theme.texturepack .. DIR_DELIM .. placedetails.id .. "_menu_" .. identifier .. extension
			if core.set_background(identifier, path) then
				return true
			end
		end

		-- Find out how many randomized textures the place provides
		local n = 0
		local filename
		local menu_files = core.get_dir_list(placedetails.path .. DIR_DELIM .. "menu", false)
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

		local path = placedetails.path .. DIR_DELIM .. "menu" .. DIR_DELIM .. filename
		if core.set_background(identifier, path) then
			return true
		end

	end
	return false
end

--------------------------------------------------------------------------------
function mm_place_theme.stop_music()
	if mm_place_theme.music_handle ~= nil then
		core.sound_stop(mm_place_theme.music_handle)
	end
end

--------------------------------------------------------------------------------
function mm_place_theme.set_music(placedetails)
	mm_place_theme.stop_music()

	assert(placedetails ~= nil)

	local music_path = placedetails.path .. DIR_DELIM .. "menu" .. DIR_DELIM .. "theme"
	mm_place_theme.music_handle = core.sound_play(music_path, true)
end
