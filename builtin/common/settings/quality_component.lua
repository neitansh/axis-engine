-- Axis
-- SPDX-License-Identifier: LGPL-2.1-or-later

-- Наборы качества картинки.
--
-- Настроек графики три десятка, и по отдельности они мало кому что говорят:
-- игрок хочет «покрасивее» или «побыстрее», а не решать про размер карты теней.
-- Здесь один выбор ставит всё сразу, а кому нужно своё — тот открывает
-- настройки ниже, и набор сам переключается на «свои».
--
-- Что стоит в наборах, взято из замеров на арене матча, а не с потолка. Самая
-- дорогая работа в кадре — геометрия: листва рисуется всеми шестью гранями
-- каждой ноды, включая закрытые такой же листвой, и на них уходит половина
-- вершин кадра. Отсюда leaves_detail_range: вблизи крона полная, вдали без
-- нутра, и на глаз это неразличимо. Тени — вторая по дороговизне система, и
-- их набор задаётся тем, сколько пикселей карты приходится на узел мира.

local labels = {
	fgettext("Low"),
	fgettext("Standard"),
	fgettext("High"),
	fgettext("Ultra"),
	fgettext("Custom"),
}
local CUSTOM = #labels

-- Порядок значения не имеет, важно только, что наборы описаны одним списком
-- настроек: тогда «свои» определяются сравнением, а не догадками.
local presets = {
	-- Низкие: всё, что стоит миллисекунд, выключено. Мир остаётся собой.
	{
		leaves_style = "simple",
		leaves_detail_range = "16",
		foliage_range = "48",
		smooth_lighting = false,
		enable_dynamic_shadows = false,
		enable_post_processing = false,
		enable_bloom = false,
		enable_volumetric_lighting = false,
		enable_auto_exposure = false,
		enable_waving_leaves = false,
		enable_waving_plants = false,
		enable_waving_water = false,
		enable_water_reflections = false,
		enable_translucent_foliage = false,
		enable_3d_clouds = false,
		fxaa = false,
		antialiasing = "none",
	},
	-- Стандартные: то, что даёт лучшую картинку за разумные деньги.
	{
		leaves_style = "fancy",
		leaves_detail_range = "32",
		foliage_range = "128",
		smooth_lighting = true,
		enable_dynamic_shadows = true,
		shadow_map_texture_size = "1024",
		shadow_map_max_distance = "80",
		enable_post_processing = true,
		enable_bloom = true,
		enable_volumetric_lighting = false,
		enable_auto_exposure = false,
		enable_waving_leaves = true,
		enable_waving_plants = true,
		enable_waving_water = true,
		enable_water_reflections = false,
		enable_translucent_foliage = false,
		enable_3d_clouds = true,
		fxaa = true,
		antialiasing = "none",
	},
	-- Высокие: дальняя крона полнее, тени чётче, свет объёмный.
	{
		leaves_style = "fancy",
		leaves_detail_range = "64",
		foliage_range = "192",
		smooth_lighting = true,
		enable_dynamic_shadows = true,
		shadow_map_texture_size = "2048",
		shadow_map_max_distance = "140",
		enable_post_processing = true,
		enable_bloom = true,
		enable_volumetric_lighting = true,
		enable_auto_exposure = true,
		enable_waving_leaves = true,
		enable_waving_plants = true,
		enable_waving_water = true,
		enable_water_reflections = true,
		enable_translucent_foliage = true,
		enable_3d_clouds = true,
		fxaa = true,
		antialiasing = "none",
	},
	-- Ультра: ничего не срезается вовсе.
	{
		leaves_style = "fancy",
		leaves_detail_range = "0",
		foliage_range = "0",
		smooth_lighting = true,
		enable_dynamic_shadows = true,
		shadow_map_texture_size = "4096",
		shadow_map_max_distance = "210",
		enable_post_processing = true,
		enable_bloom = true,
		enable_volumetric_lighting = true,
		enable_auto_exposure = true,
		enable_waving_leaves = true,
		enable_waving_plants = true,
		enable_waving_water = true,
		enable_water_reflections = true,
		enable_translucent_foliage = true,
		enable_3d_clouds = true,
		fxaa = true,
		antialiasing = "none",
	},
}

-- Настройки теней в «низких» не перечислены: там тени выключены целиком, и
-- размер карты значения не имеет. При сравнении такие пропуски не смотрим.
local function matches(preset)
	for name, want in pairs(preset) do
		if type(want) == "boolean" then
			if core.settings:get_bool(name) ~= want then
				return false
			end
		elseif core.settings:get(name) ~= want then
			return false
		end
	end
	return true
end

local function detect()
	for i, preset in ipairs(presets) do
		if matches(preset) then
			return i
		end
	end
	return CUSTOM
end

local function apply(preset)
	for name, value in pairs(preset) do
		if type(value) == "boolean" then
			core.settings:set_bool(name, value)
		else
			core.settings:set(name, value)
		end
	end
end

return {
	query_text = N_("Quality preset"),
	context = "client",
	get_formspec = function(self, avail_w)
		local idx = detect()
		local list = table.copy(labels)
		-- «Свои» показываем только когда они и есть: иначе это пункт, который
		-- нечего выбирать
		if idx ~= CUSTOM then
			table.remove(list, CUSTOM)
		end

		local w = math.min(avail_w, 4)
		local fs =
			"label[0,0.2;" .. fgettext("Quality preset") .. "]" ..
			("dropdown[0,0.4;%f,0.8;dd_quality;"):format(w) ..
			table.concat(list, ",") .. ";" .. idx .. ";true]" ..
			"label[0,1.5;" .. core.colorize("#bbb",
				fgettext("Sets everything below at once. Changing anything by hand switches this to Custom.")) .. "]"
		return fs, 1.8
	end,
	on_submit = function(self, fields)
		if not fields.dd_quality then
			return false
		end
		local idx = tonumber(fields.dd_quality)
		if not idx or idx == CUSTOM or idx == detect() then
			return false
		end
		apply(presets[idx])
		return true
	end,
}
