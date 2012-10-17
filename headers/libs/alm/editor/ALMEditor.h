/*
 * Copyright 2011, Clemens Zeidler <haiku@clemens-zeidler.de>
 * Distributed under the terms of the MIT License.
 */
#ifndef	ALM_EDITOR_H
#define	ALM_EDITOR_H


#include <Locker.h>
#include <Messenger.h>
#include <Window.h>

#include <ArrayContainer.h>
#include <Customizable.h>


const int32 kMsgLayoutEdited = '&LEd';


class IViewContainer;


namespace BALM {


class BALMLayout;
class EditWindow;
class LayoutEditView;
class OverlapManager;


enum tash_messages {
	kTrashUpdated = '&tra',
	kUnTrashComponent
};


class BALMEditor {
public:
								BALMEditor(BALMLayout* layout);
								~BALMEditor();

			void				StartEdit();
			void				StopEdit();

			BALMLayout*			Layout();
			void				UpdateEditWindow();

			void				SetShowXTabs(bool show);
			void				SetShowYTabs(bool show);
			bool				ShowXTabs();
			bool				ShowYTabs();

			void				SetFreePlacement(bool freePlacement);
			bool				FreePlacement();

			bool				Trash(Customizable* customizable);
			BReference<Customizable>	UnTrash(Customizable* customizable);
			bool				DeleteFromTrash(Customizable* customizable);

			void				GetTrash(
									BArray<BWeakReference<Customizable> >& l);
			
			void				SetTrashWatcher(BMessenger target);

			OverlapManager&		GetOverlapManager();

			BString				ProposeIdentifier(IViewContainer* container);
private:
	class trash_item {
	public:
								trash_item(Customizable* customizable);
		Customizable*					raw_pointer;
		BReference<Customizable>		trash;
		BWeakReference<Customizable>	weak_trash;		
	};

			BALMLayout*			fLayout;
			
			LayoutEditView*		fEditView;
			EditWindow*			fEditWindow;
			BMessenger			fEditWindowMessenger;

			BWindow*			fLayerWindow;
			BMessenger			fLayerWindowMessenger;

			BLocker				fLock;

			bool				fShowXTabs;
			bool				fShowYTabs;
			bool				fFreePlacement;

			BObjectList<trash_item>	fTrash;
			BMessenger			fTrashWatcher;

			OverlapManager*		fOverlapManager;
};


}	// namespace BALM


using BALM::BALMEditor;


#endif // ALM_EDITOR_H
