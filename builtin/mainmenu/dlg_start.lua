-- the Axis
-- SPDX-License-Identifier: LGPL-2.1-or-later

-- Landing page shown on startup: the sidebar of the main tabview, centred on
-- screen below the engine logo. Picking an entry opens the tabview itself.

local BUTTON_W = 5.0
local BUTTON_H = 0.9
local BUTTON_SPACING = 0.18
local PADDING = 0.5

local start_page

-- Builds the entry list from the tabview, so the landing page and the sidebar
-- always offer the same choices.
local function get_entries(tabview)
	local entries = {}

	for i, tab in ipairs(tabview.tablist) do
		if tab.sidebar then
			local caption = tab.caption
			if type(caption) == "function" then
				caption = caption(tabview)
			end

			entries[#entries + 1] = {
				name = "start_tab_" .. i,
				label = caption,
				tab_index = i,
			}
		end
	end

	local sidebar = tabview.sidebar
	if sidebar and sidebar.actions then
		for _, action in ipairs(sidebar.actions) do
			entries[#entries + 1] = {
				name = "start_action_" .. action.name,
				label = action.label,
				action = action,
			}
		end
	end

	return entries
end

local function get_formspec(data)
	local tabview = data.tabview
	local sidebar = tabview.sidebar or {}
	local entries = get_entries(tabview)

	local width = BUTTON_W + PADDING * 2
	local height = PADDING * 2 + #entries * BUTTON_H
			+ math.max(0, #entries - 1) * BUTTON_SPACING

	local fs = {
		("formspec_version[6]size[%f,%f]"):format(width, height),
		"bgcolor[;neither]",
		("box[0,0;%f,%f;%s]"):format(width, height,
			sidebar.background or "#111111"),
	}

	local y = PADDING
	for _, entry in ipairs(entries) do
		fs[#fs + 1] = ("style[%s;bgcolor=%s;border=false]"):format(
			entry.name, sidebar.button_background or "#222222")
		fs[#fs + 1] = ("button[%f,%f;%f,%f;%s;%s]"):format(
			PADDING, y, BUTTON_W, BUTTON_H, entry.name, entry.label)

		y = y + BUTTON_H + BUTTON_SPACING
	end

	return table.concat(fs)
end

local function button_handler(this, fields)
	local tabview = this.data.tabview

	for _, entry in ipairs(get_entries(tabview)) do
		if fields[entry.name] then
			if entry.action then
				-- Actions take the view they were triggered from, so dialogs
				-- opened from here return to the landing page.
				return entry.action.on_click(this)
			end

			this:hide()
			tabview:set_tab(tabview.tablist[entry.tab_index].name)
			tabview:show()
			return true
		end
	end

	return false
end

local function event_handler(event)
	if event == "DialogShow" then
		mm_game_theme.set_engine()
		return true
	end

	if event == "MenuQuit" then
		if not core.settings:get_bool("enable_esc_dialog") then
			core.close()
			return true
		end

		if not ui.childlist["dlg_exit"] then
			start_page:hide()
			local dlg = create_exit_dialog()
			dlg:set_parent(start_page)
			dlg:show()
			-- fstk only refreshes when an event handler reports back, and a
			-- handled MenuQuit never reaches it from a dialog
			ui.update()
		end
		return true
	end

	return false
end

function create_start_page(tabview)
	start_page = dialog_create("mainmenu_start", get_formspec,
		button_handler, event_handler)
	start_page.data.tabview = tabview
	return start_page
end
