/*
 * Copyright 2012, Clemens Zeidler <haiku@clemens-zeidler.de>
 * Distributed under the terms of the MIT License.
 */
#include <PreviewWindow.h>

#include <SpaceLayoutItem.h>
#include <Autolock.h>
#include <MenuBar.h>

#include "ALMEditor.h"
#include "CustomizableRoster.h"
#include "CustomizableView.h"
#include "LayoutArchive.h"
#include "LayoutEditView.h"


using namespace BALM;


void
ScaleLayout::Scale(BWindow* window, BALMLayout* layout, float scaleFactor)
{
	window->LockLooper();

/*	BSize layoutPrefSize = layout->PreferredSize();
	layoutPrefSize.width *= scaleFactor;
	layoutPrefSize.height *= scaleFactor;

	float minWidth;
	float maxWidth;
	float minHeight;
	float maxHeight;
	window->GetSizeLimits(&minWidth, &maxWidth, &minHeight, &maxHeight);
	if (maxWidth < layoutPrefSize.width)
		maxWidth = layoutPrefSize.width;
	if (maxHeight < layoutPrefSize.height)
		maxHeight = layoutPrefSize.height;
	window->SetSizeLimits(minWidth, maxWidth, minHeight, maxHeight);

	window->ResizeTo(layoutPrefSize.width, layoutPrefSize.height);
*/
	BArray<BSize> originalPrefSizes;
	for (int32 i = 0; i < layout->CountAreas(); i++) {
		Area* area = layout->AreaAt(i);
		BLayoutItem* item = layout->ItemAt(i);

		// hack on
		BMessage message;
		item->Archive(&message);
		BSize expPreferredSize;
		message.FindSize("BAbstractLayoutItem:sizes", 2, &expPreferredSize);				
		// hack off

		originalPrefSizes.AddItem(expPreferredSize);

		/*BSize prefSize = item->PreferredSize();
		if (prefSize.width > 0)
			prefSize.width *= scaleFactor;
		if (prefSize.height > 0)
			prefSize.height *= scaleFactor;
		item->SetExplicitPreferredSize(prefSize);*/
		BSize prefSize = item->PreferredSize();
		prefSize.width = area->Frame().Width() * scaleFactor;
		prefSize.height = area->Frame().Height() * scaleFactor;
		item->SetExplicitPreferredSize(prefSize);
	}

	BSize layoutPrefSize = layout->PreferredSize();

	window->ResizeTo(layoutPrefSize.width, layoutPrefSize.height);

	// reset pref sizes
	for (int32 i = 0; i < layout->CountItems(); i++) {
		BLayoutItem* item = layout->ItemAt(i);
		item->SetExplicitPreferredSize(originalPrefSizes.ItemAt(i));
	}

	window->UnlockLooper();
}


const int32 kMsgNewPrevWindow = '&nPW';
const int32 kMsgMinSizePreview = '&mSP';
const int32 kMsgCustomSizePreview = '&cSP';
const int32 kMsgEnlargedSizePreview = '&oSP';


PreviewWindow::PreviewWindow(BALMLayout* parent, prev_size size,
	PreviewWindowManager* manager)
	:
	BWindow(BRect(400, 20, 700, 300), "Preview", B_TITLED_WINDOW, 0),
	fLayout(parent),
	fManager(manager)
{
	_SetSize(size);

	fManager->_AddPreviewWindow(this);

	fOwnLayout = new BALMLayout();
	SetLayout(fOwnLayout);

	BMenuBar* bar = new BMenuBar("MainMenu");
	bar->SetExplicitAlignment(BAlignment(B_ALIGN_USE_FULL_WIDTH,
		B_ALIGN_USE_FULL_HEIGHT));
	BMenu* menu = new BMenu("Window");
	bar->AddItem(menu);
	BMenuItem* newPreview = new BMenuItem("New Preview",
		new BMessage(kMsgNewPrevWindow));
	menu->AddItem(newPreview);

	BMenu* prevMenu = new BMenu("Preview");
	bar->AddItem(prevMenu);
	BMenuItem* minPreview = new BMenuItem("Minimal Size",
		new BMessage(kMsgMinSizePreview));
	BMenuItem* customPreview = new BMenuItem("Custom Size",
		new BMessage(kMsgCustomSizePreview));
	BMenuItem* enlargedPreview = new BMenuItem("Enlarged Size",
		new BMessage(kMsgEnlargedSizePreview));
	prevMenu->AddItem(minPreview);
	prevMenu->AddItem(customPreview);
	prevMenu->AddItem(enlargedPreview);

	fOwnLayout->AddView(bar, fOwnLayout->Left(), fOwnLayout->Top(),
		fOwnLayout->Right());
	fContainerView = new BView("ContainerView", B_FRAME_EVENTS
		| B_FULL_UPDATE_ON_RESIZE);
	//fContainerView->SetViewColor(B_TRANSPARENT_COLOR);
	fContainerView->SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));

	//fContainerView->SetExplicitAlignment(BAlignment(B_ALIGN_USE_FULL_WIDTH,
	//	B_ALIGN_USE_FULL_HEIGHT));
	//fContainerView->SetExplicitPreferredSize(BSize(-1, -1));
	fOwnLayout->AddView(fContainerView, fOwnLayout->Left(),
		fOwnLayout->BottomOf(bar), fOwnLayout->Right(), fOwnLayout->Bottom());

	PostMessage(kMsgLayoutEdited);
}


PreviewWindow::~PreviewWindow()
{
	for (int32 i = 0; i < fItems.CountItems(); i++)
		fItems.ItemAt(i)->RemoveSelf();
	fItems.MakeEmpty();
}


bool
PreviewWindow::QuitRequested()
{
	fManager->RemovePreviewWindow(this);
	return true;
}


void
PreviewWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgLayoutEdited:
		{
			_InitLayout();
			_AdjustSize();
			break;
		}

		case kMsgNewPrevWindow:
		{
			fManager->AddPreviewWindow(fLayout, PreviewWindow::kCustomSize);
			break;	
		}

		case kMsgMinSizePreview:
			_SetSize(kMinSize);
			_AdjustSize();
			break;

		case kMsgCustomSizePreview:
			_SetSize(kCustomSize);
			_AdjustSize();
			break;

		case kMsgEnlargedSizePreview:
			_SetSize(kEnlargedSize);
			_AdjustSize();
			break;

		default:
			BWindow::MessageReceived(message);
	}
}


void
PreviewWindow::_SetSize(prev_size size)
{
	fPrevSize = size;
	if (fPrevSize == kMinSize)
		SetTitle("Minimum Size");
	else if (fPrevSize == kCustomSize)
		SetTitle("Preview (Custom Size)");
	else if (fPrevSize == kEnlargedSize)
		SetTitle("Enlarged Size");
}


void
PreviewWindow::_AdjustSize()
{
	BAutolock _(fLayout->Owner()->Window());
	BSize minSize = fLayout->MinSize();
	BSize maxSize = fLayout->MaxSize();
	_.Unlock();

	if (fPrevSize == kMinSize)
		maxSize = minSize;
	else if (fPrevSize == kEnlargedSize) {
		_AdjustEnlargedSize();
		return;
	}

	fContainerView->SetExplicitMinSize(minSize);
	fContainerView->SetExplicitMaxSize(maxSize);

	BLayout* layout = GetLayout();
	minSize = layout->MinSize();
	maxSize = layout->MaxSize();

	if (fPrevSize == kMinSize)
		maxSize = minSize;

	SetSizeLimits(minSize.width, maxSize.width, minSize.height,
		maxSize.height);
}


void
PreviewWindow::_AdjustEnlargedSize()
{
	BAutolock _(fLayout->Owner()->Window());
	BRect frame = fLayout->Frame();
	_.Unlock();

	int32 xTabs = fOwnLayout->CountXTabs();
	int32 yTabs = fOwnLayout->CountYTabs();

	float widthDelta = frame.Width() * 0.1;
	widthDelta *= xTabs - 1;
	float heightDelta = frame.Height() * 0.1;
	heightDelta *= yTabs - 1;

	SetSizeLimits(frame.Width() + widthDelta, frame.Width() + widthDelta,
		frame.Height() + heightDelta,	frame.Height() + heightDelta);

	/* alternative method to increase the item size by 10%, not working though
	_InitLayout();

	for (int32 i = 0; i < fOwnLayout->CountAreas(); i++) {
		Area* area = fOwnLayout->AreaAt(i);
		BLayoutItem* item = area->Item();

		BSize minItemSize = item->MinSize();
		BSize prefItemSize = item->PreferredSize();
		BSize maxItemSize = item->MaxSize();

		minItemSize.width += 10;
		minItemSize.height += 10;
		if (minItemSize.width > maxItemSize.width)
			minItemSize.width = maxItemSize.width;
		if (minItemSize.height > maxItemSize.height)
			minItemSize.height = maxItemSize.height;

		float currentWidth = area->Frame().Width();
		float currentHeight = area->Frame().Height();

		prefItemSize.width = currentWidth * 1.1;
		prefItemSize.height = currentHeight * 1.1;
		if (prefItemSize.width > maxItemSize.width)
			prefItemSize.width = maxItemSize.width;
		if (prefItemSize.height > maxItemSize.height)
			prefItemSize.height = maxItemSize.height;

		item->SetExplicitMinSize(minItemSize);
		item->SetExplicitPreferredSize(prefItemSize);
	}
	fOwnLayout->InvalidateLayout(true);
	BSize previewPrefSize = fOwnLayout->PreferredSize();;
	*/
}


void
PreviewWindow::_InitLayout()
{
	CustomizableRoster* roster = CustomizableRoster::DefaultRoster();
	for (int32 i = 0; i < fItems.CountItems(); i++)
		fItems.ItemAt(i)->RemoveSelf();
	fItems.MakeEmpty();

	// remove old container view
	if (fContainerView->GetLayout())
		delete fContainerView->GetLayout()->RemoveItem(int32(0));

	BAutolock _(fLayout->Owner()->Window());
	
	BMessage archive;
	LayoutArchive(fLayout).SaveLayout(&archive);

	fOwnLayout = new BALMLayout;
	fContainerView->SetLayout(fOwnLayout);

	float left, top, right, bottom;
	fLayout->GetInsets(&left, &top, &right, &bottom);
	fOwnLayout->SetInsets(left, top, right, bottom);
	float hSpacing, vSpacing;
	fLayout->GetSpacing(&hSpacing, &vSpacing);
	fOwnLayout->SetSpacing(hSpacing, vSpacing);

	// add dummy edit view
	//BView* dummy = new BView("LayoutEditView", B_WILL_DRAW | B_FRAME_EVENTS
	//	| B_FULL_UPDATE_ON_RESIZE);
	//dummy->SetViewColor(B_TRANSPARENT_COLOR);
	BSpaceLayoutItem* dummy = new BSpaceLayoutItem(BSize(0, 0),
		BSize(B_SIZE_UNLIMITED, B_SIZE_UNLIMITED), BSize(0, 0),
		BAlignment(B_ALIGN_CENTER, B_ALIGN_MIDDLE));
	fOwnLayout->AddItem(dummy, fOwnLayout->Left(), fOwnLayout->Top(),
		fOwnLayout->Left(), fOwnLayout->Bottom());

	for (int32 i = 0; i < fLayout->CountItems(); i++) {
		BView* view = fLayout->ItemAt(i)->View();
		if (dynamic_cast<LayoutEditView*>(view) != NULL)
			continue;

		CustomizableView* customizable = dynamic_cast<CustomizableView*>(view);
		if (customizable == NULL)
			continue;
		BString name = customizable->Customizable::ObjectName();
		BReference<Customizable> clone = roster->InstantiateCustomizable(name);

		customizable = dynamic_cast<CustomizableView*>(clone.Get());
		if (customizable == NULL)
			return;
	
		if (customizable->View().Get() != NULL)
			fOwnLayout->AddView(customizable->View());
		else if (customizable->LayoutItem().Get() != NULL)
			fOwnLayout->AddItem(customizable->LayoutItem());
		else
			return;

		fItems.AddItem(customizable);
	}
	LayoutArchive(fOwnLayout).RestoreLayout(&archive);

	fOwnLayout->RemoveItem(dummy);
	delete dummy;
	InvalidateLayout();
}


void
PreviewWindowManager::NotifyLayoutEdited()
{
	BAutolock _(fListLocker);
	for (int32 i = 0; i < fWindowList.CountItems(); i++)
		fWindowList.ItemAt(i)->PostMessage(kMsgLayoutEdited);
}


PreviewWindow*
PreviewWindowManager::AddPreviewWindow(BALMLayout* layout,
	PreviewWindow::prev_size size)
{
	PreviewWindow* window = new PreviewWindow(layout, size, this);
	window->Show();
	NotifyLayoutEdited();
	return window;
}


void
PreviewWindowManager::RemovePreviewWindow(BWindow* window)
{
	BAutolock _(fListLocker);
	fWindowList.RemoveItem(window);
}


void
PreviewWindowManager::_AddPreviewWindow(BWindow* window)
{
	BAutolock _(fListLocker);
	fWindowList.AddItem(window);
}

