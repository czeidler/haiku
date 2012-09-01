/*
 * Copyright 2011, Clemens Zeidler <haiku@clemens-zeidler.de>
 * Distributed under the terms of the MIT License.
 */


#include <BitmapLayerView.h>

#include <algorithm>

#include "LayerViewDefinitions.h"


using namespace std;


LayerItem::LayerItem(BitmapLayerView* parent)
	:
	fView(parent),
	fFocus(false)
{
}


LayerItem::~LayerItem()
{
}


bool
LayerItem::Selected() const
{
	return fSelected;
}


void
LayerItem::SetSelected(bool selected)
{
	fSelected = selected;
}


void
LayerItem::SetFocus(bool focus)
{
	fFocus = focus;
}


bool
LayerItem::Focus()
{
	return fFocus;
}


BitmapLayerView::BitmapLayerView()
	:
	BView("BitmapLayerView", B_WILL_DRAW | B_FULL_UPDATE_ON_RESIZE),
	fFocusItem(NULL),
	fSelectionStarted(false)
{
	SetViewColor(kBackGroundColor);
}


bool
BitmapLayerView::AddLayer(LayerItem* layer)
{
	if (fLayers.AddItem(layer) == false)
		return false;
	SetFocus(layer);
	return true;
}


bool
BitmapLayerView::RemoveLayer(LayerItem* layer)
{
	return fLayers.RemoveItem(layer);
}


int32
BitmapLayerView::CountLayers() const
{
	return fLayers.CountItems();
}


LayerItem*
BitmapLayerView::LayerAt(int32 index) const
{
	return fLayers.ItemAt(index);
}


bool
BitmapLayerView::MoveLayer(int32 from, int32 to)
{
	return fLayers.MoveItem(from, to);
}


void
BitmapLayerView::MoveToTop(LayerItem* layer)
{
	MoveLayer(fLayers.IndexOf(layer), fLayers.CountItems() - 1);
	Invalidate();
}


void
BitmapLayerView::SetFocus(LayerItem* layer)
{
	if (fFocusItem == layer)
		return;
	if (layer)
		layer->SetFocus(true);
	if (fFocusItem != NULL)
		fFocusItem->SetFocus(false);
	fFocusItem = layer;
	Invalidate();
}


void
BitmapLayerView::Activate(LayerItem* layer)
{
	if  (layer->Selected() == false)
		SetSelectionRect(BRect(0, 0, -1, -1));
	MoveToTop(layer);
	SetFocus(layer);
}


void
BitmapLayerView::Draw(BRect updateRect)
{
	for (int32 i = 0; i < fLayers.CountItems(); i++)
		fLayers.ItemAt(i)->Draw(updateRect);

	if (fSelectionStarted == true) {
		BRect rect = fSelectionRect;

		rgb_color white = {255, 255, 255, 200};
		SetHighColor(white);
		StrokeRect(rect);

		SetDrawingMode(B_OP_ALPHA);
		SetHighColor(kShadowColor);
		rect.InsetBy(1, 1);
		FillRect(rect);
	}
}


void
BitmapLayerView::MouseMoved(BPoint point, uint32 transit,
	const BMessage* message)
{
	for (int32 i = fLayers.CountItems() - 1; i >= 0; i--) {
		LayerItem* item = fLayers.ItemAt(i);
		bool handled = item->MouseMoved(point, transit, message);
		if (handled == true)
			return;
	}

	if (fSelectionStarted) {
		SetSelectionRect(BRect(min(fSelectionStartPoint.x, point.x),
			min(fSelectionStartPoint.y, point.y),
			max(fSelectionStartPoint.x, point.x),
			max(fSelectionStartPoint.y, point.y)));
		Invalidate();
	}

}


void
BitmapLayerView::MouseDown(BPoint point)
{
	for (int32 i = fLayers.CountItems() - 1; i >= 0; i--) {
		LayerItem* item = fLayers.ItemAt(i);
		bool handled = item->MouseDown(point);
		if (handled == true)
			return;
	}
	SetFocus(NULL);
	fSelectionStarted = true;
	fSelectionStartPoint = point;
	fSelectionRect = BRect(0, 0, -1, -1);
}


void
BitmapLayerView::MouseUp(BPoint point)
{
	if (fSelectionStarted == true) {
		fSelectionStarted = false;
		if (fSelectionStartPoint == point)
			SetSelectionRect(BRect(0, 0, -1, -1));
		Invalidate();
	}

	for (int32 i = fLayers.CountItems() - 1; i >= 0; i--) {
		LayerItem* item = fLayers.ItemAt(i);
		bool handled = item->MouseUp(point);
		if (handled == true)
			return;
	}
}



BRect
BitmapLayerView::RasterFrame(const BRect& _frame)
{
	BRect frame = _frame;
	frame.OffsetBy(-int(frame.left) % kRasterSize,
		-int(frame.top) % kRasterSize);
	return frame;
}


BPoint
BitmapLayerView::FromRasterPoint(const BPoint& point)
{
	BPoint raster = point;
	raster.x += int(raster.x) % kRasterSize;
	raster.y += int(raster.y) % kRasterSize;
	return raster;
}


void
BitmapLayerView::SetSelectionRect(BRect selection)
{
	fSelectionRect = selection;

	for (int32 i = 0; i < fLayers.CountItems(); i++) {
		BoxItem* item = dynamic_cast<BoxItem*>(fLayers.ItemAt(i));
		if (item == NULL)
			continue;
		bool inSelection = selection.Intersects(item->RasterFrame());
		item->SetSelected(inSelection);
	}
}


void
BitmapLayerView::DeselectAll()
{
	for (int32 i = 0; i < fLayers.CountItems(); i++)
		fLayers.ItemAt(i)->SetSelected(false);
}


void
BitmapLayerView::MoveSelectionBy(const BPoint& offset, BoxItem* caller)
{
	for (int32 i = 0; i < fLayers.CountItems(); i++) {
		BoxItem* item = dynamic_cast<BoxItem*>(fLayers.ItemAt(i));
		if (item == NULL)
			continue;
		if (item->Selected() == false || item == caller)
			continue;
		item->MoveBy(offset);
	}
}									


BoxItem::BoxItem(BitmapLayerView* parent, const char* title)
	:
	LayerItem(parent),

	fFrame(0, 0, 100, 100),
	fTitle(title),
	fDragStarted(false)
{
}


void
BoxItem::SetFrame(const BRect& frame)
{
	fFrame = frame;
}


BRect
BoxItem::Frame() const
{
	return fFrame;
}


BRect
BoxItem::RasterFrame() const
{
	return fView->RasterFrame(fFrame);
}


void
BoxItem::MoveBy(const BPoint& offset)
{
	fFrame.OffsetBy(offset);
}


void
BoxItem::SetSize(float width, float heigth)
{
	fFrame.right = fFrame.left + width;
	fFrame.bottom = fFrame.top + heigth;
}


void
BoxItem::SetPosition(BPoint position)
{
	fFrame.OffsetBy(position - fFrame.LeftTop());
}


BPoint
BoxItem::Position() const
{
	return fFrame.LeftTop();
}


void
BoxItem::SetTitle(const char* title)
{
	fTitle = title;
}


const char*
BoxItem::Title() const
{
	return fTitle;
}


void
BoxItem::Draw(BRect updateRect)
{
	fView->PushState();

	BRect frame = fView->RasterFrame(fFrame);

	if (Focus() == true) {
		BRect shadowFrame = frame;
		shadowFrame.OffsetBy(3, 3);
		fView->SetHighColor(kShadowColor);
		fView->SetDrawingMode(B_OP_ALPHA);
		fView->FillRoundRect(shadowFrame, 2, 2);
		fView->SetDrawingMode(B_OP_OVER);
	}
	if (Selected() == true) {
		BRect selectFrame = frame;
		selectFrame.InsetBy(-1, -1);
		fView->SetHighColor(kSelectedColor);
		fView->StrokeRect(selectFrame);
	}

	fView->SetHighColor(kBoxColor);
	fView->FillRect(frame);

	fView->BeginLineArray(8);
	fView->AddLine(frame.LeftTop(), frame.RightTop(), kBorderDarkColor);
	fView->AddLine(frame.RightTop(), frame.RightBottom(), kBorderDarkColor);
	fView->AddLine(frame.RightBottom(), frame.LeftBottom(), kBorderDarkColor);
	fView->AddLine(frame.LeftBottom(), frame.LeftTop(), kBorderDarkColor);
	frame.InsetBy(1.0, 1.0);
	fView->AddLine(frame.LeftTop(), frame.RightTop(), kBorderLightColor);
	fView->AddLine(frame.RightTop(), frame.RightBottom(), kBorderMediumColor);
	fView->AddLine(frame.RightBottom(), frame.LeftBottom(), kBorderMediumColor);
	fView->AddLine(frame.LeftBottom(), frame.LeftTop(), kBorderLightColor);
	fView->EndLineArray();

	_DrawTitle(frame);

	fView->PopState();
}


bool
BoxItem::MouseMoved(BPoint point, uint32 transit,
	const BMessage* message)
{
	if (fDragStarted == false)
		return false;
	BPoint offset = point - fPrevMousePosition;
	fFrame.OffsetBy(offset);
	if (Selected() == true)
		fView->MoveSelectionBy(offset, this);

	fPrevMousePosition = point;
	fView->LayerChanged(this);
	fView->Invalidate();
	return true;
}


bool
BoxItem::MouseDown(BPoint point)
{
	if (fFrame.Contains(point) == false)
		return false;

	fView->Activate(this);

	fDragStarted = true;
	fPrevMousePosition = point;
	return true;
}


bool
BoxItem::MouseUp(BPoint point)
{
	if (fDragStarted == false)
		return false;

	BRect rasterFrame = RasterFrame();
	if (Selected() == true) {
		BPoint diff = rasterFrame.LeftTop() - fFrame.LeftTop();
		fView->MoveSelectionBy(diff, this);
	}
	SetFrame(rasterFrame);

	fDragStarted = false;
	return true;
}


void
BoxItem::_DrawTitle(const BRect& frame)
{
	BString label = Title();
	float labelBoxWidth = frame.Width() - 2 * kBoxBorderSpace;
	fView->TruncateString(&label, B_TRUNCATE_END, labelBoxWidth);
	font_height fontHeight;
	fView->GetFontHeight(&fontHeight);
	float labelHeight = fontHeight.ascent + fontHeight.descent;
	float labelWidth = fView->StringWidth(label); 
	BPoint labelPos = frame.LeftTop();
	labelPos.x += (frame.Width() - labelWidth) / 2;
	labelPos.y += kBoxBorderSpace + fontHeight.ascent;

	if (Focus() == true) {
		BRect titleBox = frame;
		titleBox.InsetBy(kBoxBorderSpace, kBoxBorderSpace); 
		titleBox.bottom = titleBox.top + labelHeight;
		fView->SetHighColor(kFocusBoxColor);
		fView->FillRect(titleBox);
	}

	if (Focus() == true) {
		fView->SetLowColor(kFocusBoxColor);
		fView->SetHighColor(kFocusTitleColor);
	} else {
		fView->SetLowColor(kBoxColor);
		fView->SetHighColor(kTitleColor);
	}
	if (labelPos.y < frame.bottom)
		fView->DrawString(label, labelPos);
}
