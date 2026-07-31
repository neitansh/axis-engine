-- Luanti
-- Copyright (C) 2014 sapier
-- SPDX-License-Identifier: LGPL-2.1-or-later

--------------------------------------------------------------------------------
-- A tabview implementation                                                   --
-- Usage:                                                                     --
-- tabview.create: returns initialized tabview raw element                    --
-- element.add(tab): add a tab declaration                                    --
-- element.handle_buttons()                                                   --
-- element.handle_events()                                                    --
-- element.getFormspec() returns formspec of tabview                          --
--------------------------------------------------------------------------------

--------------------------------------------------------------------------------
local function add_tab(self, tab)
	assert(tab.size == nil or (type(tab.size) == table and tab.size.x ~= nil and tab.size.y ~= nil))
	assert(tab.cbf_formspec ~= nil and type(tab.cbf_formspec) == "function")
	assert(tab.cbf_button_handler == nil or type(tab.cbf_button_handler) == "function")
	assert(tab.cbf_events == nil or type(tab.cbf_events) == "function")

	local newtab = {
		name = tab.name,
		caption = tab.caption,
		button_handler = tab.cbf_button_handler,
		event_handler = tab.cbf_events,
		get_formspec = tab.cbf_formspec,
		tabsize = tab.tabsize,
		formspec_version = tab.formspec_version or 6,
		on_change = tab.on_change,
		sidebar = tab.sidebar ~= false,
		tabdata = {},
	}

	self.tablist[#self.tablist + 1] = newtab

	if self.last_tab_index == #self.tablist then
		self.current_tab = tab.name
		if tab.on_activate ~= nil then
			tab.on_activate(nil, tab.name)
		end
	end
end

--------------------------------------------------------------------------------
local function get_formspec(self)
	if self.hidden or (self.parent ~= nil and self.parent.hidden) then
		return ""
	end

	local tab = self.tablist[self.last_tab_index]

	local content, prepend = tab.get_formspec(self, tab.name, tab.tabdata, tab.tabsize)

	local TOUCH_GUI = core.settings:get_bool("touch_gui")

	local orig_tsize = tab.tabsize or {
		width = self.width,
		height = self.height,
	}

	local tsize = {
		width = orig_tsize.width,
		height = orig_tsize.height,
	}

	-- Sidebar
	local sidebar_width = 0
	local sidebar_gap = 0

	if self.sidebar then
		sidebar_width = self.sidebar.width or 3.0
		sidebar_gap = self.sidebar.gap or 0.4
	end

	-- The whole formspec becomes wider because of the sidebar.
	tsize.width = orig_tsize.width + sidebar_width + sidebar_gap

	tsize.height = tsize.height
		+ TABHEADER_H
		+ (TOUCH_GUI and GAMEBAR_OFFSET_TOUCH or GAMEBAR_OFFSET_DESKTOP)
		+ GAMEBAR_H
		+ FOOTER_H

	if self.parent == nil and not prepend then
		prepend = string.format("size[%f,%f,%s]", tsize.width, tsize.height, dump(self.fixed_size))

		local anchor_pos = TABHEADER_H + orig_tsize.height / 2

		prepend = prepend .. ("anchor[0.5,%f]"):format(anchor_pos / tsize.height)

		if tab.formspec_version then
			prepend = ("formspec_version[%d]"):format(tab.formspec_version) .. prepend
		end
	end

	local formspec = prepend or ""

	formspec = formspec .. "bgcolor[;neither]" .. menu_style.prelude()
		.. ("container[0,%f]"):format(TABHEADER_H)

	-- Layout:
	--
	-- ┌──────────────┐   gap   ┌──────────────────────────┐
	-- │   sidebar    │         │       tab content        │
	-- │              │         │                          │
	-- └──────────────┘         └──────────────────────────┘

	local content_x = sidebar_width + sidebar_gap

	-- Main content container
	formspec = formspec .. ("container[%f,0]"):format(content_x)

	-- Main content background
	formspec = formspec .. menu_style.panel(0, 0, orig_tsize.width, orig_tsize.height)

	-- Existing tab content
	formspec = formspec .. content

	formspec = formspec .. "container_end[]"

	-- Left sidebar
	if self.sidebar then
		local sidebar_x = 0
		local sidebar_height = orig_tsize.height

		local padding = self.sidebar.padding or 0.25
		local button_height = self.sidebar.button_height or 0.75
		local button_spacing = self.sidebar.button_spacing or 0.12

		local button_x = sidebar_x + padding
		local button_width = sidebar_width - padding * 2

		-- Sidebar background
		formspec = formspec
			.. menu_style.panel(sidebar_x, 0, sidebar_width, sidebar_height)

		-- Sidebar buttons
		local button_y = padding

		for i = 1, #self.tablist do
			local current_tab = self.tablist[i]

			if current_tab.sidebar then
				local caption = current_tab.caption

				if type(caption) == "function" then
					caption = caption(self)
				end

				local button_name = self.name .. "_sidebar_" .. i

				local style = ""

				if i == self.last_tab_index then
					style = menu_style.accent(button_name)
				end

				formspec = formspec
					.. style
					.. ("button[%f,%f;%f,%f;%s;%s]"):format(
						button_x,
						button_y,
						button_width,
						button_height,
						button_name,
						core.formspec_escape(caption)
					)

				button_y = button_y + button_height + button_spacing
			end
		end

		if self.sidebar and self.sidebar.actions then
			local action_count = #self.sidebar.actions
			local action_height = button_height
			local action_spacing = button_spacing

			local actions_height = action_count * action_height + (action_count - 1) * action_spacing

			local action_y = sidebar_height - padding - actions_height

			for i, action in ipairs(self.sidebar.actions) do
				local action_name = self.name .. "_sidebar_action_" .. action.name

				formspec = formspec
					.. ("button[%f,%f;%f,%f;%s;%s]"):format(
						button_x,
						action_y,
						button_width,
						action_height,
						action_name,
						core.formspec_escape(action.label)
					)

				action_y = action_y + action_height + action_spacing
			end
		end
	end

	formspec = formspec .. "container_end[]"

	-- Footer
	local footer_y = tsize.height - 0.45
	local footer_width = 9.2
	local footer_overflow = 6.8
	local footer_x = tsize.width - footer_width + footer_overflow

	formspec = formspec
		.. ("container[%f,%f]"):format(footer_x, footer_y)
		.. "style[mainmenu_footer_about;"
		.. "border=false;"
		.. "noclip=true;"
		.. "bgcolor=#00000000;"
		.. "bgcolor_hovered=#00000000;"
		.. "bgcolor_pressed=#00000000;"
		.. "textcolor=#FFFFFF]"
		.. "style_type[label;noclip=true]"
		.. ("label[0,0.08;the Axis © · the iVy Studio · %s]"):format(fgettext("All rights reserved"))
		.. "label[6.0,0.08;Luanti © ·]"
		.. ("button[6.7,-0.13;2.5,0.45;mainmenu_footer_about;%s]"):format(fgettext("Details"))
		.. "container_end[]"

	return formspec
end

--------------------------------------------------------------------------------
local function handle_buttons(self, fields)
	if self.hidden then
		return false
	end

	if self:handle_tab_buttons(fields) then
		return true
	end

	if self.sidebar then
		for i = 1, #self.tablist do
			local button_name = self.name .. "_sidebar_" .. i

			if fields[button_name] then
				self:set_tab(self.tablist[i].name)
				return true
			end
		end
	end

	if self.sidebar and self.sidebar.actions then
		for _, action in ipairs(self.sidebar.actions) do
			local button_name = self.name .. "_sidebar_action_" .. action.name

			if fields[button_name] then
				return action.on_click(self)
			end
		end
	end

	if fields["mainmenu_footer_about"] then
		self:set_tab("about")
		return true
	end

	if self.glb_btn_handler ~= nil and self.glb_btn_handler(self, fields) then
		return true
	end

	local tab = self.tablist[self.last_tab_index]
	if tab.button_handler ~= nil then
		return tab.button_handler(self, fields, tab.name, tab.tabdata)
	end

	return false
end

--------------------------------------------------------------------------------
local function handle_events(self, event)
	if self.hidden then
		return false
	end

	if self.glb_evt_handler ~= nil and self.glb_evt_handler(self, event) then
		return true
	end

	local tab = self.tablist[self.last_tab_index]
	if tab.evt_handler ~= nil then
		return tab.evt_handler(self, event, tab.name, tab.tabdata)
	end

	return false
end

--------------------------------------------------------------------------------
local function switch_to_tab(self, index)
	--first call on_change for tab to leave
	if self.tablist[self.last_tab_index].on_change ~= nil then
		self.tablist[self.last_tab_index].on_change("LEAVE", self.current_tab, self.tablist[index].name)
	end

	--update tabview data
	self.last_tab_index = index
	local old_tab = self.current_tab
	self.current_tab = self.tablist[index].name

	if self.autosave_tab then
		core.settings:set(self.name .. "_LAST", self.current_tab)
	end

	-- call for tab to enter
	if self.tablist[index].on_change ~= nil then
		self.tablist[index].on_change("ENTER", old_tab, self.current_tab)
	end
end

--------------------------------------------------------------------------------
local function handle_tab_buttons(self, fields)
	--save tab selection to config file
	if fields[self.name] then
		local index = tonumber(fields[self.name])
		switch_to_tab(self, index)
		return true
	end

	return false
end

--------------------------------------------------------------------------------
local function set_tab_by_name(self, name)
	for i = 1, #self.tablist, 1 do
		if self.tablist[i].name == name then
			switch_to_tab(self, i)
			return true
		end
	end

	return false
end

--------------------------------------------------------------------------------
local function hide_tabview(self)
	self.hidden = true

	--call on_change as we're not gonna show self tab any longer
	if self.tablist[self.last_tab_index].on_change ~= nil then
		self.tablist[self.last_tab_index].on_change("LEAVE", self.current_tab, nil)
	end
end

--------------------------------------------------------------------------------
local function show_tabview(self)
	self.hidden = false

	-- call for tab to enter
	if self.tablist[self.last_tab_index].on_change ~= nil then
		self.tablist[self.last_tab_index].on_change("ENTER", nil, self.current_tab)
	end
end

local tabview_metatable = {
	add = add_tab,
	handle_buttons = handle_buttons,
	handle_events = handle_events,
	get_formspec = get_formspec,
	show = show_tabview,
	hide = hide_tabview,
	delete = function(self)
		ui.delete(self)
	end,
	set_parent = function(self, parent)
		self.parent = parent
	end,
	set_autosave_tab = function(self, value)
		self.autosave_tab = value
	end,
	set_tab = set_tab_by_name,
	set_global_button_handler = function(self, handler)
		self.glb_btn_handler = handler
	end,
	set_global_event_handler = function(self, handler)
		self.glb_evt_handler = handler
	end,
	set_fixed_size = function(self, state)
		self.fixed_size = state
	end,
	set_sidebar = function(self, v)
		self.sidebar = v
	end,
	handle_tab_buttons = handle_tab_buttons,
}

tabview_metatable.__index = tabview_metatable

--------------------------------------------------------------------------------
function tabview_create(name, size, tabheaderpos)
	local self = {}

	self.name = name
	self.type = "toplevel"
	self.width = size.x
	self.height = size.y
	self.header_x = tabheaderpos.x
	self.header_y = tabheaderpos.y

	setmetatable(self, tabview_metatable)

	self.fixed_size = true
	self.hidden = true
	self.current_tab = nil
	self.last_tab_index = 1
	self.tablist = {}

	self.autosave_tab = false

	ui.add(self)
	return self
end
