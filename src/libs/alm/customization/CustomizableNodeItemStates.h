/*
 * Copyright 2011, Clemens Zeidler <haiku@clemens-zeidler.de>
 * Distributed under the terms of the MIT License.
 */
#ifndef CUSTOMIZABLE_NODE_ITEM_STATES_H
#define CUSTOMIZABLE_NODE_ITEM_STATES_H


#include <CustomizableNodeView.h>


class SocketConnector {
public:
	SocketConnector(Customizable* source, Customizable::Socket* socket,
		Customizable* target)
		:
		fSource(source),
		fSocket(socket),
		fTarget(target)
	{	
	}

	status_t Connect()
	{
		return fSource->Connect(fSocket, fTarget);
	}

	status_t Disconnect()
	{
		return fSource->Disconnect(fSocket, fTarget);
	}

protected:
	Customizable*				fSource;
	Customizable::Socket*		fSocket;
	Customizable*				fTarget;
};


class ConnectSocketAction : public BALM::CustomizationAction,
	public SocketConnector {
public:
	ConnectSocketAction(Customizable* source, Customizable::Socket* socket,
		Customizable* target)
		:
		SocketConnector(source, socket, target)
	{
	}

	status_t Perform()
	{
		return Connect();
	}

	status_t Undo()
	{
		return Disconnect();
	}
};


class DisconnectSocketAction : public BALM::CustomizationAction,
	public SocketConnector {
public:
	DisconnectSocketAction(Customizable* source, Customizable::Socket* socket,
		Customizable* target)
		:
		SocketConnector(source, socket, target)
	{
	}

	status_t Perform()
	{
		return Disconnect();
	}

	status_t Undo()
	{
		return Connect();
	}
};


class EventConnector {
public:
	EventConnector(Customizable* source, const char* event,
		Customizable* target, const char* method)
		:
		fSource(source),
		fEvent(event),
		fTarget(target),
		fMethod(method)
	{
	}

	status_t Connect()
	{
		return fSource->ConnectEvent(fEvent, fTarget, fMethod);
	}

	status_t Disconnect()
	{
		return fSource->DisconnectEvent(fEvent, fTarget);
	}

protected:
			Customizable*		fSource;
			BString				fEvent;
			Customizable*		fTarget;
			BString				fMethod;
};


class ConnectEventAction : public BALM::CustomizationAction,
	public EventConnector {
public:
	ConnectEventAction(Customizable* source, const char* event,
		Customizable* target, const char* method)
		:
		EventConnector(source, event, target, method)
	{
	}

	status_t Perform()
	{
		return Connect();
	}

	status_t Undo()
	{
		return Disconnect();
	}
};


class DisconnectEventAction : public BALM::CustomizationAction,
	public EventConnector {
public:
	DisconnectEventAction(Customizable* source, const char* event,
		Customizable* target, const char* method)
		:
		EventConnector(source, event, target, method)
	{
	}

	status_t Perform()
	{
		return Disconnect();
	}

	status_t Undo()
	{
		return Connect();
	}
};


static node_connection*
EndpointAt(CustomizableLayerItem* caller, const BPoint& point)
{
	BitmapLayerView* view = caller->View();
	for (int32 i = 0; i < view->CountLayers(); i++) {
		CustomizableLayerItem* item = dynamic_cast<CustomizableLayerItem*>(
			view->LayerAt(i));
		if (item == NULL)
			continue;
		node_connection* connection = item->FindConnectionAt(point);
		if (connection != NULL)
			return connection;
	}
	return NULL;
}


static node_socket*
SocketAt(CustomizableLayerItem* caller, const BPoint& point)
{
	BitmapLayerView* view = caller->View();
	for (int32 i = 0; i < view->CountLayers(); i++) {
		CustomizableLayerItem* item = dynamic_cast<CustomizableLayerItem*>(
			view->LayerAt(i));
		if (item == NULL)
			continue;
		node_socket* socket = item->FindSocketAt(point);
		if (socket != NULL)
			return socket;
	}
	return NULL;
}


static node_event*
EventAt(CustomizableLayerItem* caller, const BPoint& point)
{
	BitmapLayerView* view = caller->View();
	for (int32 i = 0; i < view->CountLayers(); i++) {
		CustomizableLayerItem* item = dynamic_cast<CustomizableLayerItem*>(
			view->LayerAt(i));
		if (item == NULL)
			continue;
		node_event* event = item->FindEventAt(point);
		if (event != NULL)
			return event;
	}
	return NULL;
}


static node_method*
MethodAt(CustomizableLayerItem* caller, const BPoint& point)
{
	BitmapLayerView* view = caller->View();
	for (int32 i = 0; i < view->CountLayers(); i++) {
		CustomizableLayerItem* item = dynamic_cast<CustomizableLayerItem*>(
			view->LayerAt(i));
		if (item == NULL)
			continue;
		node_method* method = item->FindMethodAt(point);
		if (method != NULL)
			return method;
	}
	return NULL;
}


static bool
CompatibleConnection(node_socket* socket, node_connection* connection)
{
	if (connection->node->GetCustomizable()->InterfaceAt(connection->interface)
		== BString(socket->socket->Interface()))
		return true;
	return false;
}


static node_connection*
CompatibleEndpointAt(CustomizableLayerItem* caller, const BPoint& point,
	node_socket* socket)
{
	node_connection* connection = EndpointAt(caller, point);
	if (connection == NULL)
		return NULL;
	if (CompatibleConnection(socket, connection) == false)
		return NULL;
	return connection;
}


static node_socket*
CompatibleSocketAt(CustomizableLayerItem* caller, const BPoint& point,
	node_connection* connection)
{
	node_socket* socket = SocketAt(caller, point);
	if (socket == NULL)
		return NULL;
	if (CompatibleConnection(socket, connection) == false)
		return NULL;
	return socket;
}


static node_event*
CompatibleEventAt(CustomizableLayerItem* caller, const BPoint& point,
	node_method* method)
{
	node_event* event = EventAt(caller, point);
	return event;
}


static node_method*
CompatibleMethodAt(CustomizableLayerItem* caller, const BPoint& point,
	node_event* event)
{
	node_method* method = MethodAt(caller, point);
	return method;
}


class CustomizableLayerItem::LayerItemState {
public:
	LayerItemState(CustomizableLayerItem* item)
		:
		fLayerItem(item)
	{
	}

	virtual						~LayerItemState() {}

	virtual	bool
	MouseDown(BPoint point)
	{
		return fLayerItem->BoxItem::MouseDown(point);
	}

	virtual bool
	MouseUp(BPoint point)
	{
		return fLayerItem->BoxItem::MouseUp(point);
	}

	virtual bool
	MouseMoved(BPoint point, uint32 transit, const BMessage* message)
	{
		return fLayerItem->BoxItem::MouseMoved(point, transit, message);
	}

protected:
			CustomizableLayerItem*	fLayerItem;
};


class CustomizableLayerItem::SocketDragState : public LayerItemState {
public:
	SocketDragState(CustomizableLayerItem* item, node_socket* socket)
		:
		LayerItemState(item),
		fNodeSocket(socket),
		fDragSocket(*socket),
		fFoundSocket(fNodeSocket)
	{
	}

	virtual bool
	MouseDown(BPoint point)
	{
		fLayerItem->fParent->RemoveWire(fNodeSocket);
		fLayerItem->fParent->AddWire(&fDragSocket);

		BPoint position = fLayerItem->RasterFrame().LeftTop();
		position += fNodeSocket->position;
		fDragSocket.node = NULL;
		fDragSocket.position = position;
		fDragSocket.endPoint = fNodeSocket->endPoint;
		
		fNodeSocket->endPoint = NULL;

		fLayerItem->fParent->HighlightFreeSlots(
			static_cast<node_connection*>(fDragSocket.endPoint));

		fLayerItem->fView->Activate(fLayerItem);
		return true;
	}

	virtual bool
	MouseUp(BPoint point)
	{
		fLayerItem->fParent->RemoveWire(&fDragSocket);
		fLayerItem->fParent->AddWire(fNodeSocket);

		if (fFoundSocket != NULL)
			fFoundSocket->highlightDockNow = false;
		if (fFoundSocket == NULL) {
			CustomizationAction* disconnectAction = new DisconnectSocketAction(
				fNodeSocket->node->GetCustomizable(), fNodeSocket->socket,
				fDragSocket.endPoint->node->GetCustomizable());

			fLayerItem->ActionHistory().PerformAction(disconnectAction);

			fLayerItem->DoLayout();
			fDragSocket.endPoint->node->DoLayout();
		} else  if (fFoundSocket != fNodeSocket) {
			CustomizationAction* disconnectAction = new DisconnectSocketAction(
				fNodeSocket->node->GetCustomizable(), fNodeSocket->socket,
				fDragSocket.endPoint->node->GetCustomizable());

			fLayerItem->ActionHistory().PerformAction(disconnectAction);
			
			CustomizationAction* connectAction = new ConnectSocketAction(
				fFoundSocket->node->GetCustomizable(), fFoundSocket->socket,
				fDragSocket.endPoint->node->GetCustomizable());

			fLayerItem->ActionHistory().PerformAction(connectAction);

			fLayerItem->DoLayout();
			fFoundSocket->node->DoLayout();
			fDragSocket.endPoint->node->DoLayout();
		} else
			fNodeSocket->endPoint = fDragSocket.endPoint;

		fLayerItem->SetState(NULL);
		fLayerItem->fParent->HighlightFreeSlots((node_connection*)NULL);
		fLayerItem->fView->Invalidate();
		return true;
	}

	virtual bool
	MouseMoved(BPoint point, uint32 transit, const BMessage* message)
	{
		if (fFoundSocket)
			fFoundSocket->highlightDockNow = false;

		fFoundSocket = CompatibleSocketAt(fLayerItem, point,
			static_cast<node_connection*>(fDragSocket.endPoint));
		if (fFoundSocket)
			fFoundSocket->highlightDockNow = true;

		fDragSocket.position = point;
		fLayerItem->fView->Invalidate();
		return true;
	}

private:
			node_socket*		fNodeSocket;
			node_socket			fDragSocket;
			node_socket*		fFoundSocket;
};


class CustomizableLayerItem::EndpointDragState : public LayerItemState {
public:
	EndpointDragState(CustomizableLayerItem* item, node_connection* con)
		:
		LayerItemState(item),
		fEndpoint(con),
		fDragEndpoint(NULL, -1),
		fFoundEndpoint(fEndpoint)
	{
	}

	virtual bool
	MouseDown(BPoint point)
	{
		BPoint position = fLayerItem->RasterFrame().LeftTop();
		position += fEndpoint->position;
		fDragEndpoint.position = position;
		fDragEndpoint.socket = fEndpoint->socket;

		fEndpoint->socket->endPoint = &fDragEndpoint;
		fEndpoint->socket = NULL;

		fLayerItem->fParent->HighlightFreeSlots(fDragEndpoint.socket);
		fLayerItem->fView->Activate(fLayerItem);
		return true;
	}

	virtual bool
	MouseUp(BPoint point)
	{
		node_socket* socket = fDragEndpoint.socket;
		if (fFoundEndpoint != NULL)
			fFoundEndpoint->highlightDockNow = false;
		if (fFoundEndpoint == NULL) {
			CustomizationAction* disconnectAction = new DisconnectSocketAction(
				socket->node->GetCustomizable(), socket->socket,
				fEndpoint->node->GetCustomizable());

			fLayerItem->ActionHistory().PerformAction(disconnectAction);

			fLayerItem->DoLayout();
			fDragEndpoint.socket->node->DoLayout();
		} else if (fFoundEndpoint != fEndpoint) {
			CustomizationAction* disconnectAction = new DisconnectSocketAction(
				socket->node->GetCustomizable(), socket->socket,
				fEndpoint->node->GetCustomizable());

			fLayerItem->ActionHistory().PerformAction(disconnectAction);

			CustomizationAction* connectAction = new ConnectSocketAction(
				socket->node->GetCustomizable(), socket->socket,
				fFoundEndpoint->node->GetCustomizable());

			fLayerItem->ActionHistory().PerformAction(connectAction);

			fLayerItem->DoLayout();
			fFoundEndpoint->node->DoLayout();
			fDragEndpoint.socket->node->DoLayout();
		} else {
			fEndpoint->socket = fDragEndpoint.socket;
			fEndpoint->socket->endPoint = fEndpoint;
		}

		fLayerItem->SetState(NULL);
		fLayerItem->fParent->HighlightFreeSlots((node_socket*)NULL);
		fLayerItem->fView->Invalidate();
		return true;
	}

	virtual bool
	MouseMoved(BPoint point, uint32 transit, const BMessage* message)
	{
		if (fFoundEndpoint)
			fFoundEndpoint->highlightDockNow = false;
		fFoundEndpoint = CompatibleEndpointAt(fLayerItem, point,
			fDragEndpoint.socket);
		if (fFoundEndpoint)
			fFoundEndpoint->highlightDockNow = true;

		fDragEndpoint.position = point;
		fLayerItem->fView->Invalidate();
		return true;
	}

private:
			node_connection*	fEndpoint;
			node_connection		fDragEndpoint;
			node_connection*	fFoundEndpoint;
};


class CustomizableLayerItem::SocketStartWireState : public LayerItemState {
public:
	SocketStartWireState(CustomizableLayerItem* item, node_socket* socket)
		:
		LayerItemState(item),
		fNodeSocket(socket),
		fEndpoint(NULL, -1),
		fFoundEndpoint(NULL)
	{
	}

	virtual bool
	MouseDown(BPoint point)
	{
		fLayerItem->fParent->AddWire(fNodeSocket);

		fEndpoint.position = point;
		fNodeSocket->endPoint = &fEndpoint;

		fLayerItem->fParent->HighlightFreeSlots(fNodeSocket);

		fLayerItem->fView->Activate(fLayerItem);
		return true;
	}

	virtual bool
	MouseUp(BPoint point)
	{
		fNodeSocket->endPoint = NULL;

		fLayerItem->fParent->RemoveWire(fNodeSocket);

		if (fFoundEndpoint != NULL) {
			fFoundEndpoint->highlightDockNow = false;

			CustomizationAction* connectAction = new ConnectSocketAction(
				fNodeSocket->socket->Parent(), fNodeSocket->socket,
				fFoundEndpoint->node->GetCustomizable());

			fLayerItem->ActionHistory().PerformAction(connectAction);

			fLayerItem->DoLayout();
			fFoundEndpoint->node->DoLayout();
		}

		fLayerItem->SetState(NULL);
		fLayerItem->fParent->HighlightFreeSlots((node_socket*)NULL);
		fLayerItem->fView->Invalidate();
		return true;
	}

	virtual bool
	MouseMoved(BPoint point, uint32 transit, const BMessage* message)
	{
		if (fFoundEndpoint)
			fFoundEndpoint->highlightDockNow = false;
		fFoundEndpoint = CompatibleEndpointAt(fLayerItem, point, fNodeSocket);
		if (fFoundEndpoint)
			fFoundEndpoint->highlightDockNow = true;

		if (fFoundEndpoint && fFoundEndpoint->node == fLayerItem)
			fFoundEndpoint = NULL;

		fEndpoint.position = point;
		fLayerItem->fView->Invalidate();
		return true;
	}

private:
			node_socket*		fNodeSocket;
			node_connection		fEndpoint;
			node_connection*	fFoundEndpoint;
};


class CustomizableLayerItem::EndpointStartWireState : public LayerItemState {
public:
	EndpointStartWireState(CustomizableLayerItem* item, node_connection* con)
		:
		LayerItemState(item),
		fEndpoint(con),
		fSocket(NULL, NULL, -1),
		fFoundSocket(NULL)
	{
		fSocket.selected = true;
	}

	virtual bool
	MouseDown(BPoint point)
	{
		fLayerItem->fParent->AddWire(&fSocket);

		fEndpoint->socket = &fSocket;
		fSocket.position = point;
		fSocket.endPoint = fEndpoint;

		fLayerItem->fParent->HighlightFreeSlots(fEndpoint);

		fLayerItem->fView->Activate(fLayerItem);
		return true;
	}

	virtual bool
	MouseUp(BPoint point)
	{
		fEndpoint->socket = NULL;

		fLayerItem->fParent->RemoveWire(&fSocket);

		if (fFoundSocket != NULL) {
			fFoundSocket->highlightDockNow = false;

			CustomizationAction* connectAction = new ConnectSocketAction(
				fFoundSocket->socket->Parent(), fFoundSocket->socket,
				fEndpoint->node->GetCustomizable());

			fLayerItem->ActionHistory().PerformAction(connectAction);

			fLayerItem->DoLayout();
			fFoundSocket->node->DoLayout();
		}

		fLayerItem->fParent->HighlightFreeSlots((node_connection*)NULL);

		fLayerItem->SetState(NULL);
		fLayerItem->fView->Invalidate();
		return true;
	}

	virtual bool
	MouseMoved(BPoint point, uint32 transit, const BMessage* message)
	{
		if (fFoundSocket)
			fFoundSocket->highlightDockNow = false;
		fFoundSocket = CompatibleSocketAt(fLayerItem, point, fEndpoint);

		if (fFoundSocket && fFoundSocket->node == fLayerItem)
			fFoundSocket = NULL;
		if (fFoundSocket)
			fFoundSocket->highlightDockNow = true;

		fSocket.position = point;
		fLayerItem->fView->Invalidate();
		return true;
	}

private:
			node_connection*	fEndpoint;
			node_socket			fSocket;
			node_socket*		fFoundSocket;
};


class CustomizableLayerItem::EventStartWireState : public LayerItemState {
public:
	EventStartWireState(CustomizableLayerItem* item, node_event* event)
		:
		LayerItemState(item),
		fNodeEvent(event),
		fEndpoint(NULL, -1),
		fFoundEndpoint(NULL)
	{
		fNodeEvent->selected = true;
	}

	virtual bool
	MouseDown(BPoint point)
	{
		fLayerItem->fParent->AddWire(fNodeEvent);

		fEndpoint.position = point;
		fNodeEvent->endPoint = &fEndpoint;

		//fLayerItem->fParent->HighlightFreeSlots(fNodeSocket);

		fLayerItem->fView->Activate(fLayerItem);
		return true;
	}

	virtual bool
	MouseUp(BPoint point)
	{
		fNodeEvent->endPoint = NULL;

		fLayerItem->fParent->RemoveWire(fNodeEvent);

		const event_data* eventData
			= fNodeEvent->node->GetCustomizable()->EventAt(fNodeEvent->eventId);

		if (fFoundEndpoint != NULL) {
			fFoundEndpoint->highlightDockNow = false;
			Customizable* endPointCust = fFoundEndpoint->node->GetCustomizable();
			
			CustomizationAction* action = new ConnectEventAction(
				fNodeEvent->node->GetCustomizable(), eventData->event,
				fFoundEndpoint->node->GetCustomizable(),
				endPointCust->MethodAt(fFoundEndpoint->id)->GetName());
		
			fLayerItem->ActionHistory().PerformAction(action);

			fLayerItem->DoLayout();
			fFoundEndpoint->node->DoLayout();
		}

		fLayerItem->SetState(NULL);
		//fLayerItem->fParent->HighlightFreeSlots((node_socket*)NULL);
		fLayerItem->fView->Invalidate();
		return true;
	}

	virtual bool
	MouseMoved(BPoint point, uint32 transit, const BMessage* message)
	{
		if (fFoundEndpoint)
			fFoundEndpoint->highlightDockNow = false;
		fFoundEndpoint = CompatibleMethodAt(fLayerItem, point, fNodeEvent);
		if (fFoundEndpoint)
			fFoundEndpoint->highlightDockNow = true;

		if (fFoundEndpoint && fFoundEndpoint->node == fLayerItem)
			fFoundEndpoint = NULL;

		fEndpoint.position = point;
		fLayerItem->fView->Invalidate();
		return true;
	}

private:
			node_event*			fNodeEvent;
			node_method			fEndpoint;
			node_method*		fFoundEndpoint;
};


class CustomizableLayerItem::EventDragState : public LayerItemState {
public:
	EventDragState(CustomizableLayerItem* item, node_event* event)
		:
		LayerItemState(item),
		fNodeEvent(event),
		fDragEvent(*event),
		fFoundEvent(fNodeEvent)
	{
	}

	virtual bool
	MouseDown(BPoint point)
	{
		fLayerItem->fParent->RemoveWire(fNodeEvent);
		fLayerItem->fParent->AddWire(&fDragEvent);

		BPoint position = fLayerItem->RasterFrame().LeftTop();
		position += fNodeEvent->position;
		fDragEvent.node = NULL;
		fDragEvent.position = position;
		fDragEvent.endPoint = fNodeEvent->endPoint;
		
		fNodeEvent->endPoint = NULL;

		//fLayerItem->fParent->HighlightFreeSlots(
		//	static_cast<node_connection*>(fDragEvent.endPoint));

		fLayerItem->fView->Activate(fLayerItem);
		return true;
	}

	virtual bool
	MouseUp(BPoint point)
	{
		fLayerItem->fParent->RemoveWire(&fDragEvent);
		fLayerItem->fParent->AddWire(fNodeEvent);

		const event_data* eventData
			= fNodeEvent->node->GetCustomizable()->EventAt(fNodeEvent->eventId);
		node_method* nodeMethod = static_cast<node_method*>(
				fDragEvent.endPoint);

		if (fFoundEvent != NULL)
			fFoundEvent->highlightDockNow = false;
		if (eventData == NULL)
			fFoundEvent->endPoint = fDragEvent.endPoint;
		else if (fFoundEvent == NULL) {
			CustomizationAction* action = new DisconnectEventAction(
				fNodeEvent->node->GetCustomizable(), eventData->event,
				nodeMethod->node->GetCustomizable(),
				nodeMethod->node->GetCustomizable()->MethodAt(
					nodeMethod->id)->GetName());

			fLayerItem->ActionHistory().PerformAction(action);

			fLayerItem->DoLayout();
			fDragEvent.endPoint->node->DoLayout();
		} else  if (fFoundEvent != fNodeEvent) {
			Customizable* endPointCust = nodeMethod->node->GetCustomizable();
			BString method = endPointCust->MethodAt(nodeMethod->id)->GetName();

			CustomizationAction* disconnectAction = new DisconnectEventAction(
				fNodeEvent->node->GetCustomizable(), eventData->event,
				nodeMethod->node->GetCustomizable(),
				nodeMethod->node->GetCustomizable()->MethodAt(
					nodeMethod->id)->GetName());
			CustomizationAction* connectAction = new ConnectEventAction(
				fNodeEvent->node->GetCustomizable(), eventData->event,
				fFoundEvent->node->GetCustomizable(),
				method);

			fLayerItem->ActionHistory().PerformAction(disconnectAction);
			fLayerItem->ActionHistory().PerformAction(connectAction);

			fLayerItem->DoLayout();
			fFoundEvent->node->DoLayout();
			fDragEvent.endPoint->node->DoLayout();
		} else
			fFoundEvent->endPoint = fDragEvent.endPoint;

		fLayerItem->SetState(NULL);
		//fLayerItem->fParent->HighlightFreeSlots((node_connection*)NULL);
		fLayerItem->fView->Invalidate();
		return true;
	}

	virtual bool
	MouseMoved(BPoint point, uint32 transit, const BMessage* message)
	{
		if (fFoundEvent)
			fFoundEvent->highlightDockNow = false;

		fFoundEvent = CompatibleEventAt(fLayerItem, point,
			static_cast<node_method*>(fDragEvent.endPoint));
		if (fFoundEvent)
			fFoundEvent->highlightDockNow = true;

		fDragEvent.position = point;
		fLayerItem->fView->Invalidate();
		return true;
	}

private:
			node_event*		fNodeEvent;
			node_event		fDragEvent;
		
			node_event*		fFoundEvent;
};


class CustomizableLayerItem::MethodStartWireState : public LayerItemState {
public:
	MethodStartWireState(CustomizableLayerItem* item, node_method* method)
		:
		LayerItemState(item),
		fEndpoint(method),
		fEvent(NULL, -1, -1),
		fFoundEvent(NULL)
	{
		fEvent.selected = true;
	}

	virtual bool
	MouseDown(BPoint point)
	{
		fLayerItem->fParent->AddWire(&fEvent);

		fEvent.position = point;
		fEvent.endPoint = fEndpoint;

		//fLayerItem->fParent->HighlightFreeSlots(fNodeSocket);

		fLayerItem->fView->Activate(fLayerItem);
		return true;
	}

	virtual bool
	MouseUp(BPoint point)
	{
		fLayerItem->fParent->RemoveWire(&fEvent);

		if (fFoundEvent != NULL) {
			fFoundEvent->highlightDockNow = false;
			Customizable* methodCust = fEndpoint->node->GetCustomizable();
			const event_data* eventData = fFoundEvent->node
				->GetCustomizable()->EventAt(fFoundEvent->eventId);
			CustomizationAction* connectAction = new ConnectEventAction(
				fFoundEvent->node->GetCustomizable(), eventData->event,
				fEndpoint->node->GetCustomizable(),
				methodCust->MethodAt(fEndpoint->id)->GetName());
	
			fLayerItem->ActionHistory().PerformAction(connectAction);

			fLayerItem->DoLayout();
			fFoundEvent->node->DoLayout();
		}

		fLayerItem->SetState(NULL);
		//fLayerItem->fParent->HighlightFreeSlots((node_socket*)NULL);
		fLayerItem->fView->Invalidate();
		return true;
	}

	virtual bool
	MouseMoved(BPoint point, uint32 transit, const BMessage* message)
	{
		if (fFoundEvent)
			fFoundEvent->highlightDockNow = false;
		fFoundEvent = CompatibleEventAt(fLayerItem, point, fEndpoint);
		if (fFoundEvent)
			fFoundEvent->highlightDockNow = true;

		if (fFoundEvent && fFoundEvent->node == fLayerItem)
			fFoundEvent = NULL;

		fEvent.position = point;
		fLayerItem->fView->Invalidate();
		return true;
	}

private:
			node_method*		fEndpoint;
			node_event			fEvent;
			node_event*			fFoundEvent;
};


class CustomizableLayerItem::MethodDragState : public LayerItemState {
public:
	MethodDragState(CustomizableLayerItem* item, node_method* method)
		:
		LayerItemState(item),
		fNodeMethod(method),
		fDragMethod(NULL, -1),
		fFoundMethod(fNodeMethod)
	{
		fDragMethod.event = fNodeMethod->event;
	}

	virtual bool
	MouseDown(BPoint point)
	{
		BPoint position = fLayerItem->RasterFrame().LeftTop();
		position += fNodeMethod->position;
		fDragMethod.position = position;

		fNodeMethod->event->endPoint = &fDragMethod;

		//fLayerItem->fParent->HighlightFreeSlots(
		//	static_cast<node_connection*>(fDragEvent.endPoint));

		fLayerItem->fView->Activate(fLayerItem);
		return true;
	}

	virtual bool
	MouseUp(BPoint point)
	{
		if (fFoundMethod != NULL)
			fFoundMethod->highlightDockNow = false;
		if (fFoundMethod == NULL) {
			node_event* event = fNodeMethod->event;
			const event_data* eventData = event->node->GetCustomizable()
				->EventAt(event->eventId);

			Customizable* eventCust = event->node->GetCustomizable();
			CustomizationAction* disconnectAction = new DisconnectEventAction(
				eventCust, eventData->event,
				fNodeMethod->node->GetCustomizable(),
				fNodeMethod->node->GetCustomizable()->MethodAt(
					fNodeMethod->id)->GetName());

			fLayerItem->ActionHistory().PerformAction(disconnectAction);

			fLayerItem->DoLayout();
			fDragMethod.event->node->DoLayout();
		} else  if (fFoundMethod != fNodeMethod) {
			node_event* event = fNodeMethod->event;
			const event_data* eventData = event->node->GetCustomizable()
				->EventAt(event->eventId);

			Customizable* eventCust = event->node->GetCustomizable();
			CustomizationAction* disconnectAction = new DisconnectEventAction(
				eventCust, eventData->event,
				fNodeMethod->node->GetCustomizable(),
				fNodeMethod->node->GetCustomizable()->MethodAt(
					fNodeMethod->id)->GetName());

			event = fFoundMethod->event;
			eventCust = event->node->GetCustomizable();
			eventData = event->node->GetCustomizable()->EventAt(event->eventId);
			Customizable* endPointCust = fNodeMethod->node->GetCustomizable();
			BString method = endPointCust->MethodAt(fNodeMethod->id)->GetName();
			CustomizationAction* connectAction = new ConnectEventAction(
				eventCust, eventData->event,
				fNodeMethod->node->GetCustomizable(), method);

			fLayerItem->ActionHistory().PerformAction(disconnectAction);
			fLayerItem->ActionHistory().PerformAction(connectAction);

			fLayerItem->DoLayout();
			fFoundMethod->node->DoLayout();
			fDragMethod.event->node->DoLayout();
		} else
			fNodeMethod->event->endPoint = fNodeMethod;

		fLayerItem->SetState(NULL);
		//fLayerItem->fParent->HighlightFreeSlots((node_connection*)NULL);
		fLayerItem->fView->Invalidate();
		return true;
	}

	virtual bool
	MouseMoved(BPoint point, uint32 transit, const BMessage* message)
	{
		if (fFoundMethod)
			fFoundMethod->highlightDockNow = false;

		fFoundMethod = CompatibleMethodAt(fLayerItem, point, fDragMethod.event);
		if (fFoundMethod)
			fFoundMethod->highlightDockNow = true;

		fDragMethod.position = point;
		fLayerItem->fView->Invalidate();
		return true;
	}

private:
			node_method*	fNodeMethod;
			node_method		fDragMethod;
		
			node_method*	fFoundMethod;
};


#endif // CUSTOMIZABLE_NODE_ITEM_STATES_H
