-- the Axis
-- SPDX-License-Identifier: LGPL-2.1-or-later

--------------------------------------------------------------------------------
-- Shared visual language for every menu.
--
-- Three surface levels (panel < surface < inset) carry the depth, one warm
-- accent marks the single most important action on a screen, and everything
-- else is neutral. Sizes come from the scales below rather than from numbers
-- typed at the call site, so screens keep their proportions when the GUI is
-- scaled.
--------------------------------------------------------------------------------

menu_style = {}

-- Text
menu_style.TEXT = "#E8EAED"
menu_style.TEXT_MUTED = "#8C949E"
menu_style.TEXT_DIM = "#6E757E"
menu_style.TEXT_ON_ACCENT = "#F2FFFB"

-- Accents
menu_style.ACCENT = "#7FD6C0"
menu_style.HEADING = "#7FD6C0"
menu_style.DANGER = "#C05B4D"

-- Hairlines drawn with box[]
menu_style.LINE = "#FFFFFF14"
menu_style.LINE_STRONG = "#FFFFFF24"

-- Spacing scale, in formspec units
menu_style.SPACE = {
	xs = 0.15,
	sm = 0.25,
	md = 0.4,
	lg = 0.65,
	xl = 1.0,
}

-- Row heights
menu_style.ROW = 0.9
menu_style.ROW_LG = 1.1

-- Corner size of the 9-sliced textures, in source pixels
-- Flat 32px art with a one pixel border; the middle also becomes the button's
-- inner padding, so it stays at the border width.
local BUTTON_MIDDLE = 2
local PANEL_MIDDLE = 2
local SURFACE_MIDDLE = 2
local INSET_MIDDLE = 2

local function tex(name)
	return core.formspec_escape(defaulttexturedir .. name)
end

local function nine(name, middle)
	return function(x, y, w, h)
		return ("background9[%f,%f;%f,%f;%s;false;%d]"):format(
			x, y, w, h, tex(name), middle)
	end
end

--- Deepest surface: the backdrop a screen sits on. Flat and translucent, so
--- the world stays visible behind the menu.
menu_style.panel = nine("axis_panel.png", PANEL_MIDDLE)
--- Raised surface: cards, rows and side rails that sit on the panel.
menu_style.surface = nine("axis_surface.png", SURFACE_MIDDLE)
--- Sunken surface: lists, search bars and anything holding input.
menu_style.inset = nine("axis_inset.png", INSET_MIDDLE)

--- Formspec prefix giving buttons and text entry their shared look.
function menu_style.prelude()
	return table.concat({
		("style_type[button,image_button;bgimg=%s;bgimg_hovered=%s;bgimg_pressed=%s;" ..
			"bgimg_middle=%d;border=false;textcolor=%s]"):format(
			tex("axis_button.png"), tex("axis_button_hover.png"),
			tex("axis_button_pressed.png"), BUTTON_MIDDLE, menu_style.TEXT),
		("style_type[field,pwdfield,textarea;border=false;textcolor=%s]"):format(menu_style.TEXT),
		("style_type[label;textcolor=%s]"):format(menu_style.TEXT),
	})
end

--- The one action a screen wants you to take.
function menu_style.accent(names)
	return ("style[%s;bgimg=%s;bgimg_hovered=%s;bgimg_pressed=%s;bgimg_middle=%d;" ..
			"border=false;textcolor=%s]"):format(
		names, tex("axis_button_accent.png"), tex("axis_button_accent_hover.png"),
		tex("axis_button_accent_pressed.png"), BUTTON_MIDDLE, menu_style.TEXT_ON_ACCENT)
end

--- Flat until hovered: navigation entries, where a row of raised buttons
--- would fight with the content next to it.
function menu_style.ghost(names)
	return ("style[%s;bgimg=;bgimg_hovered=%s;bgimg_pressed=%s;bgimg_middle=%d;" ..
			"border=false;bgcolor=#00000000;textcolor=%s]"):format(
		names, tex("axis_button_hover.png"), tex("axis_button_pressed.png"),
		BUTTON_MIDDLE, menu_style.TEXT_MUTED)
end

--- Marks the current entry in a navigation list. Uses the button's own
--- background because background9[] is not positioned inside scroll containers.
function menu_style.selected(names)
	return ("style[%s;bgimg=%s;bgimg_hovered=%s;bgimg_pressed=%s;bgimg_middle=%d;" ..
			"border=false;textcolor=%s]"):format(
		names, tex("axis_surface.png"), tex("axis_surface.png"),
		tex("axis_surface.png"), BUTTON_MIDDLE, menu_style.ACCENT)
end

--- Icon-only buttons: no 9-slice, which would eat the room the icon needs.
function menu_style.icon(names)
	return ("style[%s;bgimg=;bgimg_hovered=;bgimg_pressed=;border=false;" ..
			"bgcolor=#00000000;bgcolor_hovered=#FFFFFF14;bgcolor_pressed=#00000055]"):format(names)
end

--- Present but not available.
function menu_style.disabled(names)
	return ("style[%s;bgimg=%s;bgimg_hovered=%s;bgimg_pressed=%s;bgimg_middle=%d;" ..
			"border=false;textcolor=%s]"):format(
		names, tex("axis_button_disabled.png"), tex("axis_button_disabled.png"),
		tex("axis_button_disabled.png"), BUTTON_MIDDLE, menu_style.TEXT_DIM)
end

--------------------------------------------------------------------------------
-- Type scale. Every helper draws an "area label", which wraps and can be
-- aligned, and leaves the label style as it found it.
--------------------------------------------------------------------------------

local function area_label(x, y, w, h, text, size, color, weight, align)
	return ("style_type[label;font_size=%s;textcolor=%s;font=%s;halign=%s;valign=center]" ..
			"label[%f,%f;%f,%f;%s]" ..
			"style_type[label;font_size=;textcolor=%s;font=normal;halign=left;valign=top]"):format(
		size, color, weight, align or "left", x, y, w, h, text, menu_style.TEXT)
end

--- Screen title.
function menu_style.title(x, y, w, h, text)
	return area_label(x, y, w, h, text, "*1.55", menu_style.TEXT, "bold")
end

--- Section heading inside a screen.
function menu_style.heading(x, y, w, h, text)
	return area_label(x, y, w, h, text, "*1.1", menu_style.HEADING, "bold")
end

--- Ordinary text that is not a control label.
function menu_style.body(x, y, w, h, text, align)
	return area_label(x, y, w, h, text, "*1.0", menu_style.TEXT, "normal", align)
end

--- Supporting text: explanations, counts, version strings.
function menu_style.caption(x, y, w, h, text, align)
	return area_label(x, y, w, h, text, "*0.9", menu_style.TEXT_MUTED, "normal", align)
end

--- Hairline used to separate groups.
function menu_style.divider(x, y, w, strong)
	return ("box[%f,%f;%f,0.02;%s]"):format(
		x, y, w, strong and menu_style.LINE_STRONG or menu_style.LINE)
end

return menu_style
