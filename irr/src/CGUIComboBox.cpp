// Copyright (C) 2002-2012 Nikolaus Gebhardt
// This file is part of the "Irrlicht Engine".
// For conditions of distribution and use, see copyright notice in irrlicht.h

#include "CGUIComboBox.h"

#include "IGUIEnvironment.h"
#include "IVideoDriver.h"
#include "IGUISkin.h"
#include "IGUIEnvironment.h"
#include "IGUIFont.h"
#include "IGUIButton.h"
#include "CGUIListBox.h"
#include "os.h"

namespace gui
{

//! constructor
CGUIComboBox::CGUIComboBox(IGUIEnvironment *environment, IGUIElement *parent,
		s32 id, core::rect<s32> rectangle) :
		IGUIComboBox(environment, parent, id, rectangle),
		ListButton(nullptr), SelectedText(nullptr), ListBox(nullptr), LastFocus(nullptr),
		Selected(-1), HAlign(EGUIA_UPPERLEFT), VAlign(EGUIA_CENTER), MaxSelectionRows(5), HasFocus(false),
		ActiveFont(nullptr)
{
	IGUISkin *skin = Environment->getSkin();

	ListButton = Environment->addButton(core::recti(0, 0, 1, 1), this, -1, L"");
	if (skin && skin->getSpriteBank()) {
		ListButton->setSpriteBank(skin->getSpriteBank());
		ListButton->setSprite(EGBS_BUTTON_UP, skin->getIcon(EGDI_CURSOR_DOWN), skin->getColor(EGDC_WINDOW_SYMBOL));
		ListButton->setSprite(EGBS_BUTTON_DOWN, skin->getIcon(EGDI_CURSOR_DOWN), skin->getColor(EGDC_WINDOW_SYMBOL));
	}
	ListButton->setAlignment(EGUIA_LOWERRIGHT, EGUIA_LOWERRIGHT, EGUIA_UPPERLEFT, EGUIA_LOWERRIGHT);
	ListButton->setSubElement(true);
	ListButton->setTabStop(false);

	SelectedText = Environment->addStaticText(L"", core::recti(0, 0, 1, 1), false, false, this, -1, false);
	SelectedText->setSubElement(true);
	SelectedText->setAlignment(EGUIA_UPPERLEFT, EGUIA_LOWERRIGHT, EGUIA_UPPERLEFT, EGUIA_LOWERRIGHT);
	SelectedText->setTextAlignment(EGUIA_UPPERLEFT, EGUIA_CENTER);
	if (skin)
		SelectedText->setOverrideColor(skin->getColor(EGDC_BUTTON_TEXT));
	SelectedText->enableOverrideColor(true);

	updateListButtonWidth(skin ? skin->getSize(EGDS_SCROLLBAR_SIZE) : 15);

	// this element can be tabbed to
	setTabStop(true);
	setTabOrder(-1);
}

void CGUIComboBox::setTextAlignment(EGUI_ALIGNMENT horizontal, EGUI_ALIGNMENT vertical)
{
	HAlign = horizontal;
	VAlign = vertical;
	SelectedText->setTextAlignment(horizontal, vertical);
}

//! Set the maximal number of rows for the selection listbox
void CGUIComboBox::setMaxSelectionRows(u32 max)
{
	MaxSelectionRows = max;

	// force recalculation of open listbox
	if (ListBox) {
		openCloseMenu();
		openCloseMenu();
	}
}

//! Get the maximal number of rows for the selection listbox
u32 CGUIComboBox::getMaxSelectionRows() const
{
	return MaxSelectionRows;
}

//! Returns amount of items in box
u32 CGUIComboBox::getItemCount() const
{
	return Items.size();
}

//! returns string of an item. the idx may be a value from 0 to itemCount-1
const wchar_t *CGUIComboBox::getItem(u32 idx) const
{
	if (idx >= Items.size())
		return 0;

	return Items[idx].Name.c_str();
}

//! returns string of an item. the idx may be a value from 0 to itemCount-1
u32 CGUIComboBox::getItemData(u32 idx) const
{
	if (idx >= Items.size())
		return 0;

	return Items[idx].Data;
}

//! Returns index based on item data
s32 CGUIComboBox::getIndexForItemData(u32 data) const
{
	for (u32 i = 0; i < Items.size(); ++i) {
		if (Items[i].Data == data)
			return i;
	}
	return -1;
}

//! Removes an item from the combo box.
void CGUIComboBox::removeItem(u32 idx)
{
	if (idx >= Items.size())
		return;

	if (Selected == (s32)idx)
		setSelected(-1);

	Items.erase(idx);
}

//! Returns caption of this element.
const wchar_t *CGUIComboBox::getText() const
{
	const wchar_t *item = getItem(Selected);
	return item ? item : L"";
}

//! adds an item and returns the index of it
u32 CGUIComboBox::addItem(const wchar_t *text, u32 data)
{
	Items.push_back(SComboData(text, data));

	if (Selected == -1)
		setSelected(0);

	return Items.size() - 1;
}

//! deletes all items in the combo box
void CGUIComboBox::clear()
{
	Items.clear();
	setSelected(-1);
}

//! returns id of selected item. returns -1 if no item is selected.
s32 CGUIComboBox::getSelected() const
{
	return Selected;
}

//! sets the selected item. Set this to -1 if no item should be selected
void CGUIComboBox::setSelected(s32 idx)
{
	if (idx < -1 || idx >= (s32)Items.size())
		return;

	Selected = idx;
	if (Selected == -1)
		SelectedText->setText(L"");
	else
		SelectedText->setText(Items[Selected].Name.c_str());
}

//! Sets the selected item and emits a change event.
/** Set this to -1 if no item should be selected */
void CGUIComboBox::setAndSendSelected(s32 idx)
{
	setSelected(idx);
	sendSelectionChangedEvent();
}

//! called if an event happened.
bool CGUIComboBox::OnEvent(const SEvent &event)
{
	if (isEnabled()) {
		switch (event.EventType) {

		case EET_KEY_INPUT_EVENT:
			if (ListBox && event.KeyInput.PressedDown && event.KeyInput.Key == KEY_ESCAPE) {
				// hide list box
				openCloseMenu();
				return true;
			} else if (event.KeyInput.Key == KEY_RETURN || event.KeyInput.Key == KEY_SPACE) {
				if (!event.KeyInput.PressedDown) {
					openCloseMenu();
				}

				ListButton->setPressed(ListBox == nullptr);

				return true;
			} else if (event.KeyInput.PressedDown) {
				s32 oldSelected = Selected;
				bool absorb = true;
				switch (event.KeyInput.Key) {
				case KEY_DOWN:
					setSelected(Selected + 1);
					break;
				case KEY_UP:
					setSelected(Selected - 1);
					break;
				case KEY_HOME:
				case KEY_PRIOR:
					setSelected(0);
					break;
				case KEY_END:
				case KEY_NEXT:
					setSelected((s32)Items.size() - 1);
					break;
				default:
					absorb = false;
				}

				if (Selected < 0)
					setSelected(0);

				if (Selected >= (s32)Items.size())
					setSelected((s32)Items.size() - 1);

				if (Selected != oldSelected) {
					sendSelectionChangedEvent();
					return true;
				}

				if (absorb)
					return true;
			}
			break;

		case EET_GUI_EVENT:

			switch (event.GUIEvent.EventType) {
			case EGET_ELEMENT_FOCUS_LOST:
				if (ListBox &&
						// Newly focused element
						event.GUIEvent.Element != this &&
						!isMyDescendant(event.GUIEvent.Element)) {
					openCloseMenu(); // close
				}
				break;
			case EGET_BUTTON_CLICKED:
				if (event.GUIEvent.Caller == ListButton) {
					openCloseMenu();
					return true;
				}
				break;
			case EGET_LISTBOX_SELECTED_AGAIN:
			case EGET_LISTBOX_CHANGED:
				if (event.GUIEvent.Caller == ListBox) {
					setSelected(ListBox->getSelected());
					if (Selected < 0 || Selected >= (s32)Items.size())
						setSelected(-1);
					openCloseMenu();

					sendSelectionChangedEvent();
				}
				return true;
			default:
				break;
			}
			break;
		case EET_MOUSE_INPUT_EVENT: {
			core::position2d<s32> p(event.MouseInput.X, event.MouseInput.Y);

			switch (event.MouseInput.Event) {
			case EMIE_LMOUSE_PRESSED_DOWN: {

				if (!ListBox && isPointInside(p)) {
					// Quick select: mouse down -> drag to position -> mouse up -> (selected)
					openCloseMenu(); // open
					return true;
				}

				break;
			}
			case EMIE_LMOUSE_LEFT_UP: {
				// Eaten by CGUIListBox
				break;
			}
			case EMIE_MOUSE_WHEEL: {
				// Try scrolling parent first
				if (IGUIElement::OnEvent(event))
					return true;

				s32 oldSelected = Selected;
				setSelected(Selected + ((event.MouseInput.Wheel < 0) ? 1 : -1));

				if (Selected < 0)
					setSelected(0);

				if (Selected >= (s32)Items.size())
					setSelected((s32)Items.size() - 1);

				if (Selected != oldSelected) {
					sendSelectionChangedEvent();
					return true;
				}

				return false;
			}
			default:
				break;
			}

			// Prevent interaction with underlying elements.
			if (isPointInside(p) || (ListBox && ListBox->isPointInside(p)))
				return true;

			break;
		}
		default:
			break;
		}
	}

	return IGUIElement::OnEvent(event);
}

void CGUIComboBox::sendSelectionChangedEvent()
{
	if (Parent) {
		SEvent event;

		event.EventType = EET_GUI_EVENT;
		event.GUIEvent.Caller = this;
		event.GUIEvent.Element = nullptr;
		event.GUIEvent.EventType = EGET_COMBO_BOX_CHANGED;
		Parent->OnEvent(event);
	}
}

void CGUIComboBox::updateListButtonWidth(s32 width)
{
	if (ListButton->getRelativePosition().getWidth() != width) {
		// Кнопка стоит вплотную к правому краю и во всю высоту — ровно там же
		// и такой же ширины раскрытый список ставит свою полосу прокрутки.
		// Прежние два пикселя отступа и делали ту кривизну, из-за которой
		// рамка стрелки не совпадала с рамкой полосы.
		core::rect<s32> r;
		r.UpperLeftCorner.X = RelativeRect.getWidth() - width;
		r.LowerRightCorner.X = RelativeRect.getWidth();
		r.UpperLeftCorner.Y = 0;
		r.LowerRightCorner.Y = RelativeRect.getHeight();
		ListButton->setRelativePosition(r);

		r.UpperLeftCorner.X = 2;
		r.UpperLeftCorner.Y = 2;
		r.LowerRightCorner.X = RelativeRect.getWidth() - (width + 2);
		r.LowerRightCorner.Y = RelativeRect.getHeight() - 2;
		SelectedText->setRelativePosition(r);
	}
}

//! draws the element and its children
void CGUIComboBox::draw()
{
	if (!IsVisible)
		return;

	IGUISkin *skin = Environment->getSkin();

	updateListButtonWidth(skin->getSize(EGDS_SCROLLBAR_SIZE));

	// font changed while the listbox is open?
	if (ActiveFont != skin->getFont() && ListBox) {
		// close and re-open to use new font-size
		openCloseMenu();
		openCloseMenu();
	}

	IGUIElement *currentFocus = Environment->getFocus();
	if (currentFocus != LastFocus) {
		HasFocus = currentFocus == this || isMyDescendant(currentFocus);
		LastFocus = currentFocus;
	}

	// set colors each time as skin-colors can be changed
	SelectedText->setBackgroundColor(skin->getColor(EGDC_HIGH_LIGHT));
	if (isEnabled()) {
		SelectedText->setDrawBackground(HasFocus);
		SelectedText->setOverrideColor(skin->getColor(HasFocus ? EGDC_HIGH_LIGHT_TEXT : EGDC_BUTTON_TEXT));
	} else {
		SelectedText->setDrawBackground(false);
		SelectedText->setOverrideColor(skin->getColor(EGDC_GRAY_TEXT));
	}
	// Стрелку рисуем сами, поэтому кнопке нечего показывать.
	ListButton->setDrawBorder(false);
	ListButton->setSprite(EGBS_BUTTON_UP, -1, video::SColor(0, 0, 0, 0));
	ListButton->setSprite(EGBS_BUTTON_DOWN, -1, video::SColor(0, 0, 0, 0));

	video::IVideoDriver *driver = Environment->getVideoDriver();
	const core::rect<s32> &clip = AbsoluteClippingRect;
	core::rect<s32> frameRect(AbsoluteRect);

	// Поле: лёгкая заливка вместо тёмной вдавленной плашки. Тёмный
	// прямоугольник посреди светлого списка кричит громче самого поля.
	driver->draw2DRectangle(video::SColor(20, 255, 255, 255), frameRect, &clip);
	driver->draw2DRectangle(video::SColor(34, 255, 255, 255),
			core::rect<s32>(frameRect.UpperLeftCorner.X,
					frameRect.LowerRightCorner.Y - 2,
					frameRect.LowerRightCorner.X, frameRect.LowerRightCorner.Y),
			&clip);

	drawArrow();

	// draw children
	IGUIElement::draw();
}

//! Стрелка справа от поля.
//!
//! В рамке, как у полосы прокрутки, и той же ширины — раскрытый список
//! приставляет свою полосу ровно под ней, и разъезжаться им незачем. Сама
//! стрелка складывается из полосок: так она перетекает из «вниз» в «вверх»,
//! показывая, раскрыт список или закрыт.
void CGUIComboBox::drawArrow()
{
	video::IVideoDriver *driver = Environment->getVideoDriver();
	const core::rect<s32> &clip = AbsoluteClippingRect;
	const video::SColor line(255, 255, 255, 255);

	const u32 now = os::Timer::getTime();
	const f32 dt = ArrowTime == 0 ? 0.0f : core::min_((now - ArrowTime) / 1000.0f, 0.1f);
	ArrowTime = now;
	const f32 target = ListBox ? 1.0f : 0.0f;
	const f32 step = dt * 8.0f;
	if (ArrowOpen < target)
		ArrowOpen = core::min_(target, ArrowOpen + step);
	else if (ArrowOpen > target)
		ArrowOpen = core::max_(target, ArrowOpen - step);

	core::rect<s32> btn = ListButton->getAbsolutePosition();
	// Рамка в два пикселя, внутри пусто — как у полос прокрутки.
	const s32 w = 2;
	auto bar = [&](s32 x0, s32 y0, s32 x1, s32 y1) {
		driver->draw2DRectangle(line, core::rect<s32>(x0, y0, x1, y1), &clip);
	};
	bar(btn.UpperLeftCorner.X, btn.UpperLeftCorner.Y, btn.LowerRightCorner.X,
			btn.UpperLeftCorner.Y + w);
	bar(btn.UpperLeftCorner.X, btn.LowerRightCorner.Y - w, btn.LowerRightCorner.X,
			btn.LowerRightCorner.Y);
	bar(btn.UpperLeftCorner.X, btn.UpperLeftCorner.Y, btn.UpperLeftCorner.X + w,
			btn.LowerRightCorner.Y);
	bar(btn.LowerRightCorner.X - w, btn.UpperLeftCorner.Y, btn.LowerRightCorner.X,
			btn.LowerRightCorner.Y);

	// Треугольник из четырёх полосок: закрытый список — остриём вниз,
	// раскрытый — вверх, между ними ровная черта.
	const s32 rows = 4;
	// Треугольнику нужна ширина, а не высота: в узкой кнопке высокий и узкий
	// он читается не стрелкой, а восклицательным знаком.
	const s32 full = core::max_(4, btn.getWidth() - 2 * (w + 3));
	const s32 row_h = core::max_(2, btn.getHeight() / 12);
	const s32 top = btn.UpperLeftCorner.Y +
			(btn.getHeight() - rows * row_h) / 2;
	const s32 centre = btn.UpperLeftCorner.X + btn.getWidth() / 2;

	for (s32 i = 0; i < rows; ++i) {
		const f32 down = (f32)(rows - i) / rows;
		const f32 up = (f32)(i + 1) / rows;
		const s32 width = (s32)(full * (down + (up - down) * ArrowOpen));
		if (width < 1)
			continue;
		bar(centre - width / 2, top + i * row_h,
				centre - width / 2 + width, top + i * row_h + row_h);
	}
}

void CGUIComboBox::openCloseMenu()
{
	if (ListBox) {
		// close list box
		Environment->setFocus(this);
		ListBox->remove();
		ListBox = nullptr;
	} else {
		if (Parent) {
			SEvent event;
			event.EventType = EET_GUI_EVENT;
			event.GUIEvent.Caller = this;
			event.GUIEvent.Element = nullptr;
			event.GUIEvent.EventType = EGET_LISTBOX_OPENED;

			// Allow to prevent the listbox from opening.
			if (Parent->OnEvent(event))
				return;

			Parent->bringToFront(this);
		}

		IGUISkin *skin = Environment->getSkin();
		u32 h = Items.size();

		if (h > getMaxSelectionRows())
			h = getMaxSelectionRows();
		if (h == 0)
			h = 1;

		ActiveFont = skin->getFont();
		if (ActiveFont)
			h *= (ActiveFont->getDimension(L"A").Height + 4);

		// open list box
		core::rect<s32> r(0, AbsoluteRect.getHeight(),
				AbsoluteRect.getWidth(), AbsoluteRect.getHeight() + h);

		ListBox = new CGUIListBox(Environment, this, -1, r, false, true, true);
		ListBox->setSubElement(true);
		ListBox->setNotClipped(true);
		ListBox->drop();

		// ensure that list box is always completely visible
		if (ListBox->getAbsolutePosition().LowerRightCorner.Y > Environment->getRootGUIElement()->getAbsolutePosition().getHeight())
			ListBox->setRelativePosition(core::rect<s32>(0, -ListBox->getAbsolutePosition().getHeight(), AbsoluteRect.getWidth(), 0));

		for (s32 i = 0; i < (s32)Items.size(); ++i)
			ListBox->addItem(Items[i].Name.c_str());

		ListBox->setSelected(Selected);

		// set focus
		Environment->setFocus(ListBox);
	}
}

} // end namespace gui
