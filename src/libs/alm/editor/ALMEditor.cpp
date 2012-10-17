/*
 * Copyright 2011, Clemens Zeidler <haiku@clemens-zeidler.de>
 * Distributed under the terms of the MIT License.
 */


#include "editor/ALMEditor.h"

#include <Autolock.h>
#include <Window.h>

#include <ALMLayout.h>

#include <CustomizableNodeFactory.h>

#include "CustomizableNodeView.h"
#include "EditorWindow.h"
#include "LayoutArchive.h"
#include "LayoutEditView.h"
#include "OverlapManager.h"


class LayerWindow : public BWindow
{
public:
	LayerWindow(BALMEditor* editor, BALM::LayoutEditView* editView)
		:
		BWindow(BRect(350, 50, 1200, 700), "Layers", B_TITLED_WINDOW, 0),

		fEditor(editor)
	{
		BALMLayout* layout = new BALMLayout(10.);
		SetLayout(layout);
		
		CustomizableNodeView* nodeView = new CustomizableNodeView(
			CustomizableRoster::DefaultRoster());
		layout->AddView(nodeView, layout->Left(), layout->Top(), layout->Right(),
			layout->Bottom());
		
		editView->StartWatching(nodeView, kCustomizableSelected);
		nodeView->StartWatching(editView, kCustomizableSelected);
	}

	bool QuitRequested()
	{
		fEditor->StopEdit();
		return true;
	}

private:
			BALMEditor*			fEditor;
};


BALMEditor::trash_item::trash_item(Customizable* customizable)
{
	raw_pointer = customizable;
	trash = customizable;
}


BALMEditor::BALMEditor(BALMLayout* layout)
	:
	fLayout(layout),
	fEditView(NULL),
	fLayerWindow(NULL),
	fShowXTabs(false),
	fShowYTabs(false),
	fFreePlacement(false)
{
	fOverlapManager = new BALM::OverlapManager(layout);
}


BALMEditor::~BALMEditor()
{
	StopEdit();
}


void
BALMEditor::StartEdit()
{
	BAutolock _(fLock);

	fEditView = new LayoutEditView(this);

	LayoutArchive layoutArchive(fLayout);
	layoutArchive.RestoreFromAppFile("last_layout");

	fLayout->AddView(fEditView, fLayout->Left(), fLayout->Top(),
		fLayout->Right(), fLayout->Bottom());

	fEditWindow = new EditWindow(this, fEditView);
	fEditWindow->Show();
	fEditWindowMessenger = BMessenger(NULL, fEditWindow);

	Area* area = fLayout->AreaFor(fEditView);
	area->SetLeftInset(0);
	area->SetTopInset(0);
	area->SetRightInset(0);
	area->SetBottomInset(0);

//	fLayerWindow = new LayerWindow(this, fEditView);
//	fLayerWindowMessenger = BMessenger(NULL, fLayerWindow);
//	fLayerWindow->Show();
}


void
BALMEditor::StopEdit()
{
	BAutolock _(fLock);
	BMessenger(fEditView).SendMessage(LayoutEditView::kQuitMsg);

	fEditWindowMessenger.SendMessage(B_QUIT_REQUESTED);
	fLayerWindowMessenger.SendMessage(B_QUIT_REQUESTED);
}
	

BALMLayout*
BALMEditor::Layout()
{
	return fLayout;
}


void
BALMEditor::UpdateEditWindow()
{
	if (fEditWindow == NULL)
		return;
	fEditWindow->UpdateEditWindow();
}


void
BALMEditor::SetShowXTabs(bool show)
{
	BAutolock _(fLock);
	fShowXTabs = show;

	BMessage message(LayoutEditView::kShowXTabs);
	message.AddBool("show", show);
	BMessenger(fEditView).SendMessage(&message);
}


void
BALMEditor::SetShowYTabs(bool show)
{
	BAutolock _(fLock);
	fShowYTabs = show;

	BMessage message(LayoutEditView::kShowYTabs);
	message.AddBool("show", show);
	BMessenger(fEditView).SendMessage(&message);
}


void
BALMEditor::SetFreePlacement(bool freePlacement)
{
	fFreePlacement = freePlacement;
}


bool
BALMEditor::FreePlacement()
{
	return fFreePlacement;
}


bool
BALMEditor::ShowXTabs()
{
	return fShowXTabs;
}


bool
BALMEditor::ShowYTabs()
{
	return fShowYTabs;
}


bool
BALMEditor::Trash(Customizable* customizable)
{
	BAutolock _(fLock);
	trash_item* item = new trash_item(customizable);
	if (item == NULL)
		return false;
	fTrash.AddItem(item);
	fTrashWatcher.SendMessage(kTrashUpdated);
	return true;
}


BReference<Customizable>
BALMEditor::UnTrash(Customizable* customizable)
{
	BAutolock _(fLock);
	int32 count = fTrash.CountItems();
	for (int32 i = 0; i < count; i++) {
		trash_item* item = fTrash.ItemAt(i);
		BReference<Customizable> ref = item->trash;
		if (ref.Get() == customizable) {
			fTrash.RemoveItemAt(i);	
			delete item;
			fTrashWatcher.SendMessage(kTrashUpdated);
			return ref;
		}
		ref = item->weak_trash.GetReference();
		if (ref.Get() == NULL)
			continue;
		fTrash.RemoveItemAt(i);	
		delete item;
		fTrashWatcher.SendMessage(kTrashUpdated);
		return ref;
	}
	return NULL;
}


bool
BALMEditor::DeleteFromTrash(Customizable* customizable)
{
	BAutolock _(fLock);
	int32 count = fTrash.CountItems();
	for (int32 i = 0; i < count; i++) {
		trash_item* item = fTrash.ItemAt(i);
		if (item->trash.Get() == customizable) {
			item->weak_trash = item->trash;
			item->trash = NULL;
			if (!item->weak_trash.IsAlive()) {
				fTrash.RemoveItemAt(i);
				delete item;
				fTrashWatcher.SendMessage(kTrashUpdated);
				return true;
			}
			fTrashWatcher.SendMessage(kTrashUpdated);
			return true;
		}
		BReference<Customizable> ref = item->weak_trash.GetReference();
		if (ref.Get() != customizable)
			continue;
		fTrash.RemoveItemAt(i);
		delete item;
		fTrashWatcher.SendMessage(kTrashUpdated);
		return true;
	}
	return false;
}


void
BALMEditor::GetTrash(BArray<BWeakReference<Customizable> >& list)
{
	BAutolock _(fLock);
	int32 count = fTrash.CountItems();
	for (int32 i = 0; i < count; i++) {
		trash_item* item = fTrash.ItemAt(i);
		BWeakReference<Customizable> ref;
		if (item->trash.Get() == NULL)
			ref = item->weak_trash;
		else
			ref = item->trash;

		list.AddItem(ref);
	}
}

			
void
BALMEditor::SetTrashWatcher(BMessenger target)
{
	BAutolock _(fLock);
	fTrashWatcher = target;
}


BALM::OverlapManager&
BALMEditor::GetOverlapManager()
{
	return *fOverlapManager;
}


BString
BALMEditor::ProposeIdentifier(IViewContainer* container)
{
	BString objectName = container->ObjectName();
	BString identifier;

	// TODO: make that more efficient
	int32 id = 0;
	while (true) {
		id++;
		identifier = objectName;
		identifier << id;

		bool identifierTaken = false;
		for (int32 i = 0; i < fLayout->CountAreas(); i++) {
			Area* area = fLayout->AreaAt(i);

			BView* view = area->Item()->View();
			CustomizableView* customizable = dynamic_cast<CustomizableView*>(view);
			if (customizable == NULL) {
				BLayoutItem* item = area->Item();
				customizable = dynamic_cast<CustomizableView*>(item);
				if (customizable == NULL)
					continue;
			}
	
			if (identifier == customizable->Identifier()) {
				identifierTaken = true;
				break;
			}
		}
		if (!identifierTaken)
			break;
	}
	return identifier;
}
