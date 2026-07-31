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

	-- The sidebar data is kept for the landing page; the tab view itself is
	-- just content, and Escape takes you back to that page.
	local content_x = 0

	-- Room for the logo strip above and the footer below the content
	tsize.height = tsize.height
		+ TABHEADER_H
		+ (TOUCH_GUI and GAMEBAR_OFFSET_TOUCH or GAMEBAR_OFFSET_DESKTOP)
		+ FOOTER_H

	if self.parent == nil and not prepend then
		prepend = string.format("size[%f,%f,%s]", tsize.width, tsize.height, dump(self.fixed_size))

		local anchor_pos = TABHEADER_H + (orig_tsize.height + FOOTER_H) / 2

		prepend = prepend .. ("anchor[0.5,%f]"):format(anchor_pos / tsize.height)

		if tab.formspec_version then
			prepend = ("formspec_version[%d]"):format(tab.formspec_version) .. prepend
		end
	end

	local formspec = prepend or ""

	formspec = formspec .. "bgcolor[;neither]" .. menu_style.prelude()
		.. ("container[0,%f]"):format(TABHEADER_H)

	-- One card holds the content and the footer, so the screen reads as a
	-- single object rather than a panel floating over the world.
	local card_h = orig_tsize.height + FOOTER_H

	formspec = formspec .. menu_style.panel(0, 0, tsize.width, card_h)

	-- Main content container
	formspec = formspec .. ("container[%f,0]"):format(content_x)

	-- Existing tab content
	formspec = formspec .. content

	formspec = formspec .. "container_end[]"

	formspec = formspec .. "container_end[]"

	-- Footer strip spanning both columns, inside the card
	local footer_h = 0.6
	local footer_y = TABHEADER_H + orig_tsize.height + menu_style.SPACE.xs

	formspec = formspec
		.. ("container[0,%f]"):format(footer_y)
		.. menu_style.divider(menu_style.SPACE.lg, 0, tsize.width - menu_style.SPACE.lg * 2)
		.. menu_style.caption(menu_style.SPACE.lg, menu_style.SPACE.sm,
			tsize.width - 2.6 - menu_style.SPACE.lg, footer_h,
			core.formspec_escape("the Axis © the iVy Studio · " ..
				fgettext_ne("All rights reserved")))
		.. menu_style.ghost("mainmenu_footer_about")
		.. ("style[mainmenu_footer_about;font_size=*0.9]")
		.. ("button[%f,%f;2.2,%f;mainmenu_footer_about;%s]"):format(
			tsize.width - 2.2 - menu_style.SPACE.lg, menu_style.SPACE.xs, footer_h,
			fgettext("Details"))
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
