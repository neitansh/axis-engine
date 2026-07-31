-- Luanti
-- Copyright (C) 2022 rubenwardy
-- SPDX-License-Identifier: LGPL-2.1-or-later

local make = {}


-- This file defines various component constructors, of the form:
--
--     make.component(setting)
--
-- `setting` is a table representing the settingtype.
--
-- A component is a table with the following:
--
-- * `full_width`: (Optional) true if the component shouldn't reserve space for the reset button.
-- * `info_text`: (Optional) string, informational text shown in a tooltip.
-- * `setting`: (Optional) the setting.
-- * `max_w`: (Optional) maximum width, `avail_w` will never exceed this.
-- * `resettable`: (Optional) if this is true, a reset button is shown.
-- * `get_formspec = function(self, avail_w)`:
--     * `avail_w` is the available width for the component.
--     * Returns `fs, used_height`.
--     * `fs` is a string for the formspec.
--       Components should be relative to `0,0`, and not exceed `avail_w` or the returned `used_height`.
--     * `used_height` is the space used by components in `fs`.
-- * `spacing`: (Optional) the vertical margin to be added before the component (default 0.25)
-- * `on_submit = function(self, fields, parent)`:
--     * `fields`: submitted formspec fields
--     * `parent`: the fstk element for the settings UI, use to show dialogs
--     * Return true if the event was handled, to prevent future components receiving it.


-- Every ordinary setting is drawn as one row: name (and a short explanation)
-- on the left, the control that changes it on the right.
local ROW_H = 0.8
local DESC_H = 0.62
-- Roughly what fits on the two lines a description gets
local DESC_CHARS = 64
local CONTROL_W = 3.6
local GAP = 0.2

local COLOR_DESC = "#9aa0a6"
local COLOR_HEADING = menu_style.HEADING


local function get_label(setting)
	local show_technical_names = core.settings:get_bool("show_technical_names")
	if not show_technical_names and setting.readable_name then
		return fgettext(setting.readable_name)
	end
	return setting.name
end


-- LuaJIT has no utf8 library, so character counting is done by hand.
-- Returns the byte index the given character ends at, or nil if the text is
-- shorter than that.
local function byte_offset_of_char(text, count)
	local chars, i = 0, 1

	while i <= #text do
		local byte = text:byte(i)
		local width = byte < 0x80 and 1
			or byte < 0xE0 and 2
			or byte < 0xF0 and 3
			or 4

		chars = chars + 1
		if chars > count then
			return i - 1
		end
		i = i + width
	end

	return nil
end


-- Short explanation shown under the name. The area label wraps and truncates
-- on its own, so the text does not need to be shortened here.
local function get_description(setting)
	local comment = setting.comment
	if not comment or comment == "" then
		return nil
	end
	local text = fgettext_ne(comment):gsub("%s*\n%s*", " ")
	-- One sentence keeps every row the same height and still says what it does
	text = text:match("^(.-[%.!%?])%s") or text

	-- The row only has two lines; cut on a word so nothing renders half a line
	local cut = byte_offset_of_char(text, DESC_CHARS)
	if cut then
		local shortened = text:sub(1, cut)
		text = (shortened:match("^(.*)%s%S*$") or shortened) .. "…"
	end

	return core.formspec_escape(text)
end


local function is_valid_number(value)
	return type(value) == "number" and not (value ~= value or value >= math.huge or value <= -math.huge)
end


-- Geometry of a single row for the given width.
local function layout(avail_w, has_desc)
	local control_x = avail_w - CONTROL_W
	return {
		height = ROW_H + (has_desc and DESC_H or 0),
		label_w = math.max(1, control_x - GAP),
		control_x = control_x,
		-- Vertical centre of the control column
		control_y = ROW_H / 2,
	}
end


local function render_label(l, label, desc)
	local fs = {
		"style_type[label;valign=center]",
		("label[0,0;%f,%f;%s]"):format(l.label_w, ROW_H, label),
	}

	if desc then
		fs[#fs + 1] = ("style_type[label;textcolor=%s;font_size=*0.9;valign=top]"):format(COLOR_DESC)
		fs[#fs + 1] = ("label[0,%f;%f,%f;%s]"):format(ROW_H - 0.04, l.label_w, DESC_H, desc)
		fs[#fs + 1] = "style_type[label;textcolor=;font_size=]"
	end

	fs[#fs + 1] = "style_type[label;valign=top]"

	return table.concat(fs)
end


function make.heading(text, info_text)
	return {
		full_width = true,
		info_text = info_text,
		spacing = 0.45,
		get_formspec = function(self, avail_w)
			return ("style_type[label;textcolor=%s;font=bold]" ..
				"label[0,0;%f,0.6;%s]" ..
				"style_type[label;textcolor=;font=]" ..
				"box[0,0.62;%f,0.03;#ffffff22]"):format(
					COLOR_HEADING, avail_w, core.formspec_escape(text), avail_w), 0.75
		end,
	}
end


-- Reveals the settings a page keeps out of the way by default.
function make.expander(expanded)
	return {
		full_width = true,
		spacing = 0.5,
		get_formspec = function(self, avail_w)
			return ("style[toggle_advanced;bgcolor=#2c2c2c;border=false]" ..
				"button[0,0;%f,0.8;toggle_advanced;%s]"):format(
					avail_w,
					(expanded and "▼  " or "▶  ") ..
						fgettext("Advanced settings")), 0.8
		end,
	}
end


function make.unavail_list(settings)
	return {
		full_width = true,
		get_formspec = function(self, avail_w)
			local h = 0.2
			local fs = {}
			for _, setting in ipairs(settings) do
				fs[#fs + 1] = ("label[0.3,%f;%s]"):format(h,
					core.colorize("#bbb", core.formspec_escape(get_label(setting))))
				h = h + 0.4
			end
			return table.concat(fs, ""), h
		end,
	}
end

function make.note(text)
	return {
		full_width = true,
		get_formspec = function(self, avail_w)
			return ("style_type[label;textcolor=%s;font_size=*0.9]" ..
				"label[0,0;%f,0.45;%s]" ..
				"style_type[label;textcolor=;font_size=]"):format(
					COLOR_DESC, avail_w, core.formspec_escape(text)), 0.45
		end,
	}
end


--- Text entry, used for values that have no bounds to slide between.
---
--- @param converter Function to coerce values from strings.
--- @param validator Validator function, optional. Returns true when valid.
--- @param stringifier Function to convert values to strings, optional.
local function make_text_entry(converter, validator, stringifier)
	return function(setting)
		return {
			setting = setting,

			get_formspec = function(self, avail_w)
				local value = core.settings:get(setting.name) or setting.default
				self.resettable = core.settings:has(setting.name)

				local desc = get_description(setting)
				local l = layout(avail_w, desc ~= nil)
				local field_w = CONTROL_W - 0.75

				local fs = {
					render_label(l, get_label(setting), desc),
					("field[%f,%f;%f,0.7;%s;;%s]"):format(
						l.control_x, l.control_y - 0.35, field_w,
						setting.name, core.formspec_escape(value)),
					("field_enter_after_edit[%s;true]"):format(setting.name),
					-- for pause menu env
					("field_close_on_enter[%s;false]"):format(setting.name),
					("button[%f,%f;0.7,0.7;%s;%s]"):format(
						l.control_x + field_w + 0.05, l.control_y - 0.35,
						"set_" .. setting.name, fgettext("Set")),
				}

				return table.concat(fs), l.height
			end,

			on_submit = function(self, fields)
				if fields["set_" .. setting.name] or fields.key_enter_field == setting.name then
					local value = converter(fields[setting.name])
					if value == nil or (validator and not validator(value)) then
						return true
					end

					if setting.min then
						value = math.max(value, setting.min)
					end
					if setting.max then
						value = math.min(value, setting.max)
					end
					core.settings:set(setting.name, (stringifier or tostring)(value))
					return true
				end
			end,
		}
	end
end


local float_to_string = function(x)
	local str = tostring(x)
	if str:match("^[+-]?%d+$") then
		str = str .. ".0"
	end
	return str
end

local text_float = make_text_entry(tonumber, is_valid_number, float_to_string)
local text_int = make_text_entry(function(x)
	local value = tonumber(x)
	return value and math.floor(value)
end, is_valid_number)

make.string = make_text_entry(tostring, nil)


-- Bounded numbers get a slider, which is the whole point of having a settings
-- menu instead of editing minetest.conf by hand.
local SLIDER_STEPS = 1000

local function make_slider(is_int)
	return function(setting)
		-- Without both bounds there is nothing to slide between.
		if not (is_valid_number(setting.min) and is_valid_number(setting.max)
				and setting.max > setting.min) then
			return (is_int and text_int or text_float)(setting)
		end

		local min, max = setting.min, setting.max
		-- Small integer ranges map onto the scrollbar one to one, which keeps
		-- every reachable value exact.
		local direct = is_int and (max - min) <= SLIDER_STEPS
		local sb_min = direct and min or 0
		local sb_max = direct and max or SLIDER_STEPS

		local function to_slider(value)
			if direct then
				return math.floor(value + 0.5)
			end
			return math.floor((value - min) / (max - min) * SLIDER_STEPS + 0.5)
		end

		local function from_slider(pos)
			if direct then
				return pos
			end
			local value = min + (pos / SLIDER_STEPS) * (max - min)
			if is_int then
				return math.floor(value + 0.5)
			end
			-- Two decimals is as fine as anyone needs to aim with a slider
			return math.floor(value * 100 + 0.5) / 100
		end

		local function format_value(value)
			if is_int then
				return tostring(math.floor(value))
			end
			return (("%.2f"):format(value):gsub("%.?0+$", ""))
		end

		return {
			setting = setting,

			get_formspec = function(self, avail_w)
				local value = tonumber(core.settings:get(setting.name))
						or tonumber(setting.default) or min
				value = math.min(math.max(value, min), max)
				self.resettable = core.settings:has(setting.name)

				local desc = get_description(setting)
				local l = layout(avail_w, desc ~= nil)
				local value_w = 1.25
				local slider_w = CONTROL_W - value_w - 0.15

				local fs = {
					render_label(l, get_label(setting), desc),
					("scrollbaroptions[min=%d;max=%d;smallstep=%d;largestep=%d;thumbsize=%d;arrows=hide]")
						:format(sb_min, sb_max,
							direct and 1 or math.max(1, math.floor(SLIDER_STEPS / 100)),
							direct and math.max(1, math.floor((max - min) / 10))
								or math.max(1, math.floor(SLIDER_STEPS / 10)),
							math.max(1, math.floor((sb_max - sb_min) / 20))),
					("scrollbar[%f,%f;%f,0.35;horizontal;%s;%d]"):format(
						l.control_x, l.control_y - 0.175, slider_w,
						setting.name, to_slider(value)),
					-- The field lets you type an exact value the slider cannot hit
					("box[%f,%f;%f,0.65;#333941]"):format(
						l.control_x + slider_w + 0.15, l.control_y - 0.325, value_w),
					("box[%f,%f;%f,0.61;#101317]"):format(
						l.control_x + slider_w + 0.17, l.control_y - 0.305, value_w - 0.04),
					("field[%f,%f;%f,0.65;%s;;%s]"):format(
						l.control_x + slider_w + 0.15, l.control_y - 0.325, value_w,
						setting.name .. "_value", format_value(value)),
					("field_enter_after_edit[%s;true]"):format(setting.name .. "_value"),
					("field_close_on_enter[%s;false]"):format(setting.name .. "_value"),
					-- Restore the defaults for any plain scrollbar drawn later
					"scrollbaroptions[min=0;max=1000;smallstep=10;largestep=100;thumbsize=1;arrows=default]",
				}

				return table.concat(fs), l.height
			end,

			on_submit = function(self, fields)
				local function store(value)
					value = math.min(math.max(value, min), max)
					core.settings:set(setting.name,
						is_int and tostring(math.floor(value + 0.5)) or float_to_string(value))
					return true
				end

				local typed = fields[setting.name .. "_value"]
				if typed and fields.key_enter_field == setting.name .. "_value" then
					local value = tonumber(typed)
					if not is_valid_number(value) then
						return true
					end
					return store(value)
				end

				local event = core.explode_scrollbar_event(fields[setting.name])
				if event.type ~= "CHG" then
					return false
				end

				return store(from_slider(event.value))
			end,
		}
	end
end

make.int = make_slider(true)
make.float = make_slider(false)


function make.bool(setting)
	return {
		setting = setting,

		get_formspec = function(self, avail_w)
			local value = core.settings:get_bool(setting.name, core.is_yes(setting.default))
			self.resettable = core.settings:has(setting.name)

			local desc = get_description(setting)
			local l = layout(avail_w, desc ~= nil)
			local button = "toggle_" .. setting.name

			local fs = {
				render_label(l, get_label(setting), desc),
				-- A switch reads at a glance where a checkbox needs a second look
				value and menu_style.accent(button) or "",
				("button[%f,%f;%f,0.7;%s;%s]"):format(
					l.control_x, l.control_y - 0.35, CONTROL_W, button,
					value and fgettext("Enabled") or fgettext("Disabled")),
			}

			return table.concat(fs), l.height
		end,

		on_submit = function(self, fields)
			if not fields["toggle_" .. setting.name] then
				return false
			end

			local value = core.settings:get_bool(setting.name, core.is_yes(setting.default))
			core.settings:set_bool(setting.name, not value)
			return true
		end,
	}
end


function make.enum(setting)
	return {
		setting = setting,

		get_formspec = function(self, avail_w)
			local value = core.settings:get(setting.name) or setting.default
			self.resettable = core.settings:has(setting.name)

			local labels = setting.option_labels or {}
			local items = {}
			for i, option in ipairs(setting.values) do
				items[i] = core.formspec_escape(labels[option] or option)
			end

			local desc = get_description(setting)
			local l = layout(avail_w, desc ~= nil)

			local fs = {
				render_label(l, get_label(setting), desc),
				("dropdown[%f,%f;%f,0.7;%s;%s;%d;true]"):format(
					l.control_x, l.control_y - 0.35, CONTROL_W,
					setting.name, table.concat(items, ","),
					table.indexof(setting.values, value)),
			}

			return table.concat(fs), l.height
		end,

		on_submit = function(self, fields)
			local old_value = core.settings:get(setting.name) or setting.default
			local idx = tonumber(fields[setting.name]) or 0
			local value = setting.values[idx]
			if value == nil or value == old_value then
				return false
			end

			core.settings:set(setting.name, value)
			return true
		end,
	}
end


local function make_path(setting)
	return {
		setting = setting,

		get_formspec = function(self, avail_w)
			local value = core.settings:get(setting.name) or setting.default
			self.resettable = core.settings:has(setting.name)

			local fs = ("field[0,0.3;%f,0.8;%s;%s;%s]"):format(
				avail_w - 3, setting.name, get_label(setting), core.formspec_escape(value))
			fs = fs .. ("field_enter_after_edit[%s;true]"):format(setting.name)
			fs = fs .. ("field_close_on_enter[%s;false]"):format(setting.name) -- for pause menu env
			fs = fs .. ("button[%f,0.3;1.5,0.8;%s;%s]"):format(avail_w - 3, "pick_" .. setting.name, fgettext("Browse"))
			fs = fs .. ("button[%f,0.3;1.5,0.8;%s;%s]"):format(avail_w - 1.5, "set_" .. setting.name, fgettext("Set"))

			return fs, 1.1
		end,

		on_submit = function(self, fields)
			local dialog_name = "dlg_path_" .. setting.name
			if fields["pick_" .. setting.name] then
				local is_file = setting.type ~= "path"
				core.show_path_select_dialog(dialog_name,
					is_file and fgettext_ne("Select file") or fgettext_ne("Select directory"), is_file)
				return true
			end
			if fields[dialog_name .. "_accepted"] then
				local value = fields[dialog_name .. "_accepted"]
				if value ~= nil then
					core.settings:set(setting.name, value)
				end
				return true
			end
			if fields["set_" .. setting.name] or fields.key_enter_field == setting.name then
				local value = fields[setting.name]
				if value ~= nil then
					core.settings:set(setting.name, value)
				end
				return true
			end
		end,
	}
end

if PLATFORM == "Android" or INIT == "pause_menu" then
	-- The Irrlicht file picker doesn't work on Android.
	-- Access to the Irrlicht file picker isn't implemented in the pause menu.
	-- We want to delete the Irrlicht file picker anyway, so any time spent on
	-- that would be wasted.
	make.path = make.string
	make.filepath = make.string
else
	make.path = make_path
	make.filepath = make_path
end


function make.v3f(setting)
	return {
		setting = setting,

		get_formspec = function(self, avail_w)
			local value = vector.from_string(core.settings:get(setting.name) or setting.default)
			self.resettable = core.settings:has(setting.name)

			-- Allocate space for "Set" button
			avail_w = avail_w - 1

			local fs = "label[0,0.1;" .. get_label(setting) .. "]"

			local field_width = (avail_w - 3*0.25) / 3

			fs = fs .. ("field[%f,0.6;%f,0.8;%s;%s;%s]"):format(
				0, field_width, setting.name .. "_x", "X", value.x)
			fs = fs .. ("field[%f,0.6;%f,0.8;%s;%s;%s]"):format(
				field_width + 0.25, field_width, setting.name .. "_y", "Y", value.y)
			fs = fs .. ("field[%f,0.6;%f,0.8;%s;%s;%s]"):format(
				2 * (field_width + 0.25), field_width, setting.name .. "_z", "Z", value.z)

			fs = fs .. ("field_enter_after_edit[%s;true]"):format(setting.name .. "_x")
			fs = fs .. ("field_enter_after_edit[%s;true]"):format(setting.name .. "_y")
			fs = fs .. ("field_enter_after_edit[%s;true]"):format(setting.name .. "_z")
			-- for pause menu env
			fs = fs .. ("field_close_on_enter[%s;false]"):format(setting.name .. "_x")
			fs = fs .. ("field_close_on_enter[%s;false]"):format(setting.name .. "_y")
			fs = fs .. ("field_close_on_enter[%s;false]"):format(setting.name .. "_z")

			fs = fs .. ("button[%f,0.6;1,0.8;%s;%s]"):format(avail_w, "set_" .. setting.name, fgettext("Set"))

			return fs, 1.4
		end,

		on_submit = function(self, fields)
			if fields["set_" .. setting.name]  or
					fields.key_enter_field == setting.name .. "_x" or
					fields.key_enter_field == setting.name .. "_y" or
					fields.key_enter_field == setting.name .. "_z" then
				local x = tonumber(fields[setting.name .. "_x"])
				local y = tonumber(fields[setting.name .. "_y"])
				local z = tonumber(fields[setting.name .. "_z"])
				if is_valid_number(x) and is_valid_number(y) and is_valid_number(z) then
					core.settings:set(setting.name, vector.new(x, y, z):to_string())
				else
					core.log("error", "Invalid vector: " .. dump({x, y, z}))
				end
				return true
			end
		end,
	}
end


function make.flags(setting)
	local checkboxes = {}

	return {
		setting = setting,

		get_formspec = function(self, avail_w)
			local fs = {
				"label[0,0.1;" .. get_label(setting) .. "]",
			}

			self.resettable = core.settings:has(setting.name)

			checkboxes = {}
			for _, name in ipairs(setting.possible) do
				checkboxes[name] = false
			end
			local function apply_flags(flag_string, what)
				local prefixed_flags = {}
				for _, name in ipairs(flag_string:split(",")) do
					prefixed_flags[name:trim()] = true
				end
				for _, name in ipairs(setting.possible) do
					local enabled = prefixed_flags[name]
					local disabled = prefixed_flags["no" .. name]
					if enabled and disabled then
						core.log("warning", "Flag " .. name .. " in " .. what .. " " ..
								setting.name .. " both enabled and disabled, ignoring")
					elseif enabled then
						checkboxes[name] = true
					elseif disabled then
						checkboxes[name] = false
					end
				end
			end
			-- First apply the default, which is necessary since flags
			-- which are not overridden may be missing from the value.
			apply_flags(setting.default, "default for setting")
			local value = core.settings:get(setting.name)
			if value then
				apply_flags(value, "setting")
			end

			local columns = math.max(math.floor(avail_w / 2.5), 1)
			local column_width = avail_w / columns
			local x = 0
			local y = 0.55

			for _, possible in ipairs(setting.possible) do
				if x >= avail_w then
					x = 0
					y = y + 0.5
				end

				local is_checked = checkboxes[possible]
				fs[#fs + 1] = ("checkbox[%f,%f;%s;%s;%s]"):format(
					x, y, setting.name .. "_" .. possible,
					core.formspec_escape(possible), tostring(is_checked))
				x = x + column_width
			end

			return table.concat(fs, ""), y + 0.25
		end,

		on_submit = function(self, fields)
			local changed = false
			for name, _ in pairs(checkboxes) do
				local value = fields[setting.name .. "_" .. name]
				if value ~= nil then
					checkboxes[name] = core.is_yes(value)
					changed = true
				end
			end

			if changed then
				local values = {}
				for _, name in ipairs(setting.possible) do
					if checkboxes[name] then
						table.insert(values, name)
					else
						table.insert(values, "no" .. name)
					end
				end

				core.settings:set(setting.name, table.concat(values, ","))
			end
			return changed
		end
	}
end


local function make_noise_params(setting)
	return {
		setting = setting,

		get_formspec = function(self, avail_w)
			-- The "defaults" noise parameter flag doesn't reset a noise
			-- setting to its default value, so we offer a regular reset button.
			self.resettable = core.settings:has(setting.name)

			local fs = "label[0,0.4;" .. get_label(setting) .. "]" ..
					("button[%f,0;2.5,0.8;%s;%s]"):format(avail_w - 2.5, "edit_" .. setting.name, fgettext("Edit"))
			return fs, 0.8
		end,

		on_submit = function(self, fields, tabview)
			if fields["edit_" .. setting.name] then
				local dlg = create_change_mapgen_flags_dlg(setting)
				dlg:set_parent(tabview)
				tabview:hide()
				dlg:show()

				return true
			end
		end,
	}
end

local function has_keybinding_conflict(t1, t2)
	for _, v1 in pairs(t1) do
		for _, v2 in pairs(t2) do
			if core.are_keycodes_equal(v1, v2) then
				return true
			end
		end
	end
	return false
end

local function get_key_setting(name)
	return core.settings:get(name):split("|")
end

-- Setting names where an empty field shall be shown to assign new keybindings.
local key_add_empty = {}

function make.key(setting)
	local btn_bind = "bind_" .. setting.name
	local btn_clear = "unbind_" .. setting.name
	local btn_add = "add_" .. setting.name
	local function add_conflict_warnings(fs, height)
		local value = get_key_setting(setting.name)
		if value == "" then
			return height
		end

		local critical_keys = {
			keymap_drop = true,
			keymap_dig = true,
			keymap_place = true,
		}

		for _, o in ipairs(core.full_settingtypes) do
			if o.type == "key" and o.name ~= setting.name and
					has_keybinding_conflict(get_key_setting(o.name), value) then

				local is_current_close_world = setting.name == "keymap_close_world"
				local is_other_close_world = o.name == "keymap_close_world"
				local is_current_critical = critical_keys[setting.name]
				local is_other_critical = critical_keys[o.name]

				if (is_other_critical or is_current_critical) or
						(not is_current_close_world and not is_other_close_world) then
					table.insert(fs, ("label[0,%f;%s]"):format(height + 0.3,
							core.colorize(mt_color_orange, fgettext([[Conflicts with "$1"]], fgettext(o.readable_name)))))
					height = height + 0.6
				end
			end
		end
		return height
	end

	local add_empty = key_add_empty[setting.name]
	key_add_empty[setting.name] = nil

	return {
		setting = setting,
		spacing = 0.1,

		get_formspec = function(self, avail_w)
			local value_string = core.settings:get(setting.name) or ""
			local default_value = setting.default or ""
			self.resettable = core.settings:has(setting.name) and (value_string ~= default_value)
			local value_width = math.max(2.5, avail_w / 2)
			local value = get_key_setting(setting.name)
			local fs = {
				("label[0,0.4;%s]"):format(get_label(setting)),
			}

			local function add_keybinding_row(idx)
				local btn_bind_width = value_width - 1.6
				local has_value = value[idx]
				local y = (idx - 1) * 0.8
				if not has_value then
					btn_bind_width = idx == 1 and value_width or (value_width - 0.8)
				end
				table.insert(fs, ("button_key[%f,%f;%f,0.8;%s_%d;%s]"):format(
						value_width, y, btn_bind_width,
						btn_bind, idx, core.formspec_escape(value[idx] or "")))
				if has_value then
					table.insert(fs, ("image_button[%f,%f;0.8,0.8;%s;%s_%d;]"):format(
							avail_w - 1.6, y,
							core.formspec_escape(defaulttexturedir .. "clear.png"),
							btn_clear, idx))
					table.insert(fs, ("tooltip[%s_%d;%s]"):format(btn_clear, idx,
							fgettext("Remove keybinding")))
				end
			end

			local height = #value * 0.8
			for i = 1, #value do
				add_keybinding_row(i)
			end
			if add_empty or #value == 0 then
				add_keybinding_row(#value+1)
				height = height + 0.8
			else
				table.insert(fs, ("image_button[%f,%f;0.8,0.8;%s;%s;]"):format(
						avail_w - 0.8, height - 0.8,
						core.formspec_escape(defaulttexturedir .. "plus.png"), btn_add))
				table.insert(fs, ("tooltip[%s;%s]"):format(btn_add, fgettext("Add keybinding")))
			end

			height = add_conflict_warnings(fs, height)
			return table.concat(fs), height
		end,

		on_submit = function(self, fields)
			if fields[btn_add] then
				key_add_empty[setting.name] = true
				return true
			end
			local value = get_key_setting(setting.name)
			for i = 1, #value + 1 do
				if fields[("%s_%d"):format(btn_bind, i)] then
					value[i] = fields[("%s_%d"):format(btn_bind, i)]
					core.settings:set(setting.name, table.concat(value, "|"))
					return true
				elseif fields[("%s_%d"):format(btn_clear, i)] then
					table.remove(value, i)
					core.settings:set(setting.name, table.concat(value, "|"))
					return true
				end
			end
		end,
	}
end

if INIT == "pause_menu" then
	-- Making the noise parameter dialog work in the pause menu settings would
	-- require porting "FSTK" (at least the dialog API) from the mainmenu formspec
	-- API to the in-game formspec API.
	-- There's no reason you'd want to adjust mapgen noise parameter settings
	-- in-game (they only apply to new worlds, hidden as [world_creation]),
	-- so there's no reason to implement this.
	local empty = function()
		return { get_formspec = function() return "", 0 end }
	end
	make.noise_params_2d = empty
	make.noise_params_3d = empty
else
	make.noise_params_2d = make_noise_params
	make.noise_params_3d = make_noise_params
end


return make
