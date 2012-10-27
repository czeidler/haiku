/*
 * Copyright 2011, Clemens Zeidler <haiku@clemens-zeidler.de>
 * Distributed under the terms of the MIT License.
 */
#ifndef	CUSTOMIZABLE_NODE_VIEW_H
#define	CUSTOMIZABLE_NODE_VIEW_H


#include <Actions.h>
#include <BitmapLayerView.h>
#include <Customizable.h>
#include <CustomizableRoster.h>


const int32 kCustomizableSelected = '&CuS';


class CustomizableLayerItem;
class WireLayerItem;


struct node_connection;


struct node_slot {
							node_slot(CustomizableLayerItem* _node);

	/*! Returns position in view coordinates. If the node is NULL it means the
	endpoint is dragged arround. In this case the position is returned*/
	BPoint					ViewPosition();

	CustomizableLayerItem*	node;
	BPoint					position;
	bool					highlightCompatible;
	bool					highlightDockNow;
};


struct node_wire : public node_slot {
							node_wire(CustomizableLayerItem* _node);

	bool					selected;
	node_slot*				endPoint;
};


struct node_socket : public node_wire {
							node_socket(CustomizableLayerItem* _node,
								Customizable::Socket* _socket,
								int32 _connection);

	Customizable::Socket*	socket;
	int32					connection;
};


struct node_connection : public node_slot {
							node_connection(CustomizableLayerItem* _connection, 
								int32 _interface);

	int32					interface;
	node_socket*			socket;
};


struct node_method;


struct node_event : public node_wire {
							node_event(CustomizableLayerItem* _item, 
								int32 event, int32 _connection);
	int32					eventId;
	int32					connection;
};


struct node_method : public node_slot {
							node_method(CustomizableLayerItem* _connection, 
								int32 method);
	int32					id;
	node_event*				event;
};


class CustomizableNodeView : public BitmapLayerView {
public:
								CustomizableNodeView(CustomizableRoster* roster);
	virtual						~CustomizableNodeView();

			void				StoreLayout(BMessage* message) const;
			void				RestoreLayout(const BMessage* message);

	virtual void				MessageReceived(BMessage* message);
	virtual	void				AttachedToWindow();

	virtual bool				AddLayer(LayerItem* layer);
	virtual	bool				RemoveLayer(LayerItem* layer);

			CustomizableLayerItem*	FindNode(Customizable* customizable) const;

			void				SelectWire(node_wire* wire);
			//! Caller becomes owner of the wire
			bool				RemoveWire(node_wire* wire);
			void				AddWire(node_wire* wire);
			bool				WireContains(node_wire* wire,
									const BPoint& point) const;

			void				HighlightFreeSlots(node_socket* socket);
			void				HighlightFreeSlots(node_connection* connection);

			BALM::ActionHistory&	ActionHistory() { return fActionHistory; }

	class AddComponentAction;
	class RemoveComponentAction;
	friend class AddComponentAction;
	friend class RemoveComponentAction;
private:
			void				_LoadCustomizables();
			void				_HightlightCustomizables(BMessage* message);

			CustomizableRoster* fRoster;
			WireLayerItem*		fWireLayerItem;

			BALM::ActionHistory	fActionHistory;
};


class CustomizableLayerItem : public BoxItem {
public:
								CustomizableLayerItem(CustomizableNodeView* parent,
									Customizable* customizable,
									BPoint position);
	virtual						~CustomizableLayerItem();

	virtual	void				Draw(BRect updateRect);

	virtual	bool				MouseMoved(BPoint point, uint32 transit,
									const BMessage* message);
	virtual	bool				MouseDown(BPoint point);
	virtual	bool				MouseUp(BPoint point);

			bool				GetSocketPosition(int32 socket,
									int32 connection, BPoint& position);
			node_socket*		FindSocketAt(BPoint position);

			bool				GetConnectionPosition(int32 interface,
									BPoint& position);
			node_connection*	FindConnectionAt(BPoint position);

			bool				GetEventPosition(int32 event, int32 connection,
									BPoint& position);
			node_event*			FindEventAt(BPoint position);

			bool				GetMethodPosition(int32 method,
									BPoint& position);
			node_method*		FindMethodAt(BPoint position);

			node_socket*		GetNodeSocket(int32 socket, int32 connection);
			node_connection*	GetNodeConnection(int32 connection);
			node_event*			GetNodeEvent(int32 event, int32 connection);
			node_method*		GetNodeMethod(int32 method);

			BReference<Customizable>	GetCustomizable() const;

			CustomizableNodeView*	NodeView() { return fParent; }
			BALM::ActionHistory&	ActionHistory()
										{ return fParent->ActionHistory(); }

			void				HighlightFreeSlots(node_socket* socket);
			void				HighlightFreeSlots(node_connection* connection);

protected:
	class LayerItemState;
	class SocketDragState;
	class EndpointDragState;
	class SocketStartWireState;
	class EndpointStartWireState;
	class EventStartWireState;
	class EventDragState;
	class MethodStartWireState;
	class MethodDragState;
	friend class SocketDragState;
	friend class EndpointDragState;
	friend class SocketStartWireState;
	friend class EndpointStartWireState;
	friend class EventStartWireState;
	friend class EventDragState;
	friend class MethodStartWireState;
	friend class MethodDragState;

			void				SetState(LayerItemState* state);

			void				DoLayout();

			void				DrawLeftSocket(BPoint position,
									node_slot* connection, node_wire* wire);
			void				DrawRightSocket(BPoint position,
									node_wire* wire, node_slot* connection);

			float				fLineHeight;

			CustomizableNodeView*	fParent;
			BWeakReference<Customizable>	fCustomizable;
			std::vector<node_socket>		fSocketList;
			std::vector<node_connection>	fConnectionList;

			std::vector<node_event>		fEventList;
			std::vector<node_method>	fMethodList;

			LayerItemState*		fState;

private:
			void				_DrawString(BRect& frame, BString& string,
									BPoint& position, float stringWidth);
			void				_NotifySelected();
};


class WireLayerItem : public LayerItem {
public:
								WireLayerItem(CustomizableNodeView* parent);
	virtual						~WireLayerItem();

	virtual	void				Draw(BRect updateRect);

	virtual	bool				MouseMoved(BPoint point, uint32 transit,
									const BMessage* message);
	virtual	bool				MouseDown(BPoint point);
	virtual	bool				MouseUp(BPoint point);

			void				RebuildWires();
			bool				SelectWire(node_wire* wire);

			bool				RemoveWire(node_wire* wire);
			void				AddWire(node_wire* wire);

private:
			CustomizableNodeView*	fParent;
			std::vector<node_wire*>	fWires;
			node_wire*			fSelectedWire;
};


#endif // CUSTOMIZABLE_NODE_VIEW_H
