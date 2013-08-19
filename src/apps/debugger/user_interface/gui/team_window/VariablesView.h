/*
 * Copyright 2009, Ingo Weinhold, ingo_weinhold@gmx.de.
 * Copyright 2012, Rene Gollent, rene@gollent.com.
 * Distributed under the terms of the MIT License.
 */
#ifndef VARIABLES_VIEW_H
#define VARIABLES_VIEW_H


#include <GroupView.h>

#include "table/TreeTable.h"


class ActionMenuItem;
class CpuState;
class SettingsMenu;
class StackFrame;
class Thread;
class Type;
class TypeComponentPath;
class ValueNode;
class ValueNodeContainer;
class Variable;
class VariablesViewState;
class VariablesViewStateHistory;


class VariablesView : public BGroupView, private TreeTableListener {
public:
	class Listener;

public:
								VariablesView(Listener* listener);
								~VariablesView();

	static	VariablesView*		Create(Listener* listener);
									// throws

			void				SetStackFrame(Thread* thread,
									StackFrame* stackFrame);

	virtual	void				MessageReceived(BMessage* message);

	virtual	void				DetachedFromWindow();

			void				LoadSettings(const BMessage& settings);
			status_t			SaveSettings(BMessage& settings);

private:
	// TreeTableListener
	virtual	void				TreeTableNodeExpandedChanged(TreeTable* table,
									const TreeTablePath& path, bool expanded);

	virtual	void				TreeTableCellMouseDown(TreeTable* table,
									const TreeTablePath& path,
									int32 columnIndex, BPoint screenWhere,
									uint32 buttons);

private:
			class ContainerListener;
			class ModelNode;
			class VariableValueColumn;
			class VariableTableModel;
			class ContextMenu;
			class TableCellContextMenuTracker;
			typedef BObjectList<ActionMenuItem> ContextActionList;

private:
			void				_Init();

			void				_RequestNodeValue(ModelNode* node);
			status_t			_GetContextActionsForNode(ModelNode* node,
									ContextActionList* actions);
			status_t			_AddContextAction(const char* action,
									uint32 what, ContextActionList* actions,
									BMessage*& _message);
			void				_FinishContextMenu(bool force);
			void				_SaveViewState() const;
			void				_RestoreViewState();
			status_t			_AddViewStateDescendentNodeInfos(
									VariablesViewState* viewState, void* parent,
									TreeTablePath& path) const;
			status_t			_ApplyViewStateDescendentNodeInfos(
									VariablesViewState* viewState, void* parent,
									TreeTablePath& path);

private:
			Thread*				fThread;
			StackFrame*			fStackFrame;
			TreeTable*			fVariableTable;
			VariableTableModel*	fVariableTableModel;
			ContainerListener*	fContainerListener;
			VariablesViewState*	fPreviousViewState;
			VariablesViewStateHistory* fViewStateHistory;
			TableCellContextMenuTracker* fTableCellContextMenuTracker;
			Listener*			fListener;
};


class VariablesView::Listener {
public:
	virtual						~Listener();

	virtual	void				ValueNodeValueRequested(CpuState* cpuState,
									ValueNodeContainer* container,
									ValueNode* valueNode) = 0;
};


#endif	// VARIABLES_VIEW_H
