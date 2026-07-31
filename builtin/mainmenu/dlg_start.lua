-- the Axis
-- SPDX-License-Identifier: LGPL-2.1-or-later

-- Landing page shown on startup: the sidebar of the main tabview, centred on
-- screen below the engine logo. Picking an entry opens the tabview itself.

local PAD = 0.7
local WIDTH = 6.4
local GAP = 0.2
local H_PRIMARY = 1.15
local H_SECONDARY = 0.95

local start_page

-- Splits the tabview into what the landing page shows as one leading action,
-- the remaining destinations, and the two utility actions at the bottom.
local function get_entries(tabview)
	local tabs, actions = {}, {}

	for i, tab in ipairs(tabview.tablist) do
		if tab.sidebar then
			local caption = tab.caption
			if type(caption) == "function" then
				caption = caption(tabview)
			end

			tabs[#tabs + 1] = {
				name = "start_tab_" .. i,
				label = caption,
				tab_index = i,
			}
		end
	end

	for _, action in ipairs(tabview.sidebar and tabview.sidebar.actions or {}) do
		actions[#actions + 1] = {
			name = "start_action_" .. action.name,
			label = action.label,
			action = action,
		}
	end

	return tabs, actions
end


local function all_entries(tabview)
	local tabs, actions = get_entries(tabview)
	for _, action in ipairs(actions) do
		tabs[#tabs + 1] = action
	end
	return tabs
end


local function get_formspec(data)
	local tabview = data.tabview
	local tabs, actions = get_entries(tabview)

	local body_w = WIDTH - PAD * 2
	local action_w = (body_w - GAP) / 2

	local fs = {}
	local y = PAD

	-- Leading action, then the other destinations
	local rows = {}
	for i, entry in ipairs(tabs) do
		local h = i == 1 and H_PRIMARY or H_SECONDARY
		rows[#rows + 1] = { entry = entry, y = y, h = h, primary = i == 1 }
		y = y + h + GAP
	end

	if #actions > 0 then
		y = y + menu_style.SPACE.sm
		rows.divider_y = y
		y = y + menu_style.SPACE.md
		rows.action_y = y
		y = y + H_SECONDARY
	end

	y = y + menu_style.SPACE.lg
	local caption_y = y
	local height = y + 0.5 + PAD - menu_style.SPACE.lg

	fs[#fs + 1] = ("formspec_version[6]size[%f,%f]"):format(WIDTH, height)
	fs[#fs + 1] = "bgcolor[;neither]"
	fs[#fs + 1] = menu_style.panel(0, 0, WIDTH, height)
	fs[#fs + 1] = menu_style.prelude()

	for _, row in ipairs(rows) do
		if row.primary then
			fs[#fs + 1] = menu_style.accent(row.entry.name)
			fs[#fs + 1] = ("style[%s;font=bold;font_size=*1.1]"):format(row.entry.name)
		end
		fs[#fs + 1] = ("button[%f,%f;%f,%f;%s;%s]"):format(
			PAD, row.y, body_w, row.h, row.entry.name, row.entry.label)
	end

	if rows.divider_y then
		fs[#fs + 1] = menu_style.divider(PAD, rows.divider_y, body_w, true)

		for i, entry in ipairs(actions) do
			fs[#fs + 1] = ("button[%f,%f;%f,%f;%s;%s]"):format(
				PAD + (i - 1) * (action_w + GAP), rows.action_y,
				action_w, H_SECONDARY, entry.name, entry.label)
		end
	end

	fs[#fs + 1] = menu_style.caption(PAD, caption_y, body_w, 0.5,
		core.formspec_escape(core.get_version().string), "center")

	return table.concat(fs)
end


local function button_handler(this, fields)
	local tabview = this.data.tabview

	for _, entry in ipairs(all_entries(tabview)) do
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
