/*
Copyright (C) 2002-2013 Nikolaus Gebhardt
This file is part of the "Irrlicht Engine".
For conditions of distribution and use, see copyright notice in irrlicht.h
*/

#include "guiScrollBar.h"
#include "guiButton.h"
#include <IGUIEnvironment.h>
#include <IVideoDriver.h>

GUIScrollBar::GUIScrollBar(IGUIEnvironment *environment, IGUIElement *parent, s32 id,
		core::rect<s32> rectangle, bool horizontal, ISimpleTextureSource *tsrc) :
		CGUIScrollBar(environment, parent, id, rectangle, horizontal)
{
	// We use GUIButton instead of CGUIButton
	if (UpButton) {
		UpButton->remove();
		UpButton->drop();
	}
	UpButton = new GUIButton(Environment, this, -1, {}, tsrc, NoClip);
	UpButton->setSubElement(true);
	UpButton->setTabStop(false);
	if (DownButton) {
		DownButton->remove();
		DownButton->drop();
	}
	DownButton = new GUIButton(Environment, this, -1, {}, tsrc, NoClip);
	DownButton->setSubElement(true);
	DownButton->setTabStop(false);

	refreshControls();
}

//! Как выглядит полоса.
//!
//! Рамка в один пиксель и бегунок внутри с отступом — ровно столько, сколько
//! нужно, чтобы понять, где ты находишься. Объёмные кнопки скина рядом с
//! плоским меню смотрелись деталью из другой игры.
namespace
{
const video::SColor SCROLLBAR_FRAME(120, 255, 255, 255);
const video::SColor SCROLLBAR_THUMB(220, 255, 255, 255);
const video::SColor SCROLLBAR_THUMB_HELD(255, 255, 255, 255);
//! Отступ бегунка от рамки, в пикселях.
const s32 SCROLLBAR_PAD = 3;
}

void GUIScrollBar::draw()
{
	if (!IsVisible)
		return;

	video::IVideoDriver *driver = Environment->getVideoDriver();
	const core::rect<s32> &clip = AbsoluteClippingRect;
	core::rect<s32> frame = AbsoluteRect;

	// Рамка: четыре тонкие полоски, чтобы середина осталась прозрачной и под
	// полосой было видно окно.
	auto line = [&](s32 x0, s32 y0, s32 x1, s32 y1) {
		driver->draw2DRectangle(SCROLLBAR_FRAME,
				core::rect<s32>(x0, y0, x1, y1), &clip);
	};
	line(frame.UpperLeftCorner.X, frame.UpperLeftCorner.Y,
			frame.LowerRightCorner.X, frame.UpperLeftCorner.Y + 1);
	line(frame.UpperLeftCorner.X, frame.LowerRightCorner.Y - 1,
			frame.LowerRightCorner.X, frame.LowerRightCorner.Y);
	line(frame.UpperLeftCorner.X, frame.UpperLeftCorner.Y,
			frame.UpperLeftCorner.X + 1, frame.LowerRightCorner.Y);
	line(frame.LowerRightCorner.X - 1, frame.UpperLeftCorner.Y,
			frame.LowerRightCorner.X, frame.LowerRightCorner.Y);

	SliderRect = AbsoluteRect;
	if (core::isnotzero(range())) {
		if (Horizontal) {
			// У горизонтальной полосы бегунок квадратный: она задаёт одно
			// число, и растягивать его в полоску нечего.
			const s32 side = frame.getHeight() - 2 * SCROLLBAR_PAD;
			const s32 centre = frame.UpperLeftCorner.X + DrawPos;
			SliderRect.UpperLeftCorner.X = centre - side / 2;
			SliderRect.LowerRightCorner.X = SliderRect.UpperLeftCorner.X + side;
			SliderRect.UpperLeftCorner.Y = frame.UpperLeftCorner.Y + SCROLLBAR_PAD;
			SliderRect.LowerRightCorner.Y = SliderRect.UpperLeftCorner.Y + side;
		} else {
			// У вертикальной длина бегунка показывает, какая часть списка
			// видна, — её и оставляем.
			SliderRect.UpperLeftCorner.Y =
					frame.UpperLeftCorner.Y + DrawPos - DrawHeight / 2;
			SliderRect.LowerRightCorner.Y =
					SliderRect.UpperLeftCorner.Y + DrawHeight;
			SliderRect.UpperLeftCorner.X = frame.UpperLeftCorner.X + SCROLLBAR_PAD;
			SliderRect.LowerRightCorner.X = frame.LowerRightCorner.X - SCROLLBAR_PAD;
		}

		// Бегунок не должен вылезать за рамку даже на краях хода.
		SliderRect.constrainTo(core::rect<s32>(
				frame.UpperLeftCorner.X + SCROLLBAR_PAD,
				frame.UpperLeftCorner.Y + SCROLLBAR_PAD,
				frame.LowerRightCorner.X - SCROLLBAR_PAD,
				frame.LowerRightCorner.Y - SCROLLBAR_PAD));

		driver->draw2DRectangle(
				Dragging ? SCROLLBAR_THUMB_HELD : SCROLLBAR_THUMB,
				SliderRect, &clip);
	}

	// Стрелки, если их всё-таки попросили показать.
	IGUIElement::draw();
}
