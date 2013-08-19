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


#include "TimeView.h"

#include <string.h>

#include <Application.h>
#include <Catalog.h>
#include <Debug.h>
#include <Locale.h>
#include <MenuItem.h>
#include <MessageRunner.h>
#include <PopUpMenu.h>
#include <Roster.h>
#include <Screen.h>
#include <Window.h>

#include "CalendarMenuWindow.h"


static const char*  const kMinString = "99:99 AM";
static const float kHMargin = 2.0;


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "TimeView"


TTimeView::TTimeView(float maxWidth, float height)
	:
	BView(BRect(-100, -100, -90, -90), "_deskbar_tv_",
		B_FOLLOW_RIGHT | B_FOLLOW_TOP,
		B_WILL_DRAW | B_PULSE_NEEDED | B_FRAME_EVENTS),
	fParent(NULL),
	fMaxWidth(maxWidth),
	fHeight(height),
	fOrientation(true),
	fShowLevel(0),
	fShowSeconds(false),
	fShowDayOfWeek(false),
	fShowTimeZone(false)
{
	fCurrentTime = fLastTime = time(NULL);
	fSeconds = fMinute = fHour = 0;
	fCurrentTimeStr[0] = 0;
	fCurrentDateStr[0] = 0;
	fLastTimeStr[0] = 0;
	fLastDateStr[0] = 0;
	fNeedToUpdate = true;
	fLocale = *BLocale::Default();
}


#ifdef AS_REPLICANT
TTimeView::TTimeView(BMessage* data)
	: BView(data)
{
	fCurrentTime = fLastTime = time(NULL);
	data->FindBool("seconds", &fShowSeconds);

	fLocale = *BLocale::Default();
}
#endif


TTimeView::~TTimeView()
{
}


#ifdef AS_REPLICANT
BArchivable*
TTimeView::Instantiate(BMessage* data)
{
	if (!validate_instantiation(data, "TTimeView"))
		return NULL;

	return new TTimeView(data);
}


status_t
TTimeView::Archive(BMessage* data, bool deep) const
{
	BView::Archive(data, deep);
	data->AddBool("orientation", fOrientation);
	data->AddInt16("showLevel", fShowLevel);
	data->AddBool("showSeconds", fShowSeconds);
	data->AddBool("showDayOfWeek", fShowDayOfWeek);
	data->AddBool("showTimeZone", fShowTimeZone);
	data->AddInt32("deskbar:private_align", B_ALIGN_RIGHT);

	return B_OK;
}
#endif


void
TTimeView::AttachedToWindow()
{
	fCurrentTime = time(NULL);

	SetFont(be_plain_font);
	if (Parent()) {
		fParent = Parent();
		SetViewColor(Parent()->ViewColor());
	} else
		SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));

	CalculateTextPlacement();
	ResizeToPreferred();
}


void
TTimeView::Draw(BRect /*updateRect*/)
{
	PushState();

	SetHighColor(ViewColor());
	SetLowColor(ViewColor());
	FillRect(Bounds());
	SetHighColor(ui_color(B_MENU_ITEM_TEXT_COLOR));

	DrawString(fCurrentTimeStr, fTimeLocation);

	PopState();
}


void
TTimeView::FrameMoved(BPoint)
{
	Update();
}


void
TTimeView::GetPreferredSize(float* width, float* height)
{
	*height = fHeight;

	GetCurrentTime();

	// TODO: SetOrientation never gets called, fix that when in vertical mode,
	// we want to limit the width so that it can't overlap the bevels in the
	// parent view.
	*width = fOrientation ?
		min_c(fMaxWidth - kHMargin, kHMargin + StringWidth(fCurrentTimeStr))
		: kHMargin + StringWidth(fCurrentTimeStr);
}


void
TTimeView::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kChangeTime:
		{
			// launch the time prefs app
			be_roster->Launch("application/x-vnd.Haiku-Time");
			// tell Time preflet to switch to the clock tab
			BMessenger messenger("application/x-vnd.Haiku-Time");
			BMessage* switchToClock = new BMessage('SlCk');
			messenger.SendMessage(switchToClock);
			break;
		}

		case kShowHideTime:
		{
			be_app->MessageReceived(message);
			break;
		}

		case kShowCalendar:
		{
			BRect bounds(Bounds());
			BPoint center(bounds.LeftTop());
			center += BPoint(bounds.Width() / 2, bounds.Height() / 2);
			ShowCalendar(center);
			break;
		}

		default:
			BView::MessageReceived(message);
			break;
	}
}


void
TTimeView::MouseDown(BPoint point)
{
	uint32 buttons;

	Window()->CurrentMessage()->FindInt32("buttons", (int32*)&buttons);
	if (buttons == B_SECONDARY_MOUSE_BUTTON) {
		ShowTimeOptions(ConvertToScreen(point));
		return;
	} else if (buttons == B_PRIMARY_MOUSE_BUTTON)
		ShowCalendar(point);

	// invalidate last time/date strings and call the pulse
	// method directly to change the display instantly
	fLastDateStr[0] = '\0';
	fLastTimeStr[0] = '\0';
	Pulse();
}


void
TTimeView::Pulse()
{
	time_t curTime = time(NULL);
	tm* ct = localtime(&curTime);
	if (ct == NULL)
		return;

	fCurrentTime = curTime;

	GetCurrentTime();
	GetCurrentDate();
	if (strcmp(fCurrentTimeStr, fLastTimeStr) != 0) {
		// Update bounds when the size of the strings has changed
		Update();

		strlcpy(fLastTimeStr, fCurrentTimeStr, sizeof(fLastTimeStr));
		fNeedToUpdate = true;
	}

	// Update the tooltip if the date has changed
	if (strcmp(fCurrentDateStr, fLastDateStr) != 0) {
		strlcpy(fLastDateStr, fCurrentDateStr, sizeof(fLastDateStr));
		SetToolTip(fCurrentDateStr);
	}

	if (fNeedToUpdate) {
		fSeconds = ct->tm_sec;
		fMinute = ct->tm_min;
		fHour = ct->tm_hour;

		Draw(Bounds());
		fNeedToUpdate = false;
	}
}


void
TTimeView::ResizeToPreferred()
{
	float width, height;
	float oldWidth = Bounds().Width(), oldHeight = Bounds().Height();

	GetPreferredSize(&width, &height);
	if (height != oldHeight || width != oldWidth) {
		ResizeTo(width, height);
		MoveBy(oldWidth - width, 0);
		fNeedToUpdate = true;
	}
}


//	# pragma mark - Public methods


void
TTimeView::SetOrientation(bool orientation)
{
	fOrientation = orientation;
	CalculateTextPlacement();
	Invalidate();
}


bool
TTimeView::ShowSeconds() const
{
	return fShowSeconds;
}


void
TTimeView::SetShowSeconds(bool show)
{
	fShowSeconds = show;
	Update();
}


bool
TTimeView::ShowDayOfWeek() const
{
	return fShowDayOfWeek;
}


void
TTimeView::SetShowDayOfWeek(bool show)
{
	fShowDayOfWeek = show;
	Update();
}


bool
TTimeView::ShowTimeZone() const
{
	return fShowTimeZone;
}


void
TTimeView::SetShowTimeZone(bool show)
{
	fShowTimeZone = show;
	Update();
}


void
TTimeView::ShowCalendar(BPoint where)
{
	if (fCalendarWindow.IsValid()) {
		// If the calendar is already shown, just activate it
		BMessage activate(B_SET_PROPERTY);
		activate.AddSpecifier("Active");
		activate.AddBool("data", true);

		if (fCalendarWindow.SendMessage(&activate) == B_OK)
			return;
	}

	where.y = Bounds().bottom + 4.0;
	ConvertToScreen(&where);

	if (where.y >= BScreen().Frame().bottom)
		where.y -= (Bounds().Height() + 4.0);

	CalendarMenuWindow* window = new CalendarMenuWindow(where);
	fCalendarWindow = BMessenger(window);

	window->Show();
}


//	# pragma mark - Private methods


void
TTimeView::GetCurrentTime()
{
	ssize_t offset_dow = 0;
	ssize_t offset_time = 0;

	// ToDo: Check to see if we should write day of week after time for locale

	if (fShowDayOfWeek) {
		BString timeFormat("eee ");
		offset_dow = fLocale.FormatTime(fCurrentTimeStr,
			sizeof(fCurrentTimeStr), fCurrentTime, timeFormat);

		if (offset_dow < 0) {
			// error occured, attempt to overwrite with current time
			// (this should not ever happen)
			fLocale.FormatTime(fCurrentTimeStr, sizeof(fCurrentTimeStr),
				fCurrentTime,
				fShowSeconds ? B_MEDIUM_TIME_FORMAT : B_SHORT_TIME_FORMAT);
			return;
		}
	}

	offset_time = fLocale.FormatTime(fCurrentTimeStr + offset_dow,
		sizeof(fCurrentTimeStr) - offset_dow, fCurrentTime,
		fShowSeconds ? B_MEDIUM_TIME_FORMAT : B_SHORT_TIME_FORMAT);

	if (fShowTimeZone) {
		BString timeFormat(" V");
		ssize_t offset = offset_dow + offset_time;
		fLocale.FormatTime(fCurrentTimeStr + offset,
			sizeof(fCurrentTimeStr) - offset, fCurrentTime, timeFormat);
	}
}


void
TTimeView::GetCurrentDate()
{
	char tmp[sizeof(fCurrentDateStr)];

	fLocale.FormatDate(tmp, sizeof(fCurrentDateStr), fCurrentTime,
		B_FULL_DATE_FORMAT);

	// remove leading 0 from date when month is less than 10 (MM/DD/YY)
	// or remove leading 0 from date when day is less than 10 (DD/MM/YY)
	const char* str = tmp;
	if (str[0] == '0')
		str++;

	strlcpy(fCurrentDateStr, str, sizeof(fCurrentDateStr));
}


void
TTimeView::CalculateTextPlacement()
{
	BRect bounds(Bounds());

	fDateLocation.x = 0.0;
	fTimeLocation.x = 0.0;

	BFont font;
	GetFont(&font);

	const char* stringArray[1];
	stringArray[0] = fCurrentTimeStr;
	BRect rectArray[1];
	escapement_delta delta = { 0.0, 0.0 };
	font.GetBoundingBoxesForStrings(stringArray, 1, B_SCREEN_METRIC, &delta,
		rectArray);

	fTimeLocation.y = fDateLocation.y = ceilf((bounds.Height()
		- rectArray[0].Height() + 1.0) / 2.0 - rectArray[0].top);
}


void
TTimeView::ShowTimeOptions(BPoint point)
{
	BPopUpMenu* menu = new BPopUpMenu("", false, false);
	menu->SetFont(be_plain_font);
	BMenuItem* item;

	item = new BMenuItem(B_TRANSLATE("Time preferences" B_UTF8_ELLIPSIS),
		new BMessage(kChangeTime));
	menu->AddItem(item);

	item = new BMenuItem(B_TRANSLATE("Hide clock"),
		new BMessage(kShowHideTime));
	menu->AddItem(item);

	item = new BMenuItem(B_TRANSLATE("Show calendar" B_UTF8_ELLIPSIS),
		new BMessage(kShowCalendar));
	menu->AddItem(item);

	menu->SetTargetForItems(this);
	// Changed to accept screen coord system point;
	// not constrained to this view now
	menu->Go(point, true, true, BRect(point.x - 4, point.y - 4,
		point.x + 4, point.y +4), true);
}


void
TTimeView::Update()
{
	fLocale = *BLocale::Default();

	GetCurrentTime();
	GetCurrentDate();
	SetToolTip(fCurrentDateStr);

	CalculateTextPlacement();
	ResizeToPreferred();

	if (fParent != NULL)
		fParent->Invalidate();
}
