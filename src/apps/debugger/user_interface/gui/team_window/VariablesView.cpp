/*
 * Copyright 2009, Ingo Weinhold, ingo_weinhold@gmx.de.
 * Copyright 2011-2013, Rene Gollent, rene@gollent.com.
 * Distributed under the terms of the MIT License.
 */


#include "VariablesView.h"

#include <stdio.h>

#include <new>

#include <debugger.h>

#include <Alert.h>
#include <Looper.h>
#include <PopUpMenu.h>
#include <ToolTip.h>

#include <AutoDeleter.h>
#include <AutoLocker.h>
#include <PromptWindow.h>

#include "table/TableColumns.h"

#include "ActionMenuItem.h"
#include "Architecture.h"
#include "FileSourceCode.h"
#include "Function.h"
#include "FunctionID.h"
#include "FunctionInstance.h"
#include "GuiSettingsUtils.h"
#include "MessageCodes.h"
#include "RangeList.h"
#include "Register.h"
#include "SettingsMenu.h"
#include "SourceLanguage.h"
#include "StackTrace.h"
#include "StackFrame.h"
#include "StackFrameValues.h"
#include "TableCellValueRenderer.h"
#include "Team.h"
#include "TeamDebugInfo.h"
#include "Thread.h"
#include "Tracing.h"
#include "TypeComponentPath.h"
#include "TypeHandlerRoster.h"
#include "TypeLookupConstraints.h"
#include "UiUtils.h"
#include "Value.h"
#include "ValueHandler.h"
#include "ValueHandlerRoster.h"
#include "ValueLocation.h"
#include "ValueNode.h"
#include "ValueNodeManager.h"
#include "Variable.h"
#include "VariableValueNodeChild.h"
#include "VariablesViewState.h"
#include "VariablesViewStateHistory.h"


enum {
	VALUE_NODE_TYPE	= 'valn'
};


enum {
	MSG_MODEL_NODE_HIDDEN			= 'monh',
	MSG_VALUE_NODE_NEEDS_VALUE		= 'mvnv',
	MSG_RESTORE_PARTIAL_VIEW_STATE	= 'mpvs'
};


// maximum number of array elements to show by default
static const uint64 kMaxArrayElementCount = 10;


class VariablesView::ContainerListener : public ValueNodeContainer::Listener {
public:
								ContainerListener(BHandler* indirectTarget);

			void				SetModel(VariableTableModel* model);

	virtual	void				ValueNodeChanged(ValueNodeChild* nodeChild,
									ValueNode* oldNode, ValueNode* newNode);
	virtual	void				ValueNodeChildrenCreated(ValueNode* node);
	virtual	void				ValueNodeChildrenDeleted(ValueNode* node);
	virtual	void				ValueNodeValueChanged(ValueNode* node);

	virtual void				ModelNodeHidden(ModelNode* node);

	virtual void				ModelNodeValueRequested(ModelNode* node);

	virtual void				ModelNodeRestoreViewStateRequested(ModelNode* node);

private:
			BHandler*			fIndirectTarget;
			VariableTableModel*	fModel;
};


class VariablesView::ModelNode : public BReferenceable {
public:
	ModelNode(ModelNode* parent, Variable* variable, ValueNodeChild* nodeChild,
		bool isPresentationNode)
		:
		fParent(parent),
		fNodeChild(nodeChild),
		fVariable(variable),
		fValue(NULL),
		fValueHandler(NULL),
		fTableCellRenderer(NULL),
		fLastRendererSettings(),
		fCastedType(NULL),
		fComponentPath(NULL),
		fIsPresentationNode(isPresentationNode),
		fHidden(false)
	{
		fNodeChild->AcquireReference();
	}

	~ModelNode()
	{
		SetTableCellRenderer(NULL);
		SetValueHandler(NULL);
		SetValue(NULL);

		for (int32 i = 0; ModelNode* child = fChildren.ItemAt(i); i++)
			child->ReleaseReference();

		fNodeChild->ReleaseReference();

		if (fComponentPath != NULL)
			fComponentPath->ReleaseReference();

		if (fCastedType != NULL)
			fCastedType->ReleaseReference();
	}

	status_t Init()
	{
		fComponentPath = new(std::nothrow) TypeComponentPath();
		if (fComponentPath == NULL)
			return B_NO_MEMORY;

		if (fParent != NULL)
			*fComponentPath = *fParent->GetPath();

		TypeComponent component;
		// TODO: this should actually discriminate between different
		// classes of type component kinds
		component.SetToBaseType(fNodeChild->GetType()->Kind(),
			0, fNodeChild->Name());

		fComponentPath->AddComponent(component);

		return B_OK;
	}

	ModelNode* Parent() const
	{
		return fParent;
	}

	ValueNodeChild* NodeChild() const
	{
		return fNodeChild;
	}

	const BString& Name() const
	{
		return fNodeChild->Name();
	}

	Type* GetType() const
	{
		if (fCastedType != NULL)
			return fCastedType;

		return fNodeChild->GetType();
	}

	Variable* GetVariable() const
	{
		return fVariable;
	}

	Value* GetValue() const
	{
		return fValue;
	}

	void SetValue(Value* value)
	{
		if (value == fValue)
			return;

		if (fValue != NULL)
			fValue->ReleaseReference();

		fValue = value;

		if (fValue != NULL)
			fValue->AcquireReference();
	}

	Type* GetCastedType() const
	{
		return fCastedType;
	}

	void SetCastedType(Type* type)
	{
		if (fCastedType != NULL)
			fCastedType->ReleaseReference();

		fCastedType = type;
		if (type != NULL)
			fCastedType->AcquireReference();
	}

	const BMessage& GetLastRendererSettings() const
	{
		return fLastRendererSettings;
	}

	void SetLastRendererSettings(const BMessage& settings)
	{
		fLastRendererSettings = settings;
	}

	TypeComponentPath* GetPath() const
	{
		return fComponentPath;
	}

	ValueHandler* GetValueHandler() const
	{
		return fValueHandler;
	}

	void SetValueHandler(ValueHandler* handler)
	{
		if (handler == fValueHandler)
			return;

		if (fValueHandler != NULL)
			fValueHandler->ReleaseReference();

		fValueHandler = handler;

		if (fValueHandler != NULL)
			fValueHandler->AcquireReference();
	}


	TableCellValueRenderer* TableCellRenderer() const
	{
		return fTableCellRenderer;
	}

	void SetTableCellRenderer(TableCellValueRenderer* renderer)
	{
		if (renderer == fTableCellRenderer)
			return;

		if (fTableCellRenderer != NULL)
			fTableCellRenderer->ReleaseReference();

		fTableCellRenderer = renderer;

		if (fTableCellRenderer != NULL)
			fTableCellRenderer->AcquireReference();
	}

	bool IsPresentationNode() const
	{
		return fIsPresentationNode;
	}

	bool IsHidden() const
	{
		return fHidden;
	}

	void SetHidden(bool hidden)
	{
		fHidden = hidden;
	}

	int32 CountChildren() const
	{
		return fChildren.CountItems();
	}

	ModelNode* ChildAt(int32 index) const
	{
		return fChildren.ItemAt(index);
	}

	int32 IndexOf(ModelNode* child) const
	{
		return fChildren.IndexOf(child);
	}

	bool AddChild(ModelNode* child)
	{
		if (!fChildren.AddItem(child))
			return false;

		child->AcquireReference();
		return true;
	}

	bool RemoveChild(ModelNode* child)
	{
		if (!fChildren.RemoveItem(child))
			return false;

		child->ReleaseReference();
		return true;
	}

	bool RemoveAllChildren()
	{
		for (int32 i = 0; i < fChildren.CountItems(); i++)
			RemoveChild(fChildren.ItemAt(i));

		return true;
	}

private:
	typedef BObjectList<ModelNode> ChildList;

private:
	ModelNode*				fParent;
	ValueNodeChild*			fNodeChild;
	Variable*				fVariable;
	Value*					fValue;
	ValueHandler*			fValueHandler;
	TableCellValueRenderer*	fTableCellRenderer;
	BMessage				fLastRendererSettings;
	Type*					fCastedType;
	ChildList				fChildren;
	TypeComponentPath*		fComponentPath;
	bool					fIsPresentationNode;
	bool					fHidden;

public:
	ModelNode*			fNext;
};


// #pragma mark - VariableValueColumn


class VariablesView::VariableValueColumn : public StringTableColumn {
public:
	VariableValueColumn(int32 modelIndex, const char* title, float width,
		float minWidth, float maxWidth, uint32 truncate = B_TRUNCATE_MIDDLE,
		alignment align = B_ALIGN_RIGHT)
		:
		StringTableColumn(modelIndex, title, width, minWidth, maxWidth,
			truncate, align)
	{
	}

protected:
	void DrawValue(const BVariant& value, BRect rect, BView* targetView)
	{
		// draw the node's value with the designated renderer
		if (value.Type() == VALUE_NODE_TYPE) {
			ModelNode* node = dynamic_cast<ModelNode*>(value.ToReferenceable());
			if (node != NULL && node->GetValue() != NULL
				&& node->TableCellRenderer() != NULL) {
				node->TableCellRenderer()->RenderValue(node->GetValue(), rect,
					targetView);
				return;
			}
		} else if (value.Type() == B_STRING_TYPE) {
			fField.SetString(value.ToString());
		} else {
			// fall back to drawing an empty string
			fField.SetString("");
		}
		fField.SetWidth(Width());
		fColumn.DrawField(&fField, rect, targetView);
	}

	float GetPreferredWidth(const BVariant& value, BView* targetView) const
	{
		// get the preferred width from the node's designated renderer
		if (value.Type() == VALUE_NODE_TYPE) {
			ModelNode* node = dynamic_cast<ModelNode*>(value.ToReferenceable());
			if (node != NULL && node->GetValue() != NULL
				&& node->TableCellRenderer() != NULL) {
				return node->TableCellRenderer()->PreferredValueWidth(
					node->GetValue(), targetView);
			}
		}

		return fColumn.BTitledColumn::GetPreferredWidth(NULL, targetView);
	}

	virtual BField* PrepareField(const BVariant& _value) const
	{
		return NULL;
	}
};


// #pragma mark - VariableTableModel


class VariablesView::VariableTableModel : public TreeTableModel,
	public TreeTableToolTipProvider {
public:
								VariableTableModel();
								~VariableTableModel();

			status_t			Init();

			void				SetContainerListener(
									ContainerListener* listener);

			void				SetStackFrame(Thread* thread,
									StackFrame* stackFrame);

			void				ValueNodeChanged(ValueNodeChild* nodeChild,
									ValueNode* oldNode, ValueNode* newNode);
			void				ValueNodeChildrenCreated(ValueNode* node);
			void				ValueNodeChildrenDeleted(ValueNode* node);
			void				ValueNodeValueChanged(ValueNode* node);

	virtual	int32				CountColumns() const;
	virtual	void*				Root() const;
	virtual	int32				CountChildren(void* parent) const;
	virtual	void*				ChildAt(void* parent, int32 index) const;
	virtual	bool				GetValueAt(void* object, int32 columnIndex,
									BVariant& _value);

			bool				GetTreePath(ModelNode* node,
									TreeTablePath& _path) const;

			void				NodeExpanded(ModelNode* node);

			void				NotifyNodeChanged(ModelNode* node);
			void				NotifyNodeHidden(ModelNode* node);

	virtual	bool				GetToolTipForTablePath(
									const TreeTablePath& path,
									int32 columnIndex, BToolTip** _tip);

private:
			struct NodeHashDefinition {
				typedef ValueNodeChild*	KeyType;
				typedef	ModelNode		ValueType;

				size_t HashKey(const ValueNodeChild* key) const
				{
					return (size_t)key;
				}

				size_t Hash(const ModelNode* value) const
				{
					return HashKey(value->NodeChild());
				}

				bool Compare(const ValueNodeChild* key,
					const ModelNode* value) const
				{
					return value->NodeChild() == key;
				}

				ModelNode*& GetLink(ModelNode* value) const
				{
					return value->fNext;
				}
			};

			typedef BObjectList<ModelNode> NodeList;
			typedef BOpenHashTable<NodeHashDefinition> NodeTable;

private:
			// container must be locked

			status_t			_AddNode(Variable* variable, ModelNode* parent,
									ValueNodeChild* nodeChild,
									bool isPresentationNode = false,
									bool isOnlyChild = false);

private:
			Thread*				fThread;
			ValueNodeManager*	fNodeManager;
			ContainerListener*	fContainerListener;
			NodeList			fNodes;
			NodeTable			fNodeTable;
};


class VariablesView::ContextMenu : public BPopUpMenu {
public:
	ContextMenu(const BMessenger& parent, const char* name)
		: BPopUpMenu(name, false, false),
		  fParent(parent)
	{
	}

	virtual void Hide()
	{
		BPopUpMenu::Hide();

		BMessage message(MSG_VARIABLES_VIEW_CONTEXT_MENU_DONE);
		message.AddPointer("menu", this);
		fParent.SendMessage(&message);
	}

private:
	BMessenger	fParent;
};


// #pragma mark - TableCellContextMenuTracker


class VariablesView::TableCellContextMenuTracker : public BReferenceable,
	Settings::Listener {
public:
	TableCellContextMenuTracker(ModelNode* node, BLooper* parentLooper,
		const BMessenger& parent)
		:
		fNode(node),
		fParentLooper(parentLooper),
		fParent(parent),
		fRendererSettings(NULL),
		fRendererSettingsMenu(NULL),
		fRendererMenuAdded(false),
		fMenuPreparedToShow(false)
	{
		fNode->AcquireReference();
	}

	~TableCellContextMenuTracker()
	{
		FinishMenu(true);

		if (fRendererSettingsMenu != NULL)
			fRendererSettingsMenu->ReleaseReference();

		if (fRendererSettings != NULL)
			fRendererSettings->ReleaseReference();

		fNode->ReleaseReference();
	}

	status_t Init(Settings* rendererSettings,
		SettingsMenu* rendererSettingsMenu,
		ContextActionList* preSettingsActions = NULL,
		ContextActionList* postSettingsActions = NULL)
	{
		if (rendererSettings == NULL && preSettingsActions == NULL
			&& postSettingsActions == NULL) {
			return B_BAD_VALUE;
		}

		if (rendererSettings != NULL) {
			fRendererSettings = rendererSettings;
			fRendererSettings->AcquireReference();


			fRendererSettingsMenu = rendererSettingsMenu;
			fRendererSettingsMenu->AcquireReference();
		}

		fContextMenu = new(std::nothrow) ContextMenu(fParent,
			"table cell settings popup");
		if (fContextMenu == NULL)
			return B_NO_MEMORY;

		status_t error = B_OK;
		if (preSettingsActions != NULL
			&& preSettingsActions->CountItems() > 0) {
			error = _AddActionItems(preSettingsActions);
			if (error != B_OK)
				return error;

			if (fRendererSettingsMenu != NULL || postSettingsActions != NULL)
				fContextMenu->AddSeparatorItem();
		}

		if (fRendererSettingsMenu != NULL) {
			error = fRendererSettingsMenu->AddToMenu(fContextMenu,
				fContextMenu->CountItems());
			if (error != B_OK)
				return error;

			if (postSettingsActions != NULL)
				fContextMenu->AddSeparatorItem();
		}

		if (postSettingsActions != NULL) {
			error = _AddActionItems(postSettingsActions);
			if (error != B_OK)
				return error;

		}

		if (fRendererSettings != NULL) {
			AutoLocker<Settings> settingsLocker(fRendererSettings);
			fRendererSettings->AddListener(this);
			fRendererMenuAdded = true;
		}

		return B_OK;
	}

	void ShowMenu(BPoint screenWhere)
	{
		if (fRendererMenuAdded)
			fRendererSettingsMenu->PrepareToShow(fParentLooper);

		for (int32 i = 0; i < fContextMenu->CountItems(); i++) {
			ActionMenuItem* item = dynamic_cast<ActionMenuItem*>(
				fContextMenu->ItemAt(i));
			if (item != NULL)
				item->PrepareToShow(fParentLooper, fParent.Target(NULL));
		}

		fMenuPreparedToShow = true;

		BRect mouseRect(screenWhere, screenWhere);
		mouseRect.InsetBy(-4.0, -4.0);
		fContextMenu->Go(screenWhere, true, false, mouseRect, true);
	}

	bool FinishMenu(bool force)
	{
		bool stillActive = false;

		if (fMenuPreparedToShow) {
			if (fRendererMenuAdded)
				stillActive = fRendererSettingsMenu->Finish(fParentLooper,
					force);
			for (int32 i = 0; i < fContextMenu->CountItems(); i++) {
				ActionMenuItem* item = dynamic_cast<ActionMenuItem*>(
					fContextMenu->ItemAt(i));
				if (item != NULL) {
					stillActive |= item->Finish(fParentLooper,
						fParent.Target(NULL), force);
				}
			}

			fMenuPreparedToShow = stillActive;
		}

		if (fRendererMenuAdded) {
			fRendererSettingsMenu->RemoveFromMenu();
			fRendererSettings->RemoveListener(this);
			fRendererMenuAdded = false;
		}

		if (fContextMenu != NULL) {
			delete fContextMenu;
			fContextMenu = NULL;
		}

		return stillActive;
	}

private:
	// Settings::Listener

	virtual void SettingValueChanged(Setting* setting)
	{
		BMessage message(MSG_VARIABLES_VIEW_NODE_SETTINGS_CHANGED);
		fNode->AcquireReference();
		if (message.AddPointer("node", fNode) != B_OK
			|| fParent.SendMessage(&message) != B_OK) {
			fNode->ReleaseReference();
		}
	}

	status_t _AddActionItems(ContextActionList* actions)
	{
		if (fContextMenu == NULL)
			return B_BAD_VALUE;

		int32 index = fContextMenu->CountItems();
		for (int32 i = 0; ActionMenuItem* item = actions->ItemAt(i); i++) {
			if (!fContextMenu->AddItem(item, index + i)) {
				for (i--; i >= 0; i--)
					fContextMenu->RemoveItem(fContextMenu->ItemAt(index + i));

				return B_NO_MEMORY;
			}
		}

		return B_OK;
	}

private:
	ModelNode*		fNode;
	BLooper*		fParentLooper;
	BMessenger		fParent;
	ContextMenu*	fContextMenu;
	Settings*		fRendererSettings;
	SettingsMenu*	fRendererSettingsMenu;
	bool			fRendererMenuAdded;
	bool			fMenuPreparedToShow;
};


// #pragma mark - ContainerListener


VariablesView::ContainerListener::ContainerListener(BHandler* indirectTarget)
	:
	fIndirectTarget(indirectTarget),
	fModel(NULL)
{
}


void
VariablesView::ContainerListener::SetModel(VariableTableModel* model)
{
	fModel = model;
}


void
VariablesView::ContainerListener::ValueNodeChanged(ValueNodeChild* nodeChild,
	ValueNode* oldNode, ValueNode* newNode)
{
	// If the looper is already locked, invoke the model's hook synchronously.
	if (fIndirectTarget->Looper()->IsLocked()) {
		fModel->ValueNodeChanged(nodeChild, oldNode, newNode);
		return;
	}

	// looper not locked yet -- call asynchronously to avoid reverse locking
	// order
	BReference<ValueNodeChild> nodeChildReference(nodeChild);
	BReference<ValueNode> oldNodeReference(oldNode);
	BReference<ValueNode> newNodeReference(newNode);

	BMessage message(MSG_VALUE_NODE_CHANGED);
	if (message.AddPointer("nodeChild", nodeChild) == B_OK
		&& message.AddPointer("oldNode", oldNode) == B_OK
		&& message.AddPointer("newNode", newNode) == B_OK
		&& fIndirectTarget->Looper()->PostMessage(&message, fIndirectTarget)
			== B_OK) {
		nodeChildReference.Detach();
		oldNodeReference.Detach();
		newNodeReference.Detach();
	}
}


void
VariablesView::ContainerListener::ValueNodeChildrenCreated(ValueNode* node)
{
	// If the looper is already locked, invoke the model's hook synchronously.
	if (fIndirectTarget->Looper()->IsLocked()) {
		fModel->ValueNodeChildrenCreated(node);
		return;
	}

	// looper not locked yet -- call asynchronously to avoid reverse locking
	// order
	BReference<ValueNode> nodeReference(node);

	BMessage message(MSG_VALUE_NODE_CHILDREN_CREATED);
	if (message.AddPointer("node", node) == B_OK
		&& fIndirectTarget->Looper()->PostMessage(&message, fIndirectTarget)
			== B_OK) {
		nodeReference.Detach();
	}
}


void
VariablesView::ContainerListener::ValueNodeChildrenDeleted(ValueNode* node)
{
	// If the looper is already locked, invoke the model's hook synchronously.
	if (fIndirectTarget->Looper()->IsLocked()) {
		fModel->ValueNodeChildrenDeleted(node);
		return;
	}

	// looper not locked yet -- call asynchronously to avoid reverse locking
	// order
	BReference<ValueNode> nodeReference(node);

	BMessage message(MSG_VALUE_NODE_CHILDREN_DELETED);
	if (message.AddPointer("node", node) == B_OK
		&& fIndirectTarget->Looper()->PostMessage(&message, fIndirectTarget)
			== B_OK) {
		nodeReference.Detach();
	}
}


void
VariablesView::ContainerListener::ValueNodeValueChanged(ValueNode* node)
{
	// If the looper is already locked, invoke the model's hook synchronously.
	if (fIndirectTarget->Looper()->IsLocked()) {
		fModel->ValueNodeValueChanged(node);
		return;
	}

	// looper not locked yet -- call asynchronously to avoid reverse locking
	// order
	BReference<ValueNode> nodeReference(node);

	BMessage message(MSG_VALUE_NODE_VALUE_CHANGED);
	if (message.AddPointer("node", node) == B_OK
		&& fIndirectTarget->Looper()->PostMessage(&message, fIndirectTarget)
			== B_OK) {
		nodeReference.Detach();
	}
}


void
VariablesView::ContainerListener::ModelNodeHidden(ModelNode* node)
{
	BReference<ModelNode> nodeReference(node);

	BMessage message(MSG_MODEL_NODE_HIDDEN);
	if (message.AddPointer("node", node) == B_OK
		&& fIndirectTarget->Looper()->PostMessage(&message, fIndirectTarget)
			== B_OK) {
		nodeReference.Detach();
	}
}


void
VariablesView::ContainerListener::ModelNodeValueRequested(ModelNode* node)
{
	BReference<ModelNode> nodeReference(node);

	BMessage message(MSG_VALUE_NODE_NEEDS_VALUE);
	if (message.AddPointer("node", node) == B_OK
		&& fIndirectTarget->Looper()->PostMessage(&message, fIndirectTarget)
			== B_OK) {
		nodeReference.Detach();
	}
}


void
VariablesView::ContainerListener::ModelNodeRestoreViewStateRequested(
	ModelNode* node)
{
	BReference<ModelNode> nodeReference(node);

	BMessage message(MSG_RESTORE_PARTIAL_VIEW_STATE);
	if (message.AddPointer("node", node) == B_OK
		&& fIndirectTarget->Looper()->PostMessage(&message, fIndirectTarget)
			== B_OK) {
		nodeReference.Detach();
	}
}


// #pragma mark - VariableTableModel


VariablesView::VariableTableModel::VariableTableModel()
	:
	fThread(NULL),
	fNodeManager(NULL),
	fContainerListener(NULL),
	fNodeTable()
{
}


VariablesView::VariableTableModel::~VariableTableModel()
{
	if (fNodeManager != NULL)
		fNodeManager->ReleaseReference();
}


status_t
VariablesView::VariableTableModel::Init()
{
	fNodeManager = new(std::nothrow) ValueNodeManager();
	if (fNodeManager == NULL)
		return B_NO_MEMORY;

	return fNodeTable.Init();
}


void
VariablesView::VariableTableModel::SetContainerListener(
	ContainerListener* listener)
{
	if (listener == fContainerListener)
		return;

	if (fContainerListener != NULL) {
		if (fNodeManager != NULL)
			fNodeManager->RemoveListener(fContainerListener);

		fContainerListener->SetModel(NULL);
	}

	fContainerListener = listener;

	if (fContainerListener != NULL) {
		fContainerListener->SetModel(this);

		if (fNodeManager != NULL)
			fNodeManager->AddListener(fContainerListener);
	}
}


void
VariablesView::VariableTableModel::SetStackFrame(Thread* thread,
	StackFrame* stackFrame)
{
	fThread = thread;

	fNodeManager->SetStackFrame(thread, stackFrame);

	int32 count = fNodes.CountItems();
	fNodeTable.Clear(true);

	if (!fNodes.IsEmpty()) {
		for (int32 i = 0; i < count; i++)
			fNodes.ItemAt(i)->ReleaseReference();
		fNodes.MakeEmpty();
	}

	if (stackFrame == NULL) {
		NotifyNodesRemoved(TreeTablePath(), 0, count);
		return;
	}

	ValueNodeContainer* container = fNodeManager->GetContainer();
	AutoLocker<ValueNodeContainer> containerLocker(container);

	for (int32 i = 0; i < container->CountChildren(); i++) {
		VariableValueNodeChild* child = dynamic_cast<VariableValueNodeChild *>(
			container->ChildAt(i));
		_AddNode(child->GetVariable(), NULL, child);
		// top level nodes get their children added immediately
		// so those won't invoke our callback hook. Add them directly here.
		ValueNodeChildrenCreated(child->Node());
	}

	NotifyTableModelReset();
}


void
VariablesView::VariableTableModel::ValueNodeChanged(ValueNodeChild* nodeChild,
	ValueNode* oldNode, ValueNode* newNode)
{
	AutoLocker<ValueNodeContainer> containerLocker(
		fNodeManager->GetContainer());
	ModelNode* modelNode = fNodeTable.Lookup(nodeChild);
	if (modelNode == NULL)
		return;

	if (oldNode != NULL) {
		ValueNodeChildrenDeleted(oldNode);
		NotifyNodeChanged(modelNode);
	}
}


void
VariablesView::VariableTableModel::ValueNodeChildrenCreated(
	ValueNode* valueNode)
{
	AutoLocker<ValueNodeContainer> containerLocker(
		fNodeManager->GetContainer());

	// check whether we know the node
	ValueNodeChild* nodeChild = valueNode->NodeChild();
	if (nodeChild == NULL)
		return;

	ModelNode* modelNode = fNodeTable.Lookup(nodeChild);
	if (modelNode == NULL)
		return;

	// Iterate through the children and create model nodes for the ones we
	// don't know yet.
	int32 childCount = valueNode->CountChildren();
	for (int32 i = 0; i < childCount; i++) {
		ValueNodeChild* child = valueNode->ChildAt(i);
		if (fNodeTable.Lookup(child) == NULL) {
			_AddNode(modelNode->GetVariable(), modelNode, child,
				child->IsInternal(), childCount == 1);
		}

		ModelNode* childNode = fNodeTable.Lookup(child);
		if (childNode != NULL)
			fContainerListener->ModelNodeValueRequested(childNode);
	}

	if (valueNode->ChildCreationNeedsValue())
		fContainerListener->ModelNodeRestoreViewStateRequested(modelNode);
}


void
VariablesView::VariableTableModel::ValueNodeChildrenDeleted(ValueNode* node)
{
	AutoLocker<ValueNodeContainer> containerLocker(
		fNodeManager->GetContainer());

	// check whether we know the node
	ValueNodeChild* nodeChild = node->NodeChild();
	if (nodeChild == NULL)
		return;

	ModelNode* modelNode = fNodeTable.Lookup(nodeChild);
	if (modelNode == NULL)
		return;

	// in the case of an address node with a hidden child,
	// we want to send removal notifications for the children
	// instead.
	BReference<ModelNode> hiddenChild;
	if (modelNode->CountChildren() == 1
		&& modelNode->ChildAt(0)->IsHidden()) {
		hiddenChild.SetTo(modelNode->ChildAt(0));
		modelNode->RemoveChild(hiddenChild);
		modelNode = hiddenChild;
		fNodeTable.Remove(hiddenChild);
	}

	for (int32 i = modelNode->CountChildren() - 1; i >= 0 ; i--) {
		BReference<ModelNode> childNode = modelNode->ChildAt(i);
		// recursively remove the current node's child hierarchy.
		if (childNode->CountChildren() != 0)
			ValueNodeChildrenDeleted(childNode->NodeChild()->Node());

		TreeTablePath treePath;
		if (GetTreePath(childNode, treePath)) {
			int32 index = treePath.RemoveLastComponent();
			NotifyNodesRemoved(treePath, index, 1);
		}
		modelNode->RemoveChild(childNode);
		fNodeTable.Remove(childNode);
	}
}


void
VariablesView::VariableTableModel::ValueNodeValueChanged(ValueNode* valueNode)
{
	AutoLocker<ValueNodeContainer> containerLocker(
		fNodeManager->GetContainer());

	// check whether we know the node
	ValueNodeChild* nodeChild = valueNode->NodeChild();
	if (nodeChild == NULL)
		return;

	ModelNode* modelNode = fNodeTable.Lookup(nodeChild);
	if (modelNode == NULL)
		return;

	// check whether the value actually changed
	Value* value = valueNode->GetValue();
	if (value == modelNode->GetValue())
		return;

	// get a value handler
	ValueHandler* valueHandler;
	status_t error = ValueHandlerRoster::Default()->FindValueHandler(value,
		valueHandler);
	if (error != B_OK)
		return;
	BReference<ValueHandler> handlerReference(valueHandler, true);

	// create a table cell renderer for the value
	TableCellValueRenderer* renderer = NULL;
	error = valueHandler->GetTableCellValueRenderer(value, renderer);
	if (error != B_OK)
		return;

	// set value/handler/renderer
	modelNode->SetValue(value);
	modelNode->SetValueHandler(valueHandler);
	modelNode->SetTableCellRenderer(renderer);

	// we have to restore renderer settings here since until this point
	// we don't yet know what renderer is in use.
	if (renderer != NULL) {
		Settings* settings = renderer->GetSettings();
		if (settings != NULL)
			settings->RestoreValues(modelNode->GetLastRendererSettings());
	}

	// notify table model listeners
	NotifyNodeChanged(modelNode);
}


int32
VariablesView::VariableTableModel::CountColumns() const
{
	return 3;
}


void*
VariablesView::VariableTableModel::Root() const
{
	return (void*)this;
}


int32
VariablesView::VariableTableModel::CountChildren(void* parent) const
{
	if (parent == this)
		return fNodes.CountItems();

	// If the node only has a hidden child, pretend the node directly has the
	// child's children.
	ModelNode* modelNode = (ModelNode*)parent;
	int32 childCount = modelNode->CountChildren();
	if (childCount == 1) {
		ModelNode* child = modelNode->ChildAt(0);
		if (child->IsHidden())
			return child->CountChildren();
	}

	return childCount;
}


void*
VariablesView::VariableTableModel::ChildAt(void* parent, int32 index) const
{
	if (parent == this)
		return fNodes.ItemAt(index);

	// If the node only has a hidden child, pretend the node directly has the
	// child's children.
	ModelNode* modelNode = (ModelNode*)parent;
	int32 childCount = modelNode->CountChildren();
	if (childCount == 1) {
		ModelNode* child = modelNode->ChildAt(0);
		if (child->IsHidden())
			return child->ChildAt(index);
	}

	return modelNode->ChildAt(index);
}


bool
VariablesView::VariableTableModel::GetValueAt(void* object, int32 columnIndex,
	BVariant& _value)
{
	ModelNode* node = (ModelNode*)object;

	switch (columnIndex) {
		case 0:
			_value.SetTo(node->Name(), B_VARIANT_DONT_COPY_DATA);
			return true;
		case 1:
			if (node->GetValue() == NULL) {
				ValueLocation* location = node->NodeChild()->Location();
				if (location == NULL)
					return false;

				Type* nodeChildRawType = node->NodeChild()->Node()->GetType()
					->ResolveRawType(false);
				if (nodeChildRawType->Kind() == TYPE_COMPOUND)
				{
					if (location->CountPieces() > 1)
						return false;

					BString data;
					ValuePieceLocation piece = location->PieceAt(0);
					if (piece.type != VALUE_PIECE_LOCATION_MEMORY)
						return false;

					data.SetToFormat("[@ %#" B_PRIx64 "]", piece.address);
					_value.SetTo(data);
					return true;
				}
				return false;
			}

			_value.SetTo(node, VALUE_NODE_TYPE);
			return true;
		case 2:
		{
			Type* type = node->GetType();
			if (type == NULL)
				return false;

			_value.SetTo(type->Name(), B_VARIANT_DONT_COPY_DATA);
			return true;
		}
		default:
			return false;
	}
}


void
VariablesView::VariableTableModel::NodeExpanded(ModelNode* node)
{
	AutoLocker<ValueNodeContainer> containerLocker(
		fNodeManager->GetContainer());
	// add children of all children

	// If the node only has a hidden child, add the child's children instead.
	if (node->CountChildren() == 1) {
		ModelNode* child = node->ChildAt(0);
		if (child->IsHidden())
			node = child;
	}

	// add the children
	for (int32 i = 0; ModelNode* child = node->ChildAt(i); i++)
		fNodeManager->AddChildNodes(child->NodeChild());
}


void
VariablesView::VariableTableModel::NotifyNodeChanged(ModelNode* node)
{
	if (!node->IsHidden()) {
		TreeTablePath treePath;
		if (GetTreePath(node, treePath)) {
			int32 index = treePath.RemoveLastComponent();
			NotifyNodesChanged(treePath, index, 1);
		}
	}
}


void
VariablesView::VariableTableModel::NotifyNodeHidden(ModelNode* node)
{
	fContainerListener->ModelNodeHidden(node);
}


bool
VariablesView::VariableTableModel::GetToolTipForTablePath(
	const TreeTablePath& path, int32 columnIndex, BToolTip** _tip)
{
	ModelNode* node = (ModelNode*)NodeForPath(path);
	if (node == NULL)
		return false;

	if (node->NodeChild()->LocationResolutionState() != B_OK)
		return false;

	BString tipData;
	switch (columnIndex) {
		case 0:
		{
			ValueLocation* location = node->NodeChild()->Location();
			for (int32 i = 0; i < location->CountPieces(); i++) {
				ValuePieceLocation piece = location->PieceAt(i);
				BString pieceData;
				switch (piece.type) {
					case VALUE_PIECE_LOCATION_MEMORY:
						pieceData.SetToFormat("(%" B_PRId32 "): Address: %#"
							B_PRIx64 ", Size: %" B_PRId64 " bytes", i,
							piece.address, piece.size);
						break;
					case VALUE_PIECE_LOCATION_REGISTER:
					{
						Architecture* architecture = fThread->GetTeam()
							->GetArchitecture();
						pieceData.SetToFormat("(%" B_PRId32 "): Register (%s)",
							i, architecture->Registers()[piece.reg].Name());
						break;
					}
					default:
						break;
				}

				tipData	+= pieceData;
				if (i < location->CountPieces() - 1)
					tipData += "\n";
			}
			break;
		}
		case 1:
		{
			Value* value = node->GetValue();
			if (value != NULL)
				value->ToString(tipData);

			break;
		}
		default:
			break;
	}

	if (tipData.IsEmpty())
		return false;

	*_tip = new(std::nothrow) BTextToolTip(tipData);
	if (*_tip == NULL)
		return false;

	return true;
}


status_t
VariablesView::VariableTableModel::_AddNode(Variable* variable,
	ModelNode* parent, ValueNodeChild* nodeChild, bool isPresentationNode,
	bool isOnlyChild)
{
	// Don't create nodes for unspecified types -- we can't get/show their
	// value anyway.
	Type* nodeChildRawType = nodeChild->GetType()->ResolveRawType(false);
	if (nodeChildRawType->Kind() == TYPE_UNSPECIFIED)
		return B_OK;

	ModelNode* node = new(std::nothrow) ModelNode(parent, variable, nodeChild,
		isPresentationNode);
	BReference<ModelNode> nodeReference(node, true);
	if (node == NULL || node->Init() != B_OK)
		return B_NO_MEMORY;

	int32 childIndex;

	if (parent != NULL) {
		childIndex = parent->CountChildren();

		if (!parent->AddChild(node))
			return B_NO_MEMORY;
		// the parent has a reference, now
	} else {
		childIndex = fNodes.CountItems();

		if (!fNodes.AddItem(node))
			return B_NO_MEMORY;
		nodeReference.Detach();
			// the fNodes list has a reference, now
	}

	fNodeTable.Insert(node);

	// if an address type node has only a single child, and that child
	// is a compound type, mark it hidden
	if (isOnlyChild && parent != NULL) {
		ValueNode* parentValueNode = parent->NodeChild()->Node();
		if (parentValueNode != NULL) {
			if (parentValueNode->GetType()->ResolveRawType(false)->Kind()
				== TYPE_ADDRESS) {
				type_kind childKind = nodeChildRawType->Kind();
				if (childKind == TYPE_COMPOUND || childKind == TYPE_ARRAY) {
					node->SetHidden(true);

					// we need to tell the listener about nodes like this so
					// any necessary actions can be taken for them (i.e. value
					// resolution), since they're otherwise invisible to
					// outsiders.
					NotifyNodeHidden(node);
				}
			}
		}
	}

	// notify table model listeners
	if (!node->IsHidden()) {
		TreeTablePath path;
		if (parent == NULL || GetTreePath(parent, path))
			NotifyNodesAdded(path, childIndex, 1);
	}

	// if the node is hidden, add its children
	if (node->IsHidden())
		fNodeManager->AddChildNodes(nodeChild);

	return B_OK;
}


bool
VariablesView::VariableTableModel::GetTreePath(ModelNode* node,
	TreeTablePath& _path) const
{
	// recurse, if the node has a parent
	if (ModelNode* parent = node->Parent()) {
		if (!GetTreePath(parent, _path))
			return false;

		if (node->IsHidden())
			return true;

		return _path.AddComponent(parent->IndexOf(node));
	}

	// no parent -- get the index and start the path
	int32 index = fNodes.IndexOf(node);
	_path.Clear();
	return index >= 0 && _path.AddComponent(index);
}


// #pragma mark - VariablesView


VariablesView::VariablesView(Listener* listener)
	:
	BGroupView(B_VERTICAL),
	fThread(NULL),
	fStackFrame(NULL),
	fVariableTable(NULL),
	fVariableTableModel(NULL),
	fContainerListener(NULL),
	fPreviousViewState(NULL),
	fViewStateHistory(NULL),
	fTableCellContextMenuTracker(NULL),
	fListener(listener)
{
	SetName("Variables");
}


VariablesView::~VariablesView()
{
	SetStackFrame(NULL, NULL);
	fVariableTable->SetTreeTableModel(NULL);

	if (fPreviousViewState != NULL)
		fPreviousViewState->ReleaseReference();
	delete fViewStateHistory;

	if (fVariableTableModel != NULL) {
		fVariableTableModel->SetContainerListener(NULL);
		delete fVariableTableModel;
	}

	delete fContainerListener;
}


/*static*/ VariablesView*
VariablesView::Create(Listener* listener)
{
	VariablesView* self = new VariablesView(listener);

	try {
		self->_Init();
	} catch (...) {
		delete self;
		throw;
	}

	return self;
}


void
VariablesView::SetStackFrame(Thread* thread, StackFrame* stackFrame)
{
	if (thread == fThread && stackFrame == fStackFrame)
		return;

	_SaveViewState();

	_FinishContextMenu(true);

	if (fThread != NULL)
		fThread->ReleaseReference();
	if (fStackFrame != NULL)
		fStackFrame->ReleaseReference();

	fThread = thread;
	fStackFrame = stackFrame;

	if (fThread != NULL)
		fThread->AcquireReference();
	if (fStackFrame != NULL)
		fStackFrame->AcquireReference();

	fVariableTableModel->SetStackFrame(fThread, fStackFrame);

	// request loading the parameter and variable values
	if (fThread != NULL && fStackFrame != NULL) {
		AutoLocker<Team> locker(fThread->GetTeam());

		void* root = fVariableTableModel->Root();
		int32 count = fVariableTableModel->CountChildren(root);
		for (int32 i = 0; i < count; i++) {
			ModelNode* node = (ModelNode*)fVariableTableModel->ChildAt(root, i);
			_RequestNodeValue(node);
		}
	}

	_RestoreViewState();
}


void
VariablesView::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case MSG_SHOW_INSPECTOR_WINDOW:
		{
			// TODO: it'd probably be more ideal to extend the context
			// action mechanism to allow one to specify an explicit
			// target for each action rather than them all defaulting
			// to targetting here.
			Looper()->PostMessage(message);
			break;
		}
		case MSG_SHOW_TYPECAST_NODE_PROMPT:
		{
			BMessage* promptMessage = new(std::nothrow) BMessage(
				MSG_TYPECAST_NODE);

			if (promptMessage == NULL)
				return;

			ObjectDeleter<BMessage> messageDeleter(promptMessage);
			promptMessage->AddPointer("node", fVariableTable
				->SelectionModel()->NodeAt(0));
			PromptWindow* promptWindow = new(std::nothrow) PromptWindow(
				"Specify Type", "Type: ", NULL, BMessenger(this),
				promptMessage);
			if (promptWindow == NULL)
				return;

			messageDeleter.Detach();
			promptWindow->CenterOnScreen();
			promptWindow->Show();
			break;
		}
		case MSG_TYPECAST_NODE:
		{
			ModelNode* node = NULL;
			if (message->FindPointer("node", reinterpret_cast<void **>(&node))
				!= B_OK) {
				break;
			}

			Type* type = NULL;
			BString typeExpression = message->FindString("text");
			if (typeExpression.Length() == 0)
				break;

			FileSourceCode* code = fStackFrame->Function()->GetFunction()
				->GetSourceCode();
			if (code == NULL)
				break;

			SourceLanguage* language = code->GetSourceLanguage();
			if (language == NULL)
				break;

			if (language->ParseTypeExpression(typeExpression,
				fThread->GetTeam()->DebugInfo(), type) != B_OK) {
				BString errorMessage;
				errorMessage.SetToFormat("Failed to resolve type %s",
					typeExpression.String());
				BAlert* alert = new(std::nothrow) BAlert("Error",
					errorMessage.String(), "Close");
				if (alert != NULL)
					alert->Go();
				break;
			}

			BReference<Type> typeRef(type, true);
			ValueNode* valueNode = NULL;
			if (TypeHandlerRoster::Default()->CreateValueNode(
					node->NodeChild(), type, valueNode) != B_OK) {
				break;
			}

			typeRef.Detach();
			node->NodeChild()->SetNode(valueNode);
			node->SetCastedType(type);
			fVariableTableModel->NotifyNodeChanged(node);
			break;
		}
		case MSG_TYPECAST_TO_ARRAY:
		{
			ModelNode* node = NULL;
			if (message->FindPointer("node", reinterpret_cast<void **>(&node))
				!= B_OK) {
				break;
			}

			Type* baseType = dynamic_cast<AddressType*>(node->NodeChild()
					->Node()->GetType())->BaseType();
			ArrayType* arrayType = NULL;
			if (baseType->CreateDerivedArrayType(0, kMaxArrayElementCount,
				false, arrayType) != B_OK) {
				break;
			}

			AddressType* addressType = NULL;
			BReference<Type> typeRef(arrayType, true);
			if (arrayType->CreateDerivedAddressType(DERIVED_TYPE_POINTER,
					addressType) != B_OK) {
				break;
			}

			typeRef.Detach();
			typeRef.SetTo(addressType, true);
			ValueNode* valueNode = NULL;
			if (TypeHandlerRoster::Default()->CreateValueNode(
					node->NodeChild(), addressType, valueNode) != B_OK) {
				break;
			}

			typeRef.Detach();
			node->NodeChild()->SetNode(valueNode);
			node->SetCastedType(addressType);
			fVariableTableModel->NotifyNodeChanged(node);
			break;
		}
		case MSG_SHOW_CONTAINER_RANGE_PROMPT:
		{
			ModelNode* node = (ModelNode*)fVariableTable
				->SelectionModel()->NodeAt(0);
			int32 lowerBound, upperBound;
			ValueNode* valueNode = node->NodeChild()->Node();
			if (!valueNode->IsRangedContainer()) {
				valueNode = node->ChildAt(0)->NodeChild()->Node();
				if (!valueNode->IsRangedContainer())
					break;
			}

			bool fixedRange = valueNode->IsContainerRangeFixed();
			if (valueNode->SupportedChildRange(lowerBound, upperBound)
				!= B_OK) {
				break;
			}

			BMessage* promptMessage = new(std::nothrow) BMessage(
				MSG_SET_CONTAINER_RANGE);
			if (promptMessage == NULL)
				break;

			ObjectDeleter<BMessage> messageDeleter(promptMessage);
			promptMessage->AddPointer("node", node);
			promptMessage->AddBool("fixedRange", fixedRange);
			BString infoText;
			if (fixedRange) {
				infoText.SetToFormat("Allowed range: %" B_PRId32
					"-%" B_PRId32 ".", lowerBound, upperBound);
			} else {
				infoText.SetToFormat("Current range: %" B_PRId32
					"-%" B_PRId32 ".", lowerBound, upperBound);
			}

			PromptWindow* promptWindow = new(std::nothrow) PromptWindow(
				"Set Range", "Range: ", infoText.String(), BMessenger(this),
				promptMessage);
			if (promptWindow == NULL)
				return;

			messageDeleter.Detach();
			promptWindow->CenterOnScreen();
			promptWindow->Show();
			break;
		}
		case MSG_SET_CONTAINER_RANGE:
		{
			ModelNode* node = (ModelNode*)fVariableTable
				->SelectionModel()->NodeAt(0);
			int32 lowerBound, upperBound;
			ValueNode* valueNode = node->NodeChild()->Node();
			if (!valueNode->IsRangedContainer())
				valueNode = node->ChildAt(0)->NodeChild()->Node();
			if (valueNode->SupportedChildRange(lowerBound, upperBound) != B_OK)
				break;

			bool fixedRange = message->FindBool("fixedRange");

			BString rangeExpression = message->FindString("text");
			if (rangeExpression.Length() == 0)
				break;

			RangeList ranges;
			status_t result = UiUtils::ParseRangeExpression(
				rangeExpression, lowerBound, upperBound, fixedRange, ranges);
			if (result != B_OK)
				break;

			valueNode->ClearChildren();
			for (int32 i = 0; i < ranges.CountRanges(); i++) {
				const Range* range = ranges.RangeAt(i);
				result = valueNode->CreateChildrenInRange(
					range->lowerBound, range->upperBound);
				if (result != B_OK)
					break;
			}
			break;
		}
		case MSG_SHOW_WATCH_VARIABLE_PROMPT:
		{
			ModelNode* node = reinterpret_cast<ModelNode*>(
				fVariableTable->SelectionModel()->NodeAt(0));
			ValueLocation* location = node->NodeChild()->Location();
			ValuePieceLocation piece = location->PieceAt(0);
			if (piece.type != VALUE_PIECE_LOCATION_MEMORY)
				break;

			BMessage looperMessage(*message);
			looperMessage.AddUInt64("address", piece.address);
			looperMessage.AddInt32("length", piece.size);
			looperMessage.AddUInt32("type", B_DATA_READ_WRITE_WATCHPOINT);
			Looper()->PostMessage(&looperMessage);
			break;
		}
		case MSG_VALUE_NODE_CHANGED:
		{
			ValueNodeChild* nodeChild;
			ValueNode* oldNode;
			ValueNode* newNode;
			if (message->FindPointer("nodeChild", (void**)&nodeChild) == B_OK
				&& message->FindPointer("oldNode", (void**)&oldNode) == B_OK
				&& message->FindPointer("newNode", (void**)&newNode) == B_OK) {
				BReference<ValueNodeChild> nodeChildReference(nodeChild, true);
				BReference<ValueNode> oldNodeReference(oldNode, true);
				BReference<ValueNode> newNodeReference(newNode, true);

				fVariableTableModel->ValueNodeChanged(nodeChild, oldNode,
					newNode);
			}

			break;
		}
		case MSG_VALUE_NODE_CHILDREN_CREATED:
		{
			ValueNode* node;
			if (message->FindPointer("node", (void**)&node) == B_OK) {
				BReference<ValueNode> newNodeReference(node, true);
				fVariableTableModel->ValueNodeChildrenCreated(node);
			}

			break;
		}
		case MSG_VALUE_NODE_CHILDREN_DELETED:
		{
			ValueNode* node;
			if (message->FindPointer("node", (void**)&node) == B_OK) {
				BReference<ValueNode> newNodeReference(node, true);
				fVariableTableModel->ValueNodeChildrenDeleted(node);
			}

			break;
		}
		case MSG_VALUE_NODE_VALUE_CHANGED:
		{
			ValueNode* node;
			if (message->FindPointer("node", (void**)&node) == B_OK) {
				BReference<ValueNode> newNodeReference(node, true);
				fVariableTableModel->ValueNodeValueChanged(node);
			}

			break;
		}
		case MSG_RESTORE_PARTIAL_VIEW_STATE:
		{
			ModelNode* node;
			if (message->FindPointer("node", (void**)&node) == B_OK) {
				TreeTablePath path;
				if (fVariableTableModel->GetTreePath(node, path)) {
					FunctionID* functionID = fStackFrame->Function()
						->GetFunctionID();
					if (functionID == NULL)
						return;
					BReference<FunctionID> functionIDReference(functionID,
						true);
					VariablesViewState* viewState = fViewStateHistory
						->GetState(fThread->ID(), functionID);
					if (viewState != NULL) {
						_ApplyViewStateDescendentNodeInfos(viewState, node,
							path);
					}
				}
			}
			break;
		}
		case MSG_VALUE_NODE_NEEDS_VALUE:
		case MSG_MODEL_NODE_HIDDEN:
		{
			ModelNode* node;
			if (message->FindPointer("node", (void**)&node) == B_OK) {
				BReference<ModelNode> modelNodeReference(node, true);
				_RequestNodeValue(node);
			}

			break;
		}
		case MSG_VARIABLES_VIEW_CONTEXT_MENU_DONE:
		{
			_FinishContextMenu(false);
			break;
		}
		case MSG_VARIABLES_VIEW_NODE_SETTINGS_CHANGED:
		{
			ModelNode* node;
			if (message->FindPointer("node", (void**)&node) != B_OK)
				break;
			BReference<ModelNode> nodeReference(node, true);

			fVariableTableModel->NotifyNodeChanged(node);
			break;
		}
		default:
			BGroupView::MessageReceived(message);
			break;
	}
}


void
VariablesView::DetachedFromWindow()
{
	_FinishContextMenu(true);
}


void
VariablesView::LoadSettings(const BMessage& settings)
{
	BMessage tableSettings;
	if (settings.FindMessage("variableTable", &tableSettings) == B_OK) {
		GuiSettingsUtils::UnarchiveTableSettings(tableSettings,
			fVariableTable);
	}
}


status_t
VariablesView::SaveSettings(BMessage& settings)
{
	settings.MakeEmpty();

	BMessage tableSettings;
	status_t result = GuiSettingsUtils::ArchiveTableSettings(tableSettings,
		fVariableTable);
	if (result == B_OK)
		result = settings.AddMessage("variableTable", &tableSettings);

	return result;
}




void
VariablesView::TreeTableNodeExpandedChanged(TreeTable* table,
	const TreeTablePath& path, bool expanded)
{
	if (expanded) {
		ModelNode* node = (ModelNode*)fVariableTableModel->NodeForPath(path);
		if (node == NULL)
			return;

		fVariableTableModel->NodeExpanded(node);

		// request the values of all children that don't have any yet

		// If the node only has a hidden child, directly load the child's
		// children's values.
		if (node->CountChildren() == 1) {
			ModelNode* child = node->ChildAt(0);
			if (child->IsHidden())
				node = child;
		}

		// request the values
		for (int32 i = 0; ModelNode* child = node->ChildAt(i); i++) {
			if (child->IsPresentationNode())
				continue;

			_RequestNodeValue(child);
		}
	}
}


void
VariablesView::TreeTableCellMouseDown(TreeTable* table,
	const TreeTablePath& path, int32 columnIndex, BPoint screenWhere,
	uint32 buttons)
{
	if ((buttons & B_SECONDARY_MOUSE_BUTTON) == 0)
		return;

	_FinishContextMenu(true);

	ModelNode* node = (ModelNode*)fVariableTableModel->NodeForPath(path);
	if (node == NULL)
		return;

	Settings* settings = NULL;
	SettingsMenu* settingsMenu = NULL;
	BReference<SettingsMenu> settingsMenuReference;
	status_t error = B_OK;
	TableCellValueRenderer* cellRenderer = node->TableCellRenderer();
	if (cellRenderer != NULL) {
		settings = cellRenderer->GetSettings();
		if (settings != NULL) {
			error = node->GetValueHandler()
				->CreateTableCellValueSettingsMenu(node->GetValue(), settings,
					settingsMenu);
			settingsMenuReference.SetTo(settingsMenu, true);
			if (error != B_OK)
				return;
		}
	}

	TableCellContextMenuTracker* tracker = new(std::nothrow)
		TableCellContextMenuTracker(node, Looper(), this);
	BReference<TableCellContextMenuTracker> trackerReference(tracker);

	ContextActionList* preActionList = new(std::nothrow) ContextActionList;
	if (preActionList == NULL)
		return;

	BPrivate::ObjectDeleter<ContextActionList> preActionListDeleter(
		preActionList);

	error = _GetContextActionsForNode(node, preActionList);
	if (error != B_OK)
		return;

	if (tracker == NULL || tracker->Init(settings, settingsMenu, preActionList) != B_OK)
		return;

	fTableCellContextMenuTracker = trackerReference.Detach();
	fTableCellContextMenuTracker->ShowMenu(screenWhere);
}


void
VariablesView::_Init()
{
	fVariableTable = new TreeTable("variable list", 0, B_FANCY_BORDER);
	AddChild(fVariableTable->ToView());
	fVariableTable->SetSortingEnabled(false);

	// columns
	fVariableTable->AddColumn(new StringTableColumn(0, "Variable", 80, 40, 1000,
		B_TRUNCATE_END, B_ALIGN_LEFT));
	fVariableTable->AddColumn(new VariableValueColumn(1, "Value", 80, 40, 1000,
		B_TRUNCATE_END, B_ALIGN_RIGHT));
	fVariableTable->AddColumn(new StringTableColumn(2, "Type", 80, 40, 1000,
		B_TRUNCATE_END, B_ALIGN_LEFT));

	fVariableTableModel = new VariableTableModel;
	if (fVariableTableModel->Init() != B_OK)
		throw std::bad_alloc();
	fVariableTable->SetTreeTableModel(fVariableTableModel);
	fVariableTable->SetToolTipProvider(fVariableTableModel);

	fContainerListener = new ContainerListener(this);
	fVariableTableModel->SetContainerListener(fContainerListener);

	fVariableTable->AddTreeTableListener(this);

	fViewStateHistory = new VariablesViewStateHistory;
	if (fViewStateHistory->Init() != B_OK)
		throw std::bad_alloc();
}


void
VariablesView::_RequestNodeValue(ModelNode* node)
{
	// get the node child and its container
	ValueNodeChild* nodeChild = node->NodeChild();
	ValueNodeContainer* container = nodeChild->Container();

	BReference<ValueNodeContainer> containerReference(container);
	AutoLocker<ValueNodeContainer> containerLocker(container);

	if (container == NULL || nodeChild->Container() != container)
		return;

	// get the value node and check whether its value has not yet been resolved
	ValueNode* valueNode = nodeChild->Node();
	if (valueNode == NULL) {
		ModelNode* parent = node->Parent();
		if (parent != NULL) {
			TreeTablePath path;
			if (!fVariableTableModel->GetTreePath(parent, path))
				return;

			// if the parent node was already expanded when the child was
			// added, we may not yet have added a value node.
			// Notify the table model that this needs to be done.
			if (fVariableTable->IsNodeExpanded(path))
				fVariableTableModel->NodeExpanded(parent);
		}
	}

	if (valueNode == NULL || valueNode->LocationAndValueResolutionState()
		!= VALUE_NODE_UNRESOLVED) {
		return;
	}

	BReference<ValueNode> valueNodeReference(valueNode);
	containerLocker.Unlock();

	// request resolution of the value
	fListener->ValueNodeValueRequested(fStackFrame->GetCpuState(), container,
		valueNode);
}


status_t
VariablesView::_GetContextActionsForNode(ModelNode* node,
	ContextActionList* actions)
{
	ValueLocation* location = node->NodeChild()->Location();
	status_t result = B_OK;
	BMessage* message = NULL;

	// only show the Inspect option if the value is in fact located
	// in memory.
	if (location->PieceAt(0).type == VALUE_PIECE_LOCATION_MEMORY) {
		result = _AddContextAction("Inspect", MSG_SHOW_INSPECTOR_WINDOW,
			actions, message);
		if (result != B_OK)
			return result;
		message->AddUInt64("address", location->PieceAt(0).address);
	}

	ValueNode* valueNode = node->NodeChild()->Node();

	if (valueNode != NULL) {
		AddressType* type = dynamic_cast<AddressType*>(valueNode->GetType());
		if (type != NULL && type->BaseType() != NULL) {
			result = _AddContextAction("Cast to array", MSG_TYPECAST_TO_ARRAY,
				actions, message);
			if (result != B_OK)
				return result;
			message->AddPointer("node", node);
		}
	}

	result = _AddContextAction("Cast as" B_UTF8_ELLIPSIS,
		MSG_SHOW_TYPECAST_NODE_PROMPT, actions, message);
	if (result != B_OK)
		return result;

	result = _AddContextAction("Watch" B_UTF8_ELLIPSIS,
		MSG_SHOW_WATCH_VARIABLE_PROMPT, actions, message);
	if (result != B_OK)
		return result;

	if (valueNode == NULL)
		return B_OK;

	if (!valueNode->IsRangedContainer()) {
		if (node->CountChildren() == 1 && node->ChildAt(0)->IsHidden()) {
			valueNode = node->ChildAt(0)->NodeChild()->Node();
			if (valueNode == NULL || !valueNode->IsRangedContainer())
				return B_OK;
		} else
			return B_OK;
	}

	result = _AddContextAction("Set visible range" B_UTF8_ELLIPSIS,
		MSG_SHOW_CONTAINER_RANGE_PROMPT, actions, message);
	if (result != B_OK)
		return result;

	return B_OK;
}


status_t
VariablesView::_AddContextAction(const char* action, uint32 what,
	ContextActionList* actions, BMessage*& _message)
{
	_message = new BMessage(what);
	if (_message == NULL)
		return B_NO_MEMORY;

	ObjectDeleter<BMessage> messageDeleter(_message);

	ActionMenuItem* item = new(std::nothrow) ActionMenuItem(action,
		_message);
	if (item == NULL)
		return B_NO_MEMORY;

	messageDeleter.Detach();
	ObjectDeleter<ActionMenuItem> actionDeleter(item);
	if (!actions->AddItem(item))
		return B_NO_MEMORY;

	actionDeleter.Detach();

	return B_OK;
}


void
VariablesView::_FinishContextMenu(bool force)
{
	if (fTableCellContextMenuTracker != NULL) {
		if (!fTableCellContextMenuTracker->FinishMenu(force) || force) {
			fTableCellContextMenuTracker->ReleaseReference();
			fTableCellContextMenuTracker = NULL;
		}
	}
}



void
VariablesView::_SaveViewState() const
{
	if (fThread == NULL || fStackFrame == NULL
		|| fStackFrame->Function() == NULL) {
		return;
	}

	// get the function ID
	FunctionID* functionID = fStackFrame->Function()->GetFunctionID();
	if (functionID == NULL)
		return;
	BReference<FunctionID> functionIDReference(functionID, true);

	// create an empty view state
	VariablesViewState* viewState = new(std::nothrow) VariablesViewState;
	if (viewState == NULL)
		return;
	BReference<VariablesViewState> viewStateReference(viewState, true);

	if (viewState->Init() != B_OK)
		return;

	// populate it
	TreeTablePath path;
	if (_AddViewStateDescendentNodeInfos(viewState, fVariableTableModel->Root(),
			path) != B_OK) {
		return;
	}
// TODO: Add values!

	// add the view state to the history
	fViewStateHistory->SetState(fThread->ID(), functionID, viewState);
}


void
VariablesView::_RestoreViewState()
{
	if (fPreviousViewState != NULL) {
		fPreviousViewState->ReleaseReference();
		fPreviousViewState = NULL;
	}

	if (fThread == NULL || fStackFrame == NULL
		|| fStackFrame->Function() == NULL) {
		return;
	}

	// get the function ID
	FunctionID* functionID = fStackFrame->Function()->GetFunctionID();
	if (functionID == NULL)
		return;
	BReference<FunctionID> functionIDReference(functionID, true);

	// get the previous view state
	VariablesViewState* viewState = fViewStateHistory->GetState(fThread->ID(),
		functionID);
	if (viewState == NULL)
		return;

	// apply the view state
	TreeTablePath path;
	_ApplyViewStateDescendentNodeInfos(viewState, fVariableTableModel->Root(),
		path);
}


status_t
VariablesView::_AddViewStateDescendentNodeInfos(VariablesViewState* viewState,
	void* parent, TreeTablePath& path) const
{
	int32 childCount = fVariableTableModel->CountChildren(parent);
	for (int32 i = 0; i < childCount; i++) {
		ModelNode* node = (ModelNode*)fVariableTableModel->ChildAt(parent, i);
		if (!path.AddComponent(i))
			return B_NO_MEMORY;

		// add the node's info
		VariablesViewNodeInfo nodeInfo;
		nodeInfo.SetNodeExpanded(fVariableTable->IsNodeExpanded(path));
		nodeInfo.SetCastedType(node->GetCastedType());
		TableCellValueRenderer* renderer = node->TableCellRenderer();
		if (renderer != NULL) {
			Settings* settings = renderer->GetSettings();
			if (settings != NULL)
				nodeInfo.SetRendererSettings(settings->Message());
		}

		status_t error = viewState->SetNodeInfo(node->GetVariable()->ID(),
			node->GetPath(), nodeInfo);
		if (error != B_OK)
			return error;

		// recurse
		error = _AddViewStateDescendentNodeInfos(viewState, node, path);
		if (error != B_OK)
			return error;

		path.RemoveLastComponent();
	}

	return B_OK;
}


status_t
VariablesView::_ApplyViewStateDescendentNodeInfos(VariablesViewState* viewState,
	void* parent, TreeTablePath& path)
{
	int32 childCount = fVariableTableModel->CountChildren(parent);
	for (int32 i = 0; i < childCount; i++) {
		ModelNode* node = (ModelNode*)fVariableTableModel->ChildAt(parent, i);
		if (!path.AddComponent(i))
			return B_NO_MEMORY;

		// apply the node's info, if any
		const VariablesViewNodeInfo* nodeInfo = viewState->GetNodeInfo(
			node->GetVariable()->ID(), node->GetPath());
		if (nodeInfo != NULL) {
			// NB: if the node info indicates that the node in question
			// was being cast to a different type, this *must* be applied
			// before any other view state restoration, since it potentially
			// changes the child hierarchy under that node.
			Type* type = nodeInfo->GetCastedType();
			if (type != NULL) {
				ValueNode* valueNode = NULL;
				if (TypeHandlerRoster::Default()->CreateValueNode(
					node->NodeChild(), type, valueNode) == B_OK) {
					node->NodeChild()->SetNode(valueNode);
					node->SetCastedType(type);
				}
			}

			// we don't have a renderer yet so we can't apply the settings
			// at this stage. Store them on the model node so we can lazily
			// apply them once the value is retrieved.
			node->SetLastRendererSettings(nodeInfo->GetRendererSettings());

			fVariableTable->SetNodeExpanded(path, nodeInfo->IsNodeExpanded());

			// recurse
			status_t error = _ApplyViewStateDescendentNodeInfos(viewState, node,
				path);
			if (error != B_OK)
				return error;
		}

		path.RemoveLastComponent();
	}

	return B_OK;
}


// #pragma mark - Listener


VariablesView::Listener::~Listener()
{
}
