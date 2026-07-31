/*
Copyright (C) 2002-2013 Nikolaus Gebhardt
This file is part of the "Irrlicht Engine".
For conditions of distribution and use, see copyright notice in irrlicht.h
*/

#pragma once

#include <CGUIScrollBar.h>

class ISimpleTextureSource;

using namespace gui;

class GUIScrollBar final : public CGUIScrollBar
{
public:
	GUIScrollBar(IGUIEnvironment *environment, IGUIElement *parent, s32 id,
			core::rect<s32> rectangle, bool horizontal, ISimpleTextureSource *tsrc);

	virtual ~GUIScrollBar() {}

	//! State of an in-progress mouse drag.
	struct DragState
	{
		bool dragging = false;
		bool by_slider = false;
		s32 offset = 0;
	};

	DragState getDragState() const { return {Dragging, DraggedBySlider, DragOffset}; }

	void setDragState(const DragState &state)
	{
		Dragging = state.dragging;
		DraggedBySlider = state.by_slider;
		DragOffset = state.offset;
	}
};
