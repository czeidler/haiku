/*
 * Copyright 2010-2012, Haiku, Inc.
 * Copyright 2006, Ingo Weinhold <bonefish@cs.tu-berlin.de>.
 * All rights reserved. Distributed under the terms of the MIT License.
 */


#include <LayoutItem.h>

#include <Layout.h>
#include <LayoutUtils.h>
#include <View.h>
#include <ViewPrivate.h>

#include <algorithm>


BLayoutItem::BLayoutItem()
	:
	fLayout(NULL),
	fLayoutData(NULL)
{
}


BLayoutItem::BLayoutItem(BMessage* from)
	:
	BArchivable(BUnarchiver::PrepareArchive(from)),
	fLayout(NULL),
	fLayoutData(NULL)
{
	BUnarchiver(from).Finish();
}


BLayoutItem::~BLayoutItem()
{
	if (fLayout)
		fLayout->RemoveItem(this);
}


BLayout*
BLayoutItem::Layout() const
{
	return fLayout;
}


void
BLayoutItem::SetExplicitSize(BSize size)
{
	SetExplicitMinSize(size);
	SetExplicitMaxSize(size);
	SetExplicitPreferredSize(size);
}


bool
BLayoutItem::HasHeightForWidth()
{
	// no "height for width" by default
	return false;
}


void
BLayoutItem::GetHeightForWidth(float width, float* min, float* max,
	float* preferred)
{
	// no "height for width" by default
}


BView*
BLayoutItem::View()
{
	return NULL;
}


void
BLayoutItem::InvalidateLayout(bool children)
{
	LayoutInvalidated(children);
	if (fLayout)
		fLayout->InvalidateLayout(children);
}


void
BLayoutItem::Relayout(bool immediate)
{
	BView* view = View();
	if (view && !immediate)
		view->Relayout();
	else if (view && immediate)
		view->Layout(false);
}


void*
BLayoutItem::LayoutData() const
{
	return fLayoutData;
}


void
BLayoutItem::SetLayoutData(void* data)
{
	fLayoutData = data;
}


void
BLayoutItem::AlignInFrame(BRect frame)
{
	BSize maxSize = MaxSize();
	BAlignment alignment = Alignment();

	if (HasHeightForWidth()) {
		// The item has height for width, so we do the horizontal alignment
		// ourselves and restrict the height max constraint respectively.
		if (maxSize.width < frame.Width()
			&& alignment.horizontal != B_ALIGN_USE_FULL_WIDTH) {
			frame.left += (int)((frame.Width() - maxSize.width)
				* alignment.horizontal);
			frame.right = frame.left + maxSize.width;
		}
		alignment.horizontal = B_ALIGN_USE_FULL_WIDTH;

		float minHeight;
		GetHeightForWidth(frame.Width(), &minHeight, NULL, NULL);

		frame.bottom = frame.top + max_c(frame.Height(), minHeight);
		maxSize.height = minHeight;
	}

	SetFrame(BLayoutUtils::AlignInFrame(frame, maxSize, alignment));
}


status_t
BLayoutItem::Archive(BMessage* into, bool deep) const
{
	BArchiver archiver(into);
	status_t err = BArchivable::Archive(into, deep);

	if (err == B_OK)
		err = archiver.Finish();

	return err;
}


status_t
BLayoutItem::AllArchived(BMessage* into) const
{
	BArchiver archiver(into);
	return BArchivable::AllArchived(into);
}


status_t
BLayoutItem::AllUnarchived(const BMessage* from)
{
	return BArchivable::AllUnarchived(from);
}


void
BLayoutItem::SetLayout(BLayout* layout)
{
	if (layout == fLayout)
		return;

	BLayout* oldLayout = fLayout;
	fLayout = layout;

	if (oldLayout)
		DetachedFromLayout(oldLayout);

	if (BView* view = View()) {
		if (oldLayout && !fLayout) {
			BView::Private(view).DeregisterLayoutItem(this);
		} else if (fLayout && !oldLayout) {
			BView::Private(view).RegisterLayoutItem(this);
		}
	}

	if (fLayout)
		AttachedToLayout();
}


status_t
BLayoutItem::Perform(perform_code code, void* _data)
{
	return BArchivable::Perform(code, _data);
}


void
BLayoutItem::LayoutInvalidated(bool children)
{
	// hook method
}


void
BLayoutItem::AttachedToLayout()
{
	// hook method
}


void
BLayoutItem::DetachedFromLayout(BLayout* oldLayout)
{
	// hook method
}


void
BLayoutItem::AncestorVisibilityChanged(bool shown)
{
	// hook method
}


// Binary compatibility stuff


void BLayoutItem::_ReservedLayoutItem1() {}
void BLayoutItem::_ReservedLayoutItem2() {}
void BLayoutItem::_ReservedLayoutItem3() {}
void BLayoutItem::_ReservedLayoutItem4() {}
void BLayoutItem::_ReservedLayoutItem5() {}
void BLayoutItem::_ReservedLayoutItem6() {}
void BLayoutItem::_ReservedLayoutItem7() {}
void BLayoutItem::_ReservedLayoutItem8() {}
void BLayoutItem::_ReservedLayoutItem9() {}
void BLayoutItem::_ReservedLayoutItem10() {}

