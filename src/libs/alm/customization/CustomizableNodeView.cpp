/*
 * Copyright 2011, Clemens Zeidler <haiku@clemens-zeidler.de>
 * Distributed under the terms of the MIT License.
 */


#include <CustomizableNodeView.h>

#include <Looper.h>
#include <MenuItem.h>
#include <PopUpMenu.h>

#include <CustomizableNodeFactory.h>

#include "CustomizableNodeItemStates.h"
#include "LayerViewDefinitions.h"


node_slot::node_slot(CustomizableLayerItem* _node)
	:
	node(_node),
	highlightCompatible(false),
	highlightDockNow(false)
{	
}


BPoint
node_slot::ViewPosition()
{
	if (node == NULL)
		return position;
	BPoint socketPosition = node->RasterFrame().LeftTop();
	socketPosition += position;
	return socketPosition;
}


node_wire::node_wire(CustomizableLayerItem* _node)
	:
	node_slot(_node),
	selected(false),
	endPoint(NULL)
{
}

								
node_socket::node_socket(CustomizableLayerItem* _node,
	Customizable::Socket* _socket, int32 _connection)
	:
	node_wire(_node),
	socket(_socket),
	connection(_connection)
{
}


node_connection::node_connection(CustomizableLayerItem* _node,
	int32 _interface)
	:
	node_slot(_node),
	interface(_interface),
	socket(NULL)
{
}


node_event::node_event(CustomizableLayerItem* _node,  int32 event,
	int32 _connection)
	:
	node_wire(_node),
	eventId(event),
	connection(_connection)
{	
}


node_method::node_method(CustomizableLayerItem* _node,  int32 method)
	:
	node_slot(_node),
	id(method),
	event(NULL)
{	
}


static void
StrokeWire(BView* view, BPoint& start, BPoint& end, bool selected,
	float offset)
{
	BPoint offsetStart = start + BPoint(10, 0);
	BPoint offsetEnd = end - BPoint(10, 0);

	rgb_color lowColor = selected ? kMediumBlueColor : kBorderMediumColor;
	rgb_color highColor = selected ? kLightBlueColor : kBorderLightColor;

	view->SetPenSize(3.0);
	view->BeginLineArray(3);
	view->AddLine(start, offsetStart, lowColor);
	view->AddLine(offsetStart, offsetEnd, lowColor);
	view->AddLine(offsetEnd, end, lowColor);
	view->EndLineArray();

	view->SetPenSize(1.0);
	view->BeginLineArray(3);
	view->AddLine(start, offsetStart, highColor);
	view->AddLine(offsetStart, offsetEnd, highColor);
	view->AddLine(offsetEnd, end, highColor);
	view->EndLineArray();
}


using BALM::CustomizationAction;


class CustomizableNodeView::AddComponentAction : public CustomizationAction {
public:
	AddComponentAction(CustomizableNodeView* view, const char* component,
		BPoint point)
		:
		fView(view),
		fComponentName(component),
		fComponent(NULL),
		fLayerItem(NULL),
		fPoint(point)
	{
	}

	status_t Perform()
	{
		BReference<Customizable> component;
		component = fView->fRoster->InstantiateCustomizable(fComponentName);
		if (component.Get() == NULL)
			return B_ERROR;
		fComponent = component.Get();
		fView->fRoster->AddToShelf(component);

		fLayerItem = new CustomizableLayerItem(fView, component, fPoint);
		fView->AddLayer(fLayerItem);

		return B_OK;
	}
	
	
	status_t Undo()
	{
		fView->RemoveLayer(fLayerItem);
		delete fLayerItem;

		fView->fRoster->RemoveFromShelf(fComponent);

		return B_ERROR;
	}

private:
			CustomizableNodeView*	fView;
			BString				fComponentName;
			Customizable*		fComponent;
			CustomizableLayerItem* fLayerItem;
			BPoint				fPoint;
};


class CustomizableNodeView::RemoveComponentAction : public CustomizationAction {
public:
	RemoveComponentAction(CustomizableLayerItem* item)
		:
		fItem(item)
	{
	}

	status_t Perform()
	{
		Customizable* component = fItem->GetCustomizable();
		CustomizableNodeView* view = fItem->NodeView();
		view->RemoveLayer(fItem);
		view->Invalidate();
		delete fItem;
		fItem = NULL;

		CustomizableRoster::DefaultRoster()->RemoveFromShelf(component);

		return B_OK;
	}
	
	
	status_t Undo()
	{
		// TODO the state of the Customizable has to be saved and 
		return B_ERROR;
	}

private:
			CustomizableLayerItem*	fItem;
};


CustomizableNodeView::CustomizableNodeView(CustomizableRoster* roster)
	:
	fRoster(roster)
{
	fWireLayerItem = new WireLayerItem(this);
	AddLayer(fWireLayerItem);

	fRoster->StartWatching(this);
}


CustomizableNodeView::~CustomizableNodeView()
{
	fRoster->StopWatching(this);
}


void				
CustomizableNodeView::StoreLayout(BMessage* message) const
{
	message->MakeEmpty();
	for (int32 i = 0; i < CountLayers(); i++) {
		CustomizableLayerItem* item
			= dynamic_cast<CustomizableLayerItem*>(LayerAt(i));
		if (item == NULL)
			continue;
		BReference<Customizable> customizable = item->GetCustomizable();
		if (customizable == NULL)
			continue;
		message->AddPointer("customizable", customizable.Get());
		message->AddPoint("position", item->Position());
	}
}


void
CustomizableNodeView::RestoreLayout(const BMessage* message)
{
	for (int32 i = 0; i < CountLayers(); i++) {
		Customizable* customizable;
		if (message->FindPointer("customizable", i, (void**)&customizable)
			!= B_OK)
			continue;
		BPoint position;
		if (message->FindPoint("position", i, &position) != B_OK)
			continue;

		CustomizableLayerItem* item = FindNode(customizable);
		if (item == NULL)
			continue;
		item->SetPosition(position);
	}
}


void
CustomizableNodeView::MessageReceived(BMessage* message)
{
	switch (message->what) {
	case kMsgCreateComponent:
	{
		BString component;
		message->FindString("component", &component);
		BPoint point;
		message->FindPoint("_drop_point_", &point);
		BPoint offset;
		message->FindPoint("_drop_offset_", &offset);
		point -= offset;
		ConvertFromScreen(&point);

		fActionHistory.PerformAction(new AddComponentAction(this, component,
			point));		
		break;
	}

	case B_CUSTOMIZABLE_ADDED:
	{
		Customizable* customizable;
		if (message->FindPointer("customizable", (void**)&customizable) != B_OK)
			break;
		if (FindNode(customizable) != NULL)
			break;
		BArray<BWeakReference<Customizable> > list;
		fRoster->GetCustomizableList(list);
		for (int32 i = 0; i < list.CountItems(); i++) {
			BReference<Customizable> ref = list.ItemAt(i).GetReference();
			if (ref == NULL)
				continue;
			if (ref.Get() != customizable)
				continue;
			AddLayer(new CustomizableLayerItem(this, ref, BPoint(10, 10)));
			break;
		}
		break;
	}

	case B_CUSTOMIZABLE_REMOVED:
		for (int32 i = 0; i < CountLayers(); i++) {
			CustomizableLayerItem* item = dynamic_cast<CustomizableLayerItem*>(
				LayerAt(i));
			if (item == NULL)
				continue;
			if (item->GetCustomizable() == NULL)
				RemoveLayer(item);
		}
		break;

	case B_OBJECT_EVENT_CONNECTED:
	case B_OBJECT_EVENT_DISCONNECTED:
	case B_SOCKET_CONNECTED:
	case B_SOCKET_DISCONNECTED:
		fWireLayerItem->RebuildWires();
		break;

	case B_OBSERVER_NOTICE_CHANGE:
	{
		int32 what = message->FindInt32(B_OBSERVE_WHAT_CHANGE);
		if (what == kCustomizableSelected) {
			_HightlightCustomizables(message);
			break;
		}
	}
	default:
		BitmapLayerView::MessageReceived(message);	
	}
}


void
CustomizableNodeView::AttachedToWindow()
{
	_LoadCustomizables();
}


bool
CustomizableNodeView::AddLayer(LayerItem* layer)
{
	bool status = BitmapLayerView::AddLayer(layer);
	fWireLayerItem->RebuildWires();
	return status;
}


bool
CustomizableNodeView::RemoveLayer(LayerItem* layer)
{
	bool status = BitmapLayerView::RemoveLayer(layer);
	fWireLayerItem->RebuildWires();
	return status;
}


CustomizableLayerItem*
CustomizableNodeView::FindNode(Customizable* customizable) const
{
	for (int32 i = 0; i < CountLayers(); i++) {
		CustomizableLayerItem* item = dynamic_cast<CustomizableLayerItem*>(
			LayerAt(i));
		if (item == NULL)
			continue;
		if (item->GetCustomizable().Get() == customizable)
			return item;
	}
	return NULL;
}


void
CustomizableNodeView::SelectWire(node_wire* wire)
{
	bool changed = fWireLayerItem->SelectWire(wire);
	if (changed)
		Invalidate();
}


bool
CustomizableNodeView::RemoveWire(node_wire* wire)
{
	return fWireLayerItem->RemoveWire(wire);
}


void
CustomizableNodeView::AddWire(node_wire* wire)
{
	fWireLayerItem->AddWire(wire);
}


bool
CustomizableNodeView::WireContains(node_wire* wire, const BPoint& point) const
{
	if (wire->endPoint == NULL)
		return false;

	const float kDetectionRange = 3;

	BPoint start = wire->ViewPosition();
	BPoint end = wire->endPoint->ViewPosition();

	if (point.x > start.x && point.x < start.x + kWireOffset) {
		if (fabs(start.y - point.y) < kDetectionRange)
			return true;
	}
	if (point.x > end.x - kWireOffset && point.x < end.x) {
		if (fabs(end.y - point.y) < kDetectionRange)
			return true;
	}

	start.x += kWireOffset;
	end.x -= kWireOffset;
	// on the wire?
	if (end.x < start.x) {
		BPoint temp = end;
		end = start;
		start = temp;	
	}
	if (point.x < start.x || point.x > end.x)
		return false;

	float deltaX = end.x - start.x;
	if (deltaX == 0)
		return false;
	float m = (end.y - start.y) / deltaX;
	float b = start.y - m * start.x;

	BPoint onLine;
	onLine.x = (m * (b - point.y) - point.x) / -(1 + m * m);
	onLine.y = b + m * onLine.x;

	float distance = sqrt(pow(onLine.x - point.x, 2)
		+ pow(onLine.y - point.y, 2)); 
	if (distance < kDetectionRange)
		return true;

	return false;
}


void
CustomizableNodeView::HighlightFreeSlots(node_socket* socket)
{
	for (int32 i = 0; i < CountLayers(); i++) {
		CustomizableLayerItem* item = dynamic_cast<CustomizableLayerItem*>(
			LayerAt(i));
		if (item == NULL)
			continue;
		item->HighlightFreeSlots(socket);
	}
}


void
CustomizableNodeView::HighlightFreeSlots(node_connection* connection)
{
	for (int32 i = 0; i < CountLayers(); i++) {
		CustomizableLayerItem* item = dynamic_cast<CustomizableLayerItem*>(
			LayerAt(i));
		if (item == NULL)
			continue;
		item->HighlightFreeSlots(connection);
	}
}


void
CustomizableNodeView::_LoadCustomizables()
{
	BRect frame = Window()->Bounds();

	BPoint position(10, 10);

	BArray<BWeakReference<Customizable> > list;
	fRoster->GetCustomizableList(list);
	for (int32 i = 0; i < list.CountItems(); i++) {
		BReference<Customizable> customizable
			= list.ItemAt(i).GetReference();
		if (customizable.Get() == NULL)
			continue;
		
		BString label = customizable->ObjectName();
		CustomizableLayerItem* item = new CustomizableLayerItem(this,
			customizable, position);
		AddLayer(item);
		position.x += 10 + item->Frame().Width();

		if (position.x + 10 + item->Frame().Width() > frame.right) {
			position.x = 10;
			position.y += 100;
		}
	}
}


void
CustomizableNodeView::_HightlightCustomizables(BMessage* message)
{
	DeselectAll();

	for (int32 i = 0; true; i++) {
		Customizable* customizable;
		if (message->FindPointer("customizable", i, (void**)&customizable)
			!= B_OK)
			break;

		CustomizableLayerItem* item = FindNode(customizable);
		if (item != NULL)
			item->SetSelected(true);
	}

	Invalidate();
}


const float kDefaultBoxHeight = 60;


CustomizableLayerItem::CustomizableLayerItem(CustomizableNodeView* parent,
	Customizable* customizable, BPoint position)
	:
	BoxItem(parent, customizable->ObjectName()),
	fParent(parent),
	fCustomizable(customizable)
{
	fState = new LayerItemState(this);

	BRect frame(0, 0, 200, kDefaultBoxHeight);
	frame.OffsetBy(position);
	SetFrame(frame);

	DoLayout();
}

	
CustomizableLayerItem::~CustomizableLayerItem()
{
}


void
CustomizableLayerItem::Draw(BRect updateRect)
{
	BoxItem::Draw(updateRect);

	BReference<Customizable> customizable = fCustomizable.GetReference();
	if (customizable == NULL)
		return;

	BRect frame = fView->RasterFrame(fFrame);

	fView->PushState();

	// connections
	for (unsigned int i = 0; i < fConnectionList.size(); i++) {
		node_connection& connection = fConnectionList[i];
		BPoint socketPosition = connection.ViewPosition();

		BString interface = customizable->InterfaceAt(i);
		BPoint stringPos(frame.left + kSocketSize + kBoxBorderSpace,
			socketPosition.y + kSocketSize / 2);
		fView->SetLowColor(kBoxColor);
		fView->SetHighColor(kTitleColor);

		fView->DrawString(interface, stringPos);

		DrawLeftSocket(socketPosition, &connection, connection.socket);
	}

	// sockets
	for (unsigned int i = 0; i < fSocketList.size(); i++) {
		node_socket& nodeSocket = fSocketList[i];
		BPoint socketPosition = nodeSocket.ViewPosition();

		Customizable::Socket* socket = nodeSocket.socket;
		if (i == 0) {
			BString socketInfo = socket->Name();
			socketInfo += " (";
			socketInfo += socket->Interface();
			socketInfo += ")";

			float stringWidth = fView->StringWidth(socketInfo);
			BPoint stringPos(frame.right - kBoxBorderSpace - stringWidth
				- kSocketSize, socketPosition.y + kSocketSize / 2);
			fView->SetLowColor(kBoxColor);
			fView->SetHighColor(kTitleColor);

			_DrawString(frame, socketInfo, stringPos, stringWidth);
		}

		DrawRightSocket(socketPosition, &nodeSocket, nodeSocket.endPoint);
	}

	// methods
	for (unsigned int i = 0; i < fMethodList.size(); i++) {
		node_method& nodeMethod = fMethodList[i];
		BPoint socketPosition = nodeMethod.ViewPosition();

		BString methodName = customizable->MethodAt(nodeMethod.id)->GetName();
		BPoint stringPos(frame.left + kSocketSize + kBoxBorderSpace,
			socketPosition.y + kSocketSize / 2);
		fView->SetLowColor(kBoxColor);
		fView->SetHighColor(kTitleColor);
		fView->DrawString(methodName, stringPos);

		DrawLeftSocket(socketPosition, &nodeMethod, nodeMethod.event);
	}

	// events
	for (unsigned int i = 0; i < fEventList.size(); i++) {
		node_event& nodeEvent = fEventList[i];
		BPoint socketPosition = nodeEvent.ViewPosition();

		const event_data* data = customizable->EventAt(nodeEvent.eventId);
		BString event;
		if (data != NULL)
			event = data->event;

		if (i == 0) {
			float stringWidth = fView->StringWidth(event);
			BPoint stringPos(frame.right - kBoxBorderSpace - stringWidth
				- kSocketSize, socketPosition.y + kSocketSize / 2);
			fView->SetLowColor(kBoxColor);
			fView->SetHighColor(kTitleColor);
	
			fView->DrawString(event, stringPos);
		}

		DrawRightSocket(socketPosition, &nodeEvent, nodeEvent.endPoint);
	}

	fView->PopState();
}


bool
CustomizableLayerItem::MouseMoved(BPoint point, uint32 transit,
	const BMessage* message)
{
	return fState->MouseMoved(point, transit, message);
}


bool
CustomizableLayerItem::MouseDown(BPoint point)
{
	BReference<Customizable> customizable = fCustomizable.GetReference();
	if (customizable == NULL)
		return false;

	if (fFrame.Contains(point) == false)
		return false;

	BMessage* message = fView->Looper()->CurrentMessage();
	int32 buttons = message->FindInt32("buttons");

	if ((buttons & B_SECONDARY_MOUSE_BUTTON) != 0) {
		BPopUpMenu rightClickMenu("Remove");
		BMenuItem* removeItem = new BMenuItem("Remove", NULL);
		rightClickMenu.AddItem(removeItem);
		point = fView->ConvertToScreen(point);
		BMenuItem* result = rightClickMenu.Go(point);
		if (removeItem != result)
			return true;

		if (customizable->Orphan() == false)
			return true;
		fParent->ActionHistory().PerformAction(
			new CustomizableNodeView::RemoveComponentAction(this));
		// Dead by now!!!
		return true;
	}

	_NotifySelected();

	node_connection* connection = NULL;
	node_event* event = NULL;
	node_method* method = NULL;
	node_socket* socket = FindSocketAt(point);
	if (socket != NULL) {
		fParent->SelectWire(socket);
		if (socket->endPoint == NULL)
			SetState(new SocketStartWireState(this, socket));
		else
			SetState(new SocketDragState(this, socket));
	} else if ((connection = FindConnectionAt(point)) != NULL) {
		fParent->SelectWire(connection->socket);
		if (connection->socket == NULL)
			SetState(new EndpointStartWireState(this, connection));
		else
			SetState(new EndpointDragState(this, connection));
	} else if ((event = FindEventAt(point)) != NULL) {
		fParent->SelectWire(event);
		if (event->endPoint == NULL)
			SetState(new EventStartWireState(this, event));
		else
			SetState(new EventDragState(this, event));
	} else if ((method = FindMethodAt(point)) != NULL) {
		fParent->SelectWire(method->event);
		if (method->event == NULL)
			SetState(new MethodStartWireState(this, method));
		else
			SetState(new MethodDragState(this, method));	
	} else
		fParent->SelectWire(NULL);
 
	return fState->MouseDown(point);
}


bool
CustomizableLayerItem::MouseUp(BPoint point)
{
	return fState->MouseUp(point);
}


bool
CustomizableLayerItem::GetSocketPosition(int32 socketId, int32 connection,
	BPoint& position)
{
	node_socket* node = GetNodeSocket(socketId, connection);
	if (node == NULL)
		return false;

	BRect frame = fView->RasterFrame(fFrame);
	position = frame.LeftTop();
	position += node->position;
	return true;
}


node_socket*
CustomizableLayerItem::FindSocketAt(BPoint position)
{
	for (unsigned int i = 0; i < fSocketList.size(); i++) {
		node_socket& socket = fSocketList[i];
		BPoint socketPos = socket.ViewPosition();
		BRect socketRect(socketPos.x - kSocketSize,
			socketPos.y - kSocketSize / 2, socketPos.x,
			socketPos.y + kSocketSize / 2);
		if (socketRect.Contains(position) == true)
			return &socket;
	}
	return NULL;
}
	 

bool
CustomizableLayerItem::GetConnectionPosition(int32 interface, BPoint& position)
{
	node_connection* connection = GetNodeConnection(interface);
	if (connection == NULL)
		return false;

	BRect frame = fView->RasterFrame(fFrame);
	position = frame.LeftTop();
	position += connection->position;
	return true;
}


node_connection*
CustomizableLayerItem::FindConnectionAt(BPoint position)
{
	for (unsigned int i = 0; i < fConnectionList.size(); i++) {
		node_connection& connection = fConnectionList[i];
		BPoint socketPos = connection.ViewPosition();
		BRect socketRect(socketPos.x, socketPos.y - kSocketSize / 2,
			socketPos.x + kSocketSize, socketPos.y + kSocketSize / 2);
		if (socketRect.Contains(position) == true)
			return &connection;
	}
	return NULL;
}


bool
CustomizableLayerItem::GetEventPosition(int32 event, int32 connection,
	BPoint& position)
{
	node_event* node = GetNodeEvent(event, connection);
	if (node == NULL)
		return false;

	BRect frame = fView->RasterFrame(fFrame);
	position = frame.LeftTop();
	position += node->position;
	return true;
}


node_event*
CustomizableLayerItem::FindEventAt(BPoint position)
{
	for (unsigned int i = 0; i < fEventList.size(); i++) {
		node_event& event = fEventList[i];
		BPoint socketPos = event.ViewPosition();
		BRect socketRect(socketPos.x - kSocketSize,
			socketPos.y - kSocketSize / 2, socketPos.x,
			socketPos.y + kSocketSize / 2);
		if (socketRect.Contains(position) == true)
			return &event;
	}
	return NULL;
}


bool
CustomizableLayerItem::GetMethodPosition(int32 method, BPoint& position)
{
	node_method* node = GetNodeMethod(method);
	if (node == NULL)
		return false;

	BRect frame = fView->RasterFrame(fFrame);
	position = frame.LeftTop();
	position += node->position;
	return true;
}


node_method*
CustomizableLayerItem::FindMethodAt(BPoint position)
{
	for (unsigned int i = 0; i < fMethodList.size(); i++) {
		node_method& method = fMethodList[i];
		BPoint socketPos = method.ViewPosition();
		BRect socketRect(socketPos.x, socketPos.y - kSocketSize / 2,
			socketPos.x + kSocketSize, socketPos.y + kSocketSize / 2);
		if (socketRect.Contains(position) == true)
			return &method;
	}
	return NULL;
}


node_socket*
CustomizableLayerItem::GetNodeSocket(int32 socketId, int32 connection)
{
	BReference<Customizable> customizable = fCustomizable.GetReference();
	if (customizable == NULL)
		return NULL;

	Customizable::Socket* socket = customizable->SocketAt(socketId);
	if (socket == NULL)
		return NULL;

	for (unsigned int i = 0; i < fSocketList.size(); i++) {
		node_socket& node = fSocketList[i];
		if (node.socket == socket && node.connection == connection)
			return &node;
	}
	return NULL;
}


node_connection*
CustomizableLayerItem::GetNodeConnection(int32 connection)
{
	if (connection < 0 || connection >= (int32)fConnectionList.size())
		return NULL;
	return &fConnectionList[connection];
}


node_event*
CustomizableLayerItem::GetNodeEvent(int32 event, int32 connection)
{
	for (unsigned int i = 0; i < fEventList.size(); i++) {
		node_event& node = fEventList[i];
		if (node.eventId == event && node.connection == connection)
			return &node;
	}
	return NULL;
}


node_method*
CustomizableLayerItem::GetNodeMethod(int32 method)
{
	if (method < 0 || method >= (int32)fMethodList.size())
		return NULL;
	return &fMethodList[method];
}


BReference<Customizable>
CustomizableLayerItem::GetCustomizable() const
{
	BWeakReference<Customizable>& customizable
		= const_cast<BWeakReference<Customizable>&>(fCustomizable);
	return customizable.GetReference();
}



void
CustomizableLayerItem::HighlightFreeSlots(node_socket* socket)
{
	if (socket == NULL) {
		for (unsigned i = 0; i < fConnectionList.size(); i++)
			fConnectionList[i].highlightCompatible = false;
		return;
	}

	for (unsigned i = 0; i < fConnectionList.size(); i++) {
		node_connection& connection = fConnectionList[i];
		if (connection.socket != NULL)
			continue;
		if (connection.node->GetCustomizable()->InterfaceAt(
			connection.interface) == socket->socket->Interface())
			connection.highlightCompatible = true;
	}
}


void
CustomizableLayerItem::HighlightFreeSlots(node_connection* connection)
{
	if (connection == NULL) {
		for (unsigned i = 0; i < fSocketList.size(); i++)
			fSocketList[i].highlightCompatible = false;
		return;
	}

	for (unsigned i = 0; i < fSocketList.size(); i++) {
		node_socket& socket = fSocketList[i];
		if (socket.endPoint != NULL)
			continue;
		if (connection->node->GetCustomizable()->InterfaceAt(
			connection->interface) == socket.socket->Interface())
			socket.highlightCompatible = true;
	}
}
			

void
CustomizableLayerItem::_DrawString(BRect& frame, BString& string,
	BPoint& position, float stringWidth)
{
	float maxSize = frame.Width() - 2 * (kBoxBorderSpace + kSocketSize);
	if (stringWidth > maxSize)
		fView->TruncateString(&string, B_TRUNCATE_END, maxSize);

	float left = frame.left + kBoxBorderSpace + kSocketSize;
	if (position.x < left)
		position.x = left; 
	fView->DrawString(string, position);
}


void
CustomizableLayerItem::_NotifySelected()
{
	Customizable* customizable = fCustomizable.GetReference();
	BMessage message;
	message.AddPointer("customizable", customizable);
	fParent->SendNotices(kCustomizableSelected, &message);
}


void
CustomizableLayerItem::SetState(LayerItemState* state)
{
	if (state == NULL)
		state = new LayerItemState(this);
	delete fState;
	fState = state;
}


void
CustomizableLayerItem::DoLayout()
{
	BReference<Customizable> customizable = fCustomizable.GetReference();
	if (customizable == NULL)
		return;

	fFrame.bottom = fFrame.top + kDefaultBoxHeight;

	// clean up
	fSocketList.clear();
	fConnectionList.clear();
	fEventList.clear();
	fMethodList.clear();

	font_height fontHeight;
	fView->GetFontHeight(&fontHeight);
	fLineHeight = fontHeight.ascent + fontHeight.descent + fontHeight.leading;
 	// title
	float yPosition = 2 * kBoxBorderSpace;
	yPosition += fLineHeight;

	// interfaces
	for (int32 i = 0; i < customizable->CountInterfaces(); i++) {
		yPosition += fLineHeight;
		node_connection connection(this, i);
		connection.position = BPoint(0, yPosition);
		fConnectionList.push_back(connection);
	}
	yPosition += kBoxBorderSpace;

	// sockets
	yPosition += fLineHeight;
	for (int32 i = 0; i < customizable->CountSockets(); i++) {
		Customizable::Socket* socket = customizable->SocketAt(i);
		for (int32 c = 0; c < socket->CountConnections(); c++) {
			node_socket nodeSocket(this, socket, c);
			nodeSocket.position = BPoint(fFrame.Width(), yPosition);
			fSocketList.push_back(nodeSocket);
			yPosition += fLineHeight;
		}
		if (socket->HasEmptySlot() == true) {
			node_socket nodeSocket(this, socket, -1);
			nodeSocket.position = BPoint(fFrame.Width(), yPosition);
			fSocketList.push_back(nodeSocket);
			yPosition += fLineHeight;
		}
	}

	// methods
	yPosition += fLineHeight;
	for (int32 i = 0; i < customizable->CountMethods(); i++) {
		node_method node(this, i);
		node.position = BPoint(0, yPosition);
		fMethodList.push_back(node);
		yPosition += fLineHeight;
	}

	// events
	yPosition += fLineHeight;
	for (int32 i = 0; i < customizable->CountEvents(); i++) {
		const event_data* data = customizable->EventAt(i);
		for (unsigned int c = 0; c < data->connections.size(); c++) {
			node_event node(this, i, c);
			node.position = BPoint(fFrame.Width(), yPosition);
			fEventList.push_back(node);
			yPosition += fLineHeight;
		}
		node_event node(this, i, -1);
		node.position = BPoint(fFrame.Width(), yPosition);
		fEventList.push_back(node);
		yPosition += fLineHeight;
	}	

	yPosition -= kSocketSize;

	if (yPosition > fFrame.Height())
		SetSize(fFrame.Width(), yPosition);
}


void
CustomizableLayerItem::DrawLeftSocket(BPoint position,
	node_slot* connection, node_wire* wire)
{
	BRect socketFrame(position.x, position.y - kSocketSize / 2,
		position.x + kSocketSize, position.y + kSocketSize / 2);
	fView->SetHighColor(kBorderDarkColor);
	fView->BeginLineArray(3);
	fView->AddLine(socketFrame.LeftTop(), socketFrame.RightTop(),
		kBorderDarkColor);
	fView->AddLine(socketFrame.RightTop(), socketFrame.RightBottom(),
		kBorderDarkColor);
	fView->AddLine(socketFrame.RightBottom(), socketFrame.LeftBottom(),
		kBorderDarkColor);
	fView->EndLineArray();

	socketFrame.top += 1;
	socketFrame.right -= 1;
	socketFrame.bottom -= 1;
	fView->SetHighColor(kBackGroundColor);
	fView->FillRect(socketFrame);

	if (connection->highlightDockNow) {
		fView->SetHighColor(kDockNowColor);
		fView->FillRect(socketFrame);
	} else if (connection->highlightCompatible) {
		fView->SetHighColor(kCompatibleSlotColor);
		fView->FillRect(socketFrame);
	}

	if (wire == NULL)
		return;
	// draw wire
	rgb_color lowColor = wire->selected ? kMediumBlueColor : kBorderMediumColor;
	rgb_color highColor = wire->selected ? kLightBlueColor : kBorderLightColor;

	BPoint endPos = position;
	endPos.x += kSocketSize - 1;
	fView->SetPenSize(3.0);
	fView->SetHighColor(lowColor);
	fView->StrokeLine(position, endPos);

	endPos.x -= 1;
	fView->SetPenSize(1.0);
	fView->SetHighColor(highColor);
	fView->StrokeLine(position, endPos);
}


void
CustomizableLayerItem::DrawRightSocket(BPoint position, node_wire* wire,
	node_slot* connection)
{
	BRect socketFrame(position.x - kSocketSize, position.y - kSocketSize / 2,
		position.x, position.y + kSocketSize / 2);
	fView->SetHighColor(kBorderDarkColor);
	fView->BeginLineArray(3);
	fView->AddLine(socketFrame.RightTop(), socketFrame.LeftTop(),
		kBorderDarkColor);
	fView->AddLine(socketFrame.LeftTop(), socketFrame.LeftBottom(),
		kBorderDarkColor);
	fView->AddLine(socketFrame.LeftBottom(), socketFrame.RightBottom(),
		kBorderDarkColor);
	fView->EndLineArray();

	socketFrame.top += 1;
	socketFrame.left += 1;
	socketFrame.bottom -= 1;
	fView->SetHighColor(kBackGroundColor);
	fView->FillRect(socketFrame);

	if (wire->highlightDockNow) {
		fView->SetHighColor(kDockNowColor);
		fView->FillRect(socketFrame);
	} else if (wire->highlightCompatible) {
		fView->SetHighColor(kCompatibleSlotColor);
		fView->FillRect(socketFrame);
	}

	if (connection == NULL)
		return;
	// draw wire
	rgb_color lowColor = wire->selected ? kMediumBlueColor : kBorderMediumColor;
	rgb_color highColor = wire->selected ? kLightBlueColor : kBorderLightColor;

	BPoint endPos = position;
	endPos.x -= kSocketSize - 1;
	fView->SetPenSize(3.0);
	fView->SetHighColor(lowColor);
	fView->StrokeLine(position, endPos);

	endPos.x += 1;
	fView->SetPenSize(1.0);
	fView->SetHighColor(highColor);
	fView->StrokeLine(position, endPos);
}


WireLayerItem::WireLayerItem(CustomizableNodeView* parent)
	:
	LayerItem(parent),
	fParent(parent),
	fSelectedWire(NULL)
{
	RebuildWires();
}


WireLayerItem::~WireLayerItem()
{
	
}


void
WireLayerItem::Draw(BRect updateRect)
{
	fView->PushState();

	for (unsigned int i = 0; i < fWires.size(); i++) {
		node_wire* wire = fWires[i];

		BPoint start = wire->ViewPosition();
		BPoint end = wire->endPoint->ViewPosition();
		StrokeWire(fView, start, end, wire->selected, kWireOffset);
	}

	fView->PopState();
}


bool
WireLayerItem::MouseMoved(BPoint point, uint32 transit, const BMessage* message)
{
	return false;
}

									
bool
WireLayerItem::MouseDown(BPoint point)
{
	bool wireClicked = false;
	node_wire* clickedWire = NULL;
	for (unsigned int i = 0; i < fWires.size(); i++) {
		clickedWire = fWires[i];
		wireClicked = fParent->WireContains(clickedWire, point);
		if (wireClicked == true)
			break;
	}

	bool changed = false;
	if (wireClicked)
		changed = SelectWire(clickedWire);
	else
		changed = SelectWire(NULL);
	if (changed)
		fView->Invalidate();

	return false;
}


bool
WireLayerItem::MouseUp(BPoint point)
{
	return false;
}


void
WireLayerItem::RebuildWires()
{
	fWires.clear();

	for (int32 i = 0; i < fView->CountLayers(); i++) {
		CustomizableLayerItem* item = dynamic_cast<CustomizableLayerItem*>(
			fView->LayerAt(i));
		if (item == NULL)
			continue;
		BReference<Customizable> customizable = item->GetCustomizable();
		if (customizable == NULL)
			continue;
		for (int32 a = 0; a < customizable->CountSockets(); a++) {
			Customizable::Socket* socket = customizable->SocketAt(a);
			for (int32 c = 0; c < socket->CountConnections(); c++) {
				Customizable* connection = socket->ConnectionAt(c);
				CustomizableLayerItem* connectionItem = fParent->FindNode(
					connection);
				if (connectionItem == NULL)
					continue;

				node_socket* nodeSocket = item->GetNodeSocket(a, c);
				int32 conectionId = connection->InterfaceIndex(
					socket->Interface());
				node_connection* nodeConnection
					= connectionItem->GetNodeConnection(conectionId);
				if (nodeSocket == NULL || nodeConnection == NULL)
					continue;

				nodeSocket->endPoint = nodeConnection;
				nodeConnection->socket = nodeSocket;
				fWires.push_back(nodeSocket);
			} 
		}
		for (int32 e = 0; e < customizable->CountEvents(); e++) {
			const event_data* data = customizable->EventAt(e);
			for (unsigned int c = 0; c < data->connections.size(); c++) {
				const connection_data& connection = data->connections[c];
				Customizable* target = dynamic_cast<Customizable*>(
					connection.target.Get());
				if (target == NULL)
					continue;
				CustomizableLayerItem* connectionItem = fParent->FindNode(
					target);
				if (connectionItem == NULL)
					continue;

				node_event* event = item->GetNodeEvent(e, c);

				int32 methodId = target->MethodIndex(connection.method);
				node_method* method = connectionItem->GetNodeMethod(methodId);
				if (event == NULL || method == NULL)
					continue;

				event->endPoint = method;
				method->event = event;
				fWires.push_back(event);
			}
		}
	}
}


bool
WireLayerItem::SelectWire(node_wire* wire)
{
	if (fSelectedWire == wire)
		return false;
	if (fSelectedWire != NULL)
		fSelectedWire->selected = false;
	fSelectedWire = wire;
	if (fSelectedWire != NULL)
		fSelectedWire->selected = true;
	return true;
}


bool
WireLayerItem::RemoveWire(node_wire* wire)
{
	for (unsigned int i = 0; i < fWires.size(); i++) {
		node_wire* w = fWires[i];
		if (wire == w) {
			fWires.erase(fWires.begin() + i);
			return true;
		}
	}
	return false;
}


void
WireLayerItem::AddWire(node_wire* wire)
{
	fWires.push_back(wire);
}
