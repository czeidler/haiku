/*
Open Tracker License

Terms and Conditions

Copyright (c) 1991-2000, Be Incorporated. All rights reserved.

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
of the Software, and to permit persons to whom the Software is furnished to do
so, subject to the following conditions:

The above copyright notice and this permission notice applies to all licensees
and shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF TITLE, MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
BE INCORPORATED BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN
AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF, OR IN CONNECTION
WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

Except as contained in this notice, the name of Be Incorporated shall not be
used in advertising or otherwise to promote the sale, use or other dealings in
this Software without prior written authorization from Be Incorporated.

Tracker(TM), Be(R), BeOS(R), and BeIA(TM) are trademarks or registered
trademarks of Be Incorporated in the United States and other countries. Other
brand product names are registered trademarks or trademarks of their respective
holders.
All rights reserved.
*/


#include "ExpandoMenuBar.h"

#include <string.h>

#include <Autolock.h>
#include <Bitmap.h>
#include <ControlLook.h>
#include <Debug.h>
#include <NodeInfo.h>
#include <Roster.h>
#include <Screen.h>
#include <Window.h>

#include "icons.h"

#include "BarApp.h"
#include "BarMenuTitle.h"
#include "BarView.h"
#include "BarWindow.h"
#include "DeskbarMenu.h"
#include "DeskbarUtils.h"
#include "InlineScrollView.h"
#include "ResourceSet.h"
#include "ShowHideMenuItem.h"
#include "StatusView.h"
#include "TeamMenuItem.h"
#include "WindowMenu.h"
#include "WindowMenuItem.h"


const float kMinMenuItemWidth = 50.0f;
const float kSepItemWidth = 5.0f;
const float kIconPadding = 8.0f;

const uint32 kMinimizeTeam = 'mntm';
const uint32 kBringTeamToFront = 'bftm';

bool TExpandoMenuBar::sDoMonitor = false;
thread_id TExpandoMenuBar::sMonThread = B_ERROR;
BLocker TExpandoMenuBar::sMonLocker("expando monitor");


TExpandoMenuBar::TExpandoMenuBar(BRect frame, const char* name,
	TBarView* barView, bool vertical)
	:
	BMenuBar(frame, name, B_FOLLOW_NONE,
		vertical ? B_ITEMS_IN_COLUMN : B_ITEMS_IN_ROW),
	fBarView(barView),
	fVertical(vertical),
	fOverflow(false),
	fDrawLabel(!static_cast<TBarApp*>(be_app)->Settings()->hideLabels),
	fShowTeamExpander(static_cast<TBarApp*>(be_app)->Settings()->superExpando),
	fExpandNewTeams(static_cast<TBarApp*>(be_app)->Settings()->expandNewTeams),
	fDeskbarMenuWidth(kMinMenuItemWidth),
	fPreviousDragTargetItem(NULL),
	fLastClickedItem(NULL),
	fClickedExpander(false)
{
	SetItemMargins(0.0f, 0.0f, 0.0f, 0.0f);
	SetFont(be_plain_font);
	SetMaxItemWidth();

	// top or bottom mode, add deskbar menu and sep for menubar tracking
	// consistency
	const BBitmap* logoBitmap = AppResSet()->FindBitmap(B_MESSAGE_TYPE,
		R_LeafLogoBitmap);
	if (logoBitmap != NULL)
		fDeskbarMenuWidth = logoBitmap->Bounds().Width() + 16;
}


int
TExpandoMenuBar::CompareByName(const void* first, const void* second)
{
	return strcasecmp((*(static_cast<BarTeamInfo* const*>(first)))->name,
		(*(static_cast<BarTeamInfo* const*>(second)))->name);
}


void
TExpandoMenuBar::AttachedToWindow()
{
	BMenuBar::AttachedToWindow();

	fTeamList.MakeEmpty();

	if (fVertical) {
		sDoMonitor = true;
		sMonThread = spawn_thread(monitor_team_windows,
			"Expando Window Watcher", B_LOW_PRIORITY, this);
		resume_thread(sMonThread);
	}
}


void
TExpandoMenuBar::DetachedFromWindow()
{
	BMenuBar::DetachedFromWindow();

	if (sMonThread != B_ERROR) {
		sDoMonitor = false;

		status_t returnCode;
		wait_for_thread(sMonThread, &returnCode);

		sMonThread = B_ERROR;
	}

	BMessenger self(this);
	BMessage message(kUnsubscribe);
	message.AddMessenger("messenger", self);
	be_app->PostMessage(&message);

	RemoveItems(0, CountItems(), true);
}


void
TExpandoMenuBar::MessageReceived(BMessage* message)
{
	int32 index;
	TTeamMenuItem* item;

	switch (message->what) {
		case B_SOME_APP_LAUNCHED:
		{
			BList* teams = NULL;
			message->FindPointer("teams", (void**)&teams);

			BBitmap* icon = NULL;
			message->FindPointer("icon", (void**)&icon);

			const char* signature = NULL;
			message->FindString("sig", &signature);

			uint32 flags = 0;
			message->FindInt32("flags", ((int32*) &flags));

			const char* name = NULL;
			message->FindString("name", &name);

			AddTeam(teams, icon, strdup(name), strdup(signature));
			break;
		}

		case B_MOUSE_WHEEL_CHANGED:
		{
			float deltaY = 0;
			message->FindFloat("be:wheel_delta_y", &deltaY);
			if (deltaY == 0)
				return;

			TInlineScrollView* scrollView
				= dynamic_cast<TInlineScrollView*>(Parent());
			if (scrollView == NULL)
				return;

			float largeStep;
			float smallStep;
			scrollView->GetSteps(&smallStep, &largeStep);

			// pressing the option/command/control key scrolls faster
			if (modifiers() & (B_OPTION_KEY | B_COMMAND_KEY | B_CONTROL_KEY))
				deltaY *= largeStep;
			else
				deltaY *= smallStep;

			scrollView->ScrollBy(deltaY);
			break;
		}

		case kAddTeam:
			AddTeam(message->FindInt32("team"), message->FindString("sig"));
			break;

		case kRemoveTeam:
		{
			team_id team = -1;
			message->FindInt32("team", &team);

			RemoveTeam(team, true);
			break;
		}

		case B_SOME_APP_QUIT:
		{
			team_id team = -1;
			message->FindInt32("team", &team);

			RemoveTeam(team, false);
			break;
		}

		case kMinimizeTeam:
		{
			index = message->FindInt32("itemIndex");
			item = dynamic_cast<TTeamMenuItem*>(ItemAt(index));
			if (item == NULL)
				break;

			TShowHideMenuItem::TeamShowHideCommon(B_MINIMIZE_WINDOW,
				item->Teams(),
				item->Menu()->ConvertToScreen(item->Frame()),
				true);
			break;
		}

		case kBringTeamToFront:
		{
			index = message->FindInt32("itemIndex");
			item = dynamic_cast<TTeamMenuItem*>(ItemAt(index));
			if (item == NULL)
				break;

			TShowHideMenuItem::TeamShowHideCommon(B_BRING_TO_FRONT,
				item->Teams(), item->Menu()->ConvertToScreen(item->Frame()),
				true);
			break;
		}

		default:
			BMenuBar::MessageReceived(message);
			break;
	}
}


void
TExpandoMenuBar::MouseDown(BPoint where)
{
	fClickedExpander = false;
		// in case MouseUp() wasn't called

	BMessage* message = Window()->CurrentMessage();
	BMenuItem* menuItem;
	TTeamMenuItem* item = TeamItemAtPoint(where, &menuItem);

	if (message == NULL || item == NULL || fBarView->Dragging()) {
		BMenuBar::MouseDown(where);
		return;
	}

	int32 modifiers = 0;
	message->FindInt32("modifiers", &modifiers);

	// check for three finger salute, a.k.a. Vulcan Death Grip
	if ((modifiers & B_COMMAND_KEY) != 0
		&& (modifiers & B_CONTROL_KEY) != 0
		&& (modifiers & B_SHIFT_KEY) != 0) {
		const BList* teams = item->Teams();
		int32 teamCount = teams->CountItems();
		team_id teamID;
		for (int32 team = 0; team < teamCount; team++) {
			teamID = (addr_t)teams->ItemAt(team);
			kill_team(teamID);
			RemoveTeam(teamID, false);
				// remove the team from display immediately
		}
		return;
			// absorb the message
	}

	// control click - show all/hide all shortcut
	if ((modifiers & B_CONTROL_KEY) != 0) {
		// show/hide item's teams
		BMessage showMessage((modifiers & B_SHIFT_KEY) != 0
			? kMinimizeTeam : kBringTeamToFront);
		showMessage.AddInt32("itemIndex", IndexOf(item));
		Window()->PostMessage(&showMessage, this);
		return;
			// absorb the message
	}

	int32 buttons = 0;
	// check if within expander bounds to expand window items
	if (fVertical && fShowTeamExpander
		&& item->ExpanderBounds().Contains(where)
		&& message->FindInt32("buttons", &buttons) == B_OK
		&& buttons == B_PRIMARY_MOUSE_BUTTON) {
		// start the animation here, finish on mouse up
		fLastClickedItem = item;
		fClickedExpander = true;
		item->SetArrowDirection(BControlLook::B_RIGHT_DOWN_ARROW);
		Invalidate(item->ExpanderBounds());
		return;
			// absorb the message
	}

	// double-click on an item brings the team to front
	int32 clicks;
	if (message->FindInt32("clicks", &clicks) == B_OK && clicks > 1
		&& item == menuItem && item == fLastClickedItem) {
		be_roster->ActivateApp((addr_t)item->Teams()->ItemAt(0));
			// activate this team
		return;
			// absorb the message
	}

	fLastClickedItem = item;
	BMenuBar::MouseDown(where);
}


void
TExpandoMenuBar::MouseMoved(BPoint where, uint32 code, const BMessage* message)
{
	int32 buttons;
	BMessage* currentMessage = Window()->CurrentMessage();
	if (currentMessage == NULL
		|| currentMessage->FindInt32("buttons", &buttons) != B_OK) {
		buttons = 0;
	}

	if (message == NULL) {
		// force a cleanup
		_FinishedDrag();

		switch (code) {
			case B_ENTERED_VIEW:
			{
				TTeamMenuItem* lastItem
					= dynamic_cast<TTeamMenuItem*>(fLastClickedItem);
				if (fVertical && fShowTeamExpander && fClickedExpander
					&& lastItem != NULL && buttons == B_PRIMARY_MOUSE_BUTTON) {
					// Started expander animation, exited view then entered
					// again, redraw the expanded arrow
					lastItem->SetArrowDirection(BControlLook::B_RIGHT_DOWN_ARROW);
					Invalidate(lastItem->ExpanderBounds());
				}
				break;
			}

			case B_INSIDE_VIEW:
			{
				BMenuItem* menuItem = NULL;
				TTeamMenuItem* item = TeamItemAtPoint(where, &menuItem);
				TWindowMenuItem* windowMenuItem
					= dynamic_cast<TWindowMenuItem*>(menuItem);

				if (item == NULL || menuItem == NULL) {
					// item is NULL, remove the tooltip and break out
					fLastMousedOverItem = NULL;
					SetToolTip((const char*)NULL);
					break;
				}

				if (menuItem == fLastMousedOverItem) {
					// already set the tooltip for this item, break out
					break;
				}

				if (windowMenuItem != NULL && fBarView->Vertical()
					&& fBarView->ExpandoState() && item->IsExpanded()) {
					// expando mode window menu item
					fLastMousedOverItem = menuItem;
					if (strcmp(windowMenuItem->Label(),
							windowMenuItem->FullTitle()) != 0) {
						// label is truncated, set tooltip
						SetToolTip(windowMenuItem->FullTitle());
					} else
						SetToolTip((const char*)NULL);
					break;
				}

				if (item->HasLabel()) {
					// item has a visible label, remove the tooltip and break out
					fLastMousedOverItem = menuItem;
					SetToolTip((const char*)NULL);
					break;
				}

				// new item, set the tooltip to the item name
				SetToolTip(item->Name());

				// save the current menuitem for the next MouseMoved() call
				fLastMousedOverItem = menuItem;

				break;
			}

			case B_OUTSIDE_VIEW:
				// NOTE: Should not be here, but for the sake of defensive
				// programming... fall-through
			case B_EXITED_VIEW:
			{
				TTeamMenuItem* lastItem
					= dynamic_cast<TTeamMenuItem*>(fLastClickedItem);
				if (fVertical && fShowTeamExpander && fClickedExpander
					&& lastItem != NULL) {
					// Started expander animation, then exited view,
					// since we can't track outside mouse movements
					// redraw the original expander arrow
					lastItem->SetArrowDirection(lastItem->IsExpanded()
						? BControlLook::B_DOWN_ARROW
						: BControlLook::B_RIGHT_ARROW);
					Invalidate(lastItem->ExpanderBounds());
				}
				break;
			}
		}

		BMenuBar::MouseMoved(where, code, message);
		return;
	}

	if (buttons == 0)
		return;

	switch (code) {
		case B_ENTERED_VIEW:
			// fPreviousDragTargetItem should always be NULL here anyways.
			if (fPreviousDragTargetItem != NULL)
				_FinishedDrag();

			fBarView->CacheDragData(message);
			fPreviousDragTargetItem = NULL;
			break;

		case B_OUTSIDE_VIEW:
			// NOTE: Should not be here, but for the sake of defensive
			// programming... fall-through
		case B_EXITED_VIEW:
			_FinishedDrag();
			break;

		case B_INSIDE_VIEW:
			if (fBarView->Dragging()) {
				TTeamMenuItem* item = NULL;
				int32 itemCount = CountItems();
				for (int32 i = 0; i < itemCount; i++) {
					BMenuItem* _item = ItemAt(i);
					if (_item->Frame().Contains(where)) {
						item = dynamic_cast<TTeamMenuItem*>(_item);
						break;
					}
				}
				if (item == fPreviousDragTargetItem)
					break;
				if (fPreviousDragTargetItem != NULL)
					fPreviousDragTargetItem->SetOverrideSelected(false);
				if (item != NULL)
					item->SetOverrideSelected(true);
				fPreviousDragTargetItem = item;
			}
			break;
	}
}


void
TExpandoMenuBar::MouseUp(BPoint where)
{
	bool clickedExpander = fClickedExpander;
	fClickedExpander = false;

	if (fBarView->Dragging()) {
		_FinishedDrag(true);
		return;
			// absorb the message
	}

	TTeamMenuItem* item = TeamItemAtPoint(where, NULL);
	TTeamMenuItem* lastItem = dynamic_cast<TTeamMenuItem*>(fLastClickedItem);
	if (fVertical && fShowTeamExpander && clickedExpander) {
		if (item != NULL && lastItem != NULL && item == lastItem
			&& item->ExpanderBounds().Contains(where)) {
			// Toggle the expanded state
			BAutolock locker(sMonLocker);
				// let the update thread wait...
			item->ToggleExpandState(true);
			item->Draw();
			return;
				// absorb the message
		} else if (lastItem != NULL) {
			// User changed their mind, redraw the original expander arrow
			lastItem->SetArrowDirection(lastItem->IsExpanded()
				? BControlLook::B_DOWN_ARROW : BControlLook::B_RIGHT_ARROW);
			Invalidate(lastItem->ExpanderBounds());
		}
	}

	BMenuBar::MouseUp(where);
}


void
TExpandoMenuBar::BuildItems()
{
	BMessenger self(this);
	TBarApp::Subscribe(self, &fTeamList);

	int32 iconSize = static_cast<TBarApp*>(be_app)->IconSize();
	desk_settings* settings = static_cast<TBarApp*>(be_app)->Settings();
	fDrawLabel = !settings->hideLabels;
	fShowTeamExpander = settings->superExpando;
	fExpandNewTeams = settings->expandNewTeams;

	float itemWidth = -0.1f;
	if (fVertical)
		itemWidth = Frame().Width();
	else {
		itemWidth = iconSize;
		if (fDrawLabel)
			itemWidth += sMinimumWindowWidth - kMinimumIconSize;
		else
			itemWidth += kIconPadding * 2;
	}
	float itemHeight = -1.0f;

	RemoveItems(0, CountItems(), true);
		// remove all items

	if (settings->sortRunningApps)
		fTeamList.SortItems(CompareByName);

	int32 count = fTeamList.CountItems();
	for (int32 i = 0; i < count; i++) {
		// add items back
		BarTeamInfo* barInfo = (BarTeamInfo*)fTeamList.ItemAt(i);
		TTeamMenuItem* item = new TTeamMenuItem(barInfo->teams,
			barInfo->icon, barInfo->name, barInfo->sig, itemWidth,
			itemHeight, fDrawLabel, fVertical);

		if (settings->trackerAlwaysFirst
			&& strcmp(barInfo->sig, kTrackerSignature) == 0) {
			AddItem(item, 0);
		} else
			AddItem(item);
	}

	if (CountItems() == 0) {
		// If we're empty, BMenuBar::AttachedToWindow() resizes us to some
		// weird value - we just override it again
		ResizeTo(itemWidth, 0);
	}
}


bool
TExpandoMenuBar::InDeskbarMenu(BPoint loc) const
{
	TBarWindow* window = dynamic_cast<TBarWindow*>(Window());
	if (window) {
		if (TDeskbarMenu* bemenu = window->DeskbarMenu()) {
			bool inDeskbarMenu = false;
			if (bemenu->LockLooper()) {
				inDeskbarMenu = bemenu->Frame().Contains(loc);
				bemenu->UnlockLooper();
			}
			return inDeskbarMenu;
		}
	}

	return false;
}


/*!	Returns the team menu item that belongs to the item under the
	specified \a point.
	If \a _item is given, it will return the exact menu item under
	that point (which might be a window item when the expander is on).
*/
TTeamMenuItem*
TExpandoMenuBar::TeamItemAtPoint(BPoint point, BMenuItem** _item)
{
	TTeamMenuItem* lastApp = NULL;
	int32 count = CountItems();

	for (int32 i = 0; i < count; i++) {
		BMenuItem* item = ItemAt(i);

		if (dynamic_cast<TTeamMenuItem*>(item) != NULL)
			lastApp = (TTeamMenuItem*)item;

		if (item && item->Frame().Contains(point)) {
			if (_item != NULL)
				*_item = item;

			return lastApp;
		}
	}

	// no item found

	if (_item != NULL)
		*_item = NULL;

	return NULL;
}


void
TExpandoMenuBar::AddTeam(BList* team, BBitmap* icon, char* name,
	char* signature)
{
	desk_settings* settings = static_cast<TBarApp*>(be_app)->Settings();
	int32 iconSize = static_cast<TBarApp*>(be_app)->IconSize();

	float itemWidth = -1.0f;
	if (fVertical)
		itemWidth = fBarView->Bounds().Width();
	else {
		itemWidth = iconSize;
		if (fDrawLabel)
			itemWidth += sMinimumWindowWidth - kMinimumIconSize;
		else
			itemWidth += kIconPadding * 2;
	}
	float itemHeight = -1.0f;

	TTeamMenuItem* item = new TTeamMenuItem(team, icon, name, signature,
		itemWidth, itemHeight, fDrawLabel, fVertical);

	if (settings->trackerAlwaysFirst && !strcmp(signature, kTrackerSignature))
		AddItem(item, 0);
	else if (settings->sortRunningApps) {
		TTeamMenuItem* teamItem
			= dynamic_cast<TTeamMenuItem*>(ItemAt(0));
		int32 firstApp = 0;

		// if Tracker should always be the first item, we need to skip it
		// when sorting in the current item
		if (settings->trackerAlwaysFirst && teamItem != NULL
			&& !strcmp(teamItem->Signature(), kTrackerSignature)) {
			firstApp++;
		}

		int32 i = firstApp;
		int32 itemCount = CountItems();
		while (i < itemCount) {
			teamItem = dynamic_cast<TTeamMenuItem*>(ItemAt(i));
			if (teamItem != NULL && strcasecmp(teamItem->Name(), name) > 0) {
				AddItem(item, i);
				break;
			}
			i++;
		}
		// was the item added to the list yet?
		if (i == itemCount)
			AddItem(item);
	} else
		AddItem(item);

	if (fVertical) {
		if (item && fShowTeamExpander && fExpandNewTeams)
			item->ToggleExpandState(false);
	}

	SizeWindow(1);
	Window()->UpdateIfNeeded();
}


void
TExpandoMenuBar::AddTeam(team_id team, const char* signature)
{
	int32 count = CountItems();
	for (int32 i = 0; i < count; i++) {
		// Only add to team menu items
		if (TTeamMenuItem* item = dynamic_cast<TTeamMenuItem*>(ItemAt(i))) {
			if (strcasecmp(item->Signature(), signature) == 0) {
				if (!(item->Teams()->HasItem((void*)(addr_t)team)))
					item->Teams()->AddItem((void*)(addr_t)team);
				break;
			}
		}
	}
}


void
TExpandoMenuBar::RemoveTeam(team_id team, bool partial)
{
	TWindowMenuItem* windowItem = NULL;

	for (int32 i = CountItems() - 1; i >= 0; i--) {
		if (TTeamMenuItem* item = dynamic_cast<TTeamMenuItem*>(ItemAt(i))) {
			if (item->Teams()->HasItem((void*)(addr_t)team)) {
				item->Teams()->RemoveItem(team);

				if (partial)
					return;

#ifdef DOUBLECLICKBRINGSTOFRONT
				if (fLastClickedItem == i)
					fLastClickedItem = -1;
#endif

				BAutolock locker(sMonLocker);
					// make the update thread wait
				RemoveItem(i);
				if (item == fPreviousDragTargetItem)
					fPreviousDragTargetItem = NULL;
				if (item == fLastMousedOverItem)
					fLastMousedOverItem = NULL;
				if (item == fLastClickedItem)
					fLastClickedItem = NULL;
				delete item;
				while ((windowItem = dynamic_cast<TWindowMenuItem*>(
						ItemAt(i))) != NULL) {
					// Also remove window items (if there are any)
					RemoveItem(i);
					if (windowItem == fLastMousedOverItem)
						fLastMousedOverItem = NULL;
					if (windowItem == fLastClickedItem)
						fLastClickedItem = NULL;
					delete windowItem;
				}
				SizeWindow(-1);
				Window()->UpdateIfNeeded();
				return;
			}
		}
	}
}


void
TExpandoMenuBar::CheckItemSizes(int32 delta)
{
	if (fBarView->Vertical())
		return;

	float maxWidth = fBarView->DragRegion()->Frame().left
		- fDeskbarMenuWidth - kSepItemWidth;
	int32 iconSize = static_cast<TBarApp*>(be_app)->IconSize();
	float iconOnlyWidth = kIconPadding + iconSize + kIconPadding;
	float minItemWidth = fDrawLabel
		? iconOnlyWidth + kMinMenuItemWidth
		: iconOnlyWidth - kIconPadding;
	float maxItemWidth = fDrawLabel
		? sMinimumWindowWidth + iconSize - kMinimumIconSize
		: iconOnlyWidth;
	float menuWidth = maxItemWidth * CountItems() + fDeskbarMenuWidth
		+ kSepItemWidth;

	bool reset = false;
	float newWidth = -1.0f;

	if (delta >= 0 && menuWidth > maxWidth) {
		fOverflow = true;
		reset = true;
		newWidth = floorf(maxWidth / CountItems());
	} else if (delta < 0 && fOverflow) {
		reset = true;
		if (menuWidth > maxWidth)
			newWidth = floorf(maxWidth / CountItems());
		else
			newWidth = maxItemWidth;
	}

	if (reset) {
		if (newWidth > maxItemWidth)
			newWidth = maxItemWidth;
		else if (newWidth < minItemWidth)
			newWidth = minItemWidth;

		SetMaxContentWidth(newWidth);
		if (newWidth == maxItemWidth)
			fOverflow = false;

		InvalidateLayout();

		for (int32 index = 0; ; index++) {
			TTeamMenuItem* item = (TTeamMenuItem*)ItemAt(index);
			if (item == NULL)
				break;

			item->SetOverrideWidth(newWidth);
		}

		Invalidate();
		Window()->UpdateIfNeeded();
		fBarView->CheckForScrolling();
	}
}


menu_layout
TExpandoMenuBar::MenuLayout() const
{
	return Layout();
}


void
TExpandoMenuBar::Draw(BRect updateRect)
{
	BMenu::Draw(updateRect);
}


void
TExpandoMenuBar::DrawBackground(BRect updateRect)
{
	if (fVertical)
		return;

	BRect bounds(Bounds());
	rgb_color menuColor = LowColor();
	rgb_color hilite = tint_color(menuColor, B_DARKEN_1_TINT);
	rgb_color vlight = tint_color(menuColor, B_LIGHTEN_2_TINT);

	int32 count = CountItems() - 1;
	if (count >= 0)
		bounds.left = ItemAt(count)->Frame().right + 1;
	else
		bounds.left = 0;

	if (be_control_look != NULL) {
		SetHighColor(tint_color(menuColor, 1.22));
		StrokeLine(bounds.LeftTop(), bounds.LeftBottom());
		bounds.left++;
		uint32 borders = BControlLook::B_TOP_BORDER
			| BControlLook::B_BOTTOM_BORDER | BControlLook::B_RIGHT_BORDER;

		be_control_look->DrawButtonBackground(this, bounds, bounds, menuColor,
			0, borders);
	} else {
		SetHighColor(vlight);
		StrokeLine(bounds.LeftTop(), bounds.RightTop());
		StrokeLine(BPoint(bounds.left, bounds.top + 1), bounds.LeftBottom());
		SetHighColor(hilite);
		StrokeLine(BPoint(bounds.left + 1, bounds.bottom),
			bounds.RightBottom());
	}
}


/*!	Something to help determine if we are showing too many apps
	need to add in scrolling functionality.
*/
bool
TExpandoMenuBar::CheckForSizeOverrun()
{
	if (fVertical) {
		BRect screenFrame = (BScreen(Window())).Frame();
		return Window()->Frame().bottom > screenFrame.bottom;
	}

	// horizontal
	int32 count = CountItems() - 1;
	if (count < 0)
		return false;

	int32 iconSize = static_cast<TBarApp*>(be_app)->IconSize();
	float iconOnlyWidth = kIconPadding + iconSize + kIconPadding;
	float minItemWidth = fDrawLabel
		? iconOnlyWidth + kMinMenuItemWidth
		: iconOnlyWidth - kIconPadding;
	float menuWidth = minItemWidth * CountItems() + fDeskbarMenuWidth
		+ kSepItemWidth;
	float maxWidth = fBarView->DragRegion()->Frame().left
		- fDeskbarMenuWidth - kSepItemWidth;

	return menuWidth > maxWidth;
}


void
TExpandoMenuBar::SetMaxItemWidth()
{
	if (fVertical)
		SetMaxContentWidth(sMinimumWindowWidth);
	else {
		// Make more room for the icon in horizontal mode
		int32 iconSize = static_cast<TBarApp*>(be_app)->IconSize();
		SetMaxContentWidth(sMinimumWindowWidth + iconSize
			- kMinimumIconSize);
	}
}


void
TExpandoMenuBar::SizeWindow(int32 delta)
{
	// instead of resizing the window here and there in the
	// code the resize method will be centered in one place
	// thus, the same behavior (good or bad) will be used
	// wherever window sizing is done
	if (fVertical) {
		BRect screenFrame = (BScreen(Window())).Frame();
		fBarView->SizeWindow(screenFrame);
		fBarView->PositionWindow(screenFrame);
		fBarView->CheckForScrolling();
	} else
		CheckItemSizes(delta);
}


int32
TExpandoMenuBar::monitor_team_windows(void* arg)
{
	TExpandoMenuBar* teamMenu = (TExpandoMenuBar*)arg;

	while (teamMenu->sDoMonitor) {
		sMonLocker.Lock();

		if (teamMenu->Window()->LockWithTimeout(50000) == B_OK) {
			int32 totalItems = teamMenu->CountItems();

			// Set all WindowMenuItems to require an update.
			TWindowMenuItem* item = NULL;
			for (int32 i = 0; i < totalItems; i++) {
				if (!teamMenu->SubmenuAt(i)) {
					item = static_cast<TWindowMenuItem*>(teamMenu->ItemAt(i));
					item->SetRequireUpdate();
				}
			}

			// Perform SetTo() on all the items that still exist as well as add
			// new items.
			bool itemModified = false, resize = false;
			TTeamMenuItem* teamItem = NULL;

			for (int32 i = 0; i < totalItems; i++) {
				if (teamMenu->SubmenuAt(i) == NULL)
					continue;

				teamItem = static_cast<TTeamMenuItem*>(teamMenu->ItemAt(i));
				if (teamItem->IsExpanded()) {
					int32 teamCount = teamItem->Teams()->CountItems();
					for (int32 j = 0; j < teamCount; j++) {
						// The following code is almost a copy/paste from
						// WindowMenu.cpp
						team_id	theTeam = (addr_t)teamItem->Teams()->ItemAt(j);
						int32 count = 0;
						int32* tokens = get_token_list(theTeam, &count);

						for (int32 k = 0; k < count; k++) {
							client_window_info* wInfo
								= get_window_info(tokens[k]);
							if (wInfo == NULL)
								continue;

							if (TWindowMenu::WindowShouldBeListed(wInfo)) {
								// Check if we have a matching window item...
								item = teamItem->ExpandedWindowItem(
									wInfo->server_token);
								if (item) {
									item->SetTo(wInfo->name,
										wInfo->server_token, wInfo->is_mini,
										((1 << current_workspace())
											& wInfo->workspaces) != 0);

									if (strcmp(wInfo->name,
										item->Label()) != 0)
										item->SetLabel(wInfo->name);

									if (item->ChangedState())
										itemModified = true;
								} else if (teamItem->IsExpanded()) {
									// Add the item
									item = new TWindowMenuItem(wInfo->name,
										wInfo->server_token, wInfo->is_mini,
										((1 << current_workspace())
											& wInfo->workspaces) != 0, false);
									item->ExpandedItem(true);
									teamMenu->AddItem(item,
										TWindowMenuItem::InsertIndexFor(
											teamMenu, i + 1, item));
									resize = true;
								}
							}
							free(wInfo);
						}
						free(tokens);
					}
				}
			}

			// Remove any remaining items which require an update.
			for (int32 i = 0; i < totalItems; i++) {
				if (!teamMenu->SubmenuAt(i)) {
					item = static_cast<TWindowMenuItem*>(teamMenu->ItemAt(i));
					if (item && item->RequiresUpdate()) {
						item = static_cast<TWindowMenuItem*>
							(teamMenu->RemoveItem(i));
						delete item;
						totalItems--;

						resize = true;
					}
				}
			}

			// If any of the WindowMenuItems changed state, we need to force a
			// repaint.
			if (itemModified || resize) {
				teamMenu->Invalidate();
				if (resize)
					teamMenu->SizeWindow(1);
			}

			teamMenu->Window()->Unlock();
		}

		sMonLocker.Unlock();

		// sleep for a bit...
		snooze(150000);
	}
	return B_OK;
}


void
TExpandoMenuBar::_FinishedDrag(bool invoke)
{
	if (fPreviousDragTargetItem != NULL) {
		if (invoke)
			fPreviousDragTargetItem->Invoke();
		fPreviousDragTargetItem->SetOverrideSelected(false);
		fPreviousDragTargetItem = NULL;
	}
	if (!invoke && fBarView->Dragging())
		fBarView->DragStop(true);
}
