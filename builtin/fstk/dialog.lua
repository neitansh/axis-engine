-- Luanti
-- Copyright (C) 2014 sapier
-- SPDX-License-Identifier: LGPL-2.1-or-later

-- The engine only accepts these before anything else, and stops looking for
-- size[] at the first element that is not one of them.
local LEADING_ELEMENTS = {
	formspec_version = true,
	size = true,
	invsize = true,
	position = true,
	anchor = true,
	padding = true,
	no_prepend = true,
}

--- Puts shared styles into a formspec without displacing its leading elements.
function dialog_insert_styles(formspec, styles)
	local pos = 1

	while true do
		local _, stop, name = formspec:find("^%s*([%a_]+)%[[^%]]*%]", pos)
		if not stop or not LEADING_ELEMENTS[name] then
			break
		end
		pos = stop + 1
	end

	return formspec:sub(1, pos - 1) .. styles .. formspec:sub(pos)
end


local function dialog_event_handler(self,event)
	if self.user_eventhandler == nil or
		self.user_eventhandler(event) == false then

		--close dialog on esc
		if event == "MenuQuit" then
			self:delete()
			return true
		end
	end
end

local dialog_metatable = {
	eventhandler = dialog_event_handler,
	get_formspec = function(self)
				if self.hidden then
					return
				end

				local fs = self.formspec(self.data)
				if not fs or fs == "" then
					return fs
				end

				return dialog_insert_styles(fs, menu_style.prelude())
			end,
	handle_buttons = function(self,fields)
				if not self.hidden then return self.buttonhandler(self,fields) end
			end,
	handle_events  = function(self,event)
				if not self.hidden then return self.eventhandler(self,event) end
			end,
	hide = function(self)
		if not self.hidden then
			self.hidden = true
			self.eventhandler(self, "DialogHide")
		end
	end,
	show = function(self)
		if self.hidden then
			self.hidden = false
			self.eventhandler(self, "DialogShow")
		end
	end,
	delete = function(self)
			if self.parent ~= nil then
				self.parent:show()
			end
			ui.delete(self)
		end,
	set_parent = function(self,parent) self.parent = parent end
}
dialog_metatable.__index = dialog_metatable

function dialog_create(name,get_formspec,buttonhandler,eventhandler)
	local self = {}

	self.name = name
	self.type = "toplevel"
	self.hidden = true
	self.data = {}

	self.formspec      = get_formspec
	self.buttonhandler = buttonhandler
	self.user_eventhandler  = eventhandler

	setmetatable(self,dialog_metatable)

	ui.add(self)
	return self
end

-- "message" must already be formspec-escaped, e.g. via fgettext or
-- core.formspec_escape.
function messagebox(name, message)
	return dialog_create(name,
			function()
				return ui.get_message_formspec("", message, "ok")
			end,
			function(this, fields)
				if fields.ok then
					this:delete()
					return true
				end
			end,
			nil)
end
