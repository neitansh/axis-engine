-- Luanti
-- Copyright (C) 2021-2 x2048
-- Copyright (C) 2022-3 rubenwardy
-- SPDX-License-Identifier: LGPL-2.1-or-later


local shadow_levels_labels = {
	fgettext("Disabled"),
	fgettext("Very Low"),
	fgettext("Low"),
	fgettext("Medium"),
	fgettext("High"),
	fgettext("Very High"),
	fgettext("Custom"),
}
local PRESET_DISABLED = 1
local PRESET_CUSTOM = #shadow_levels_labels


-- max distance, texture size, texture_32bit, filters, map color
--
-- Край тени во всех наборах чёткий (filters = 0), и это не экономия, а решение
-- о том, как игра выглядит: у мира из кубов тень куба должна быть кубом, а не
-- размытым пятном. Размытие в шейдере осталось и работает, но включать его
-- больше нечем — за него платили до половины стоимости теней, и картинке от
-- этого было только хуже.
--
-- Наборы отличаются тем, что и решает чёткость: сколько пикселей карты
-- приходится на узел мира. Разрешение делить на дальность — вот и вся мера.
local shadow_presets = {
	[2] = { 62,  512,  true, 0, false },
	[3] = { 93,  1024, true, 0, false },
	[4] = { 140, 2048, true, 0, false },
	[5] = { 210, 4096, true, 0,  true },
	[6] = { 300, 8192, true, 0,  true },
}


local function detect_mapping_idx()
	if not core.settings:get_bool("enable_dynamic_shadows", false) then
		return PRESET_DISABLED
	end

	local shadow_map_max_distance = tonumber(core.settings:get("shadow_map_max_distance"))
	local shadow_map_texture_size = tonumber(core.settings:get("shadow_map_texture_size"))
	local shadow_map_texture_32bit = core.settings:get_bool("shadow_map_texture_32bit")
	local shadow_filters = tonumber(core.settings:get("shadow_filters"))
	local shadow_map_color = core.settings:get_bool("shadow_map_color")

	for i = 2, 6 do
		local preset = shadow_presets[i]
		if preset[1] == shadow_map_max_distance and
				preset[2] == shadow_map_texture_size and
				preset[3] == shadow_map_texture_32bit and
				preset[4] == shadow_filters and
				preset[5] == shadow_map_color then
			return i
		end
	end
	return PRESET_CUSTOM
end


local function apply_preset(preset)
	if preset then
		core.settings:set_bool("enable_dynamic_shadows", true)
		core.settings:set("shadow_map_max_distance", preset[1])
		core.settings:set("shadow_map_texture_size", preset[2])
		core.settings:set_bool("shadow_map_texture_32bit", preset[3])
		core.settings:set("shadow_filters", preset[4])
		core.settings:set_bool("shadow_map_color", preset[5])
	else
		core.settings:set_bool("enable_dynamic_shadows", false)
	end
end


return {
	query_text = N_("Shadows"),
	requires = {
		shadows_support = true,
	},
	context = "client",
	get_formspec = function(self, avail_w)
		local labels = table.copy(shadow_levels_labels)
		local idx = detect_mapping_idx()

		-- Remove "custom" if not already selected
		if idx ~= PRESET_CUSTOM then
			table.remove(labels, PRESET_CUSTOM)
		end

		-- Don't show anything if shadows are disabled
		-- Reason: there's a separate checkbox and users can't see the suggested default in the dropdown
		if idx == PRESET_DISABLED then
			return "", 0
		end

		local fs =
			"label[0,0.2;" .. fgettext("Quality preset") .. "]" ..
			"dropdown[0,0.4;3,0.8;dd_shadows;" .. table.concat(labels, ",") .. ";" .. idx .. ";true]" ..
			"label[0,1.5;" .. core.colorize("#bbb", fgettext("(The crate will need to enable shadows as well)")) .. "]"
		return fs, 1.8
	end,
	on_submit = function(self, fields)
		if fields.dd_shadows then
			local old_shadow_level_idx = detect_mapping_idx()
			local shadow_level_idx = tonumber(fields.dd_shadows)
			if shadow_level_idx == nil or shadow_level_idx == old_shadow_level_idx then
				return false
			end

			if shadow_level_idx == PRESET_CUSTOM then
				core.settings:set_bool("enable_dynamic_shadows", true)
				return true
			end

			local preset = shadow_presets[shadow_level_idx]
			apply_preset(preset)
			return true
		end
	end,
}
