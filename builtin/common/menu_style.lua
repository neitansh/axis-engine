-- the Axis
-- SPDX-License-Identifier: LGPL-2.1-or-later

-- One look for every menu: rounded 9-sliced panels and buttons, a green accent
-- for whatever is currently selected. Menus prepend `menu_style.prelude()` to
-- their formspec and use the helpers below for panels and highlighted buttons.

menu_style = {}

menu_style.ACCENT = "#3C8527"
menu_style.TEXT = "#FFFFFF"
menu_style.TEXT_MUTED = "#9AA0A6"
menu_style.HEADING = "#E8C15A"

-- Corner size of the 9-sliced textures, in pixels
local BUTTON_MIDDLE = 16
local PANEL_MIDDLE = 10
local INSET_MIDDLE = 6

local function tex(name)
	return core.formspec_escape(defaulttexturedir .. name)
end

--- Formspec prefix that gives buttons and text entry their shared look.
function menu_style.prelude()
	return table.concat({
		("style_type[button,image_button;bgimg=%s;bgimg_hovered=%s;bgimg_pressed=%s;" ..
			"bgimg_middle=%d;border=false;textcolor=%s]"):format(
			tex("axis_button.png"), tex("axis_button_hover.png"),
			tex("axis_button_pressed.png"), BUTTON_MIDDLE, menu_style.TEXT),
		("style_type[field,pwdfield,textarea;border=false;textcolor=%s]"):format(menu_style.TEXT),
	})
end

--- Green styling for the given element names, e.g. the selected page.
function menu_style.accent(names)
	return ("style[%s;bgimg=%s;bgimg_hovered=%s;bgimg_pressed=%s;bgimg_middle=%d;" ..
			"border=false;textcolor=%s]"):format(
		names, tex("axis_button_accent.png"), tex("axis_button_accent_hover.png"),
		tex("axis_button_accent_pressed.png"), BUTTON_MIDDLE, menu_style.TEXT)
end

--- Rounded translucent panel, used as the backdrop of a menu.
function menu_style.panel(x, y, w, h)
	return ("background9[%f,%f;%f,%f;%s;false;%d]"):format(
		x, y, w, h, tex("axis_panel.png"), PANEL_MIDDLE)
end

--- Rounded sunken area, used behind lists and input rows.
function menu_style.inset(x, y, w, h)
	return ("background9[%f,%f;%f,%f;%s;false;%d]"):format(
		x, y, w, h, tex("axis_inset.png"), INSET_MIDDLE)
end

return menu_style
