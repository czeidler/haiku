/*
 * Copyright 2011, Clemens Zeidler <haiku@clemens-zeidler.de>
 * Distributed under the terms of the MIT License.
 */
#ifndef	BITMAP_LAYER_VIEW_H
#define	BITMAP_LAYER_VIEW_H


#include <ObjectList.h>

#include <Point.h>
#include <Rect.h>
#include <String.h>
#include <View.h>


class BitmapLayerView;


class LayerItem {
public:
								LayerItem(BitmapLayerView* parent);
	virtual						~LayerItem();

	virtual	void				Draw(BRect updateRect) = 0;

	virtual	bool				MouseMoved(BPoint point, uint32 transit,
									const BMessage* message) = 0;
	virtual	bool				MouseDown(BPoint point) = 0;
	virtual	bool				MouseUp(BPoint point) = 0;

			BitmapLayerView*	View() { return fView; }

			bool				Selected() const;
			void				SetSelected(bool selected);
protected:
	friend class BitmapLayerView;

			void				SetFocus(bool focus);
			bool				Focus();

			BitmapLayerView*	fView;
			bool				fFocus;

			bool				fSelected;
};


typedef BObjectList<LayerItem> LayerList;


class BoxItem;


class BitmapLayerView : public BView {
public:
								BitmapLayerView();

	virtual	bool				AddLayer(LayerItem* layer);
	virtual	bool				RemoveLayer(LayerItem* layer);
			int32				CountLayers() const;
			LayerItem*			LayerAt(int32 index) const;
			bool				MoveLayer(int32 from, int32 to);

			void				MoveToTop(LayerItem* layer);
	virtual void				SetFocus(LayerItem* layer);
			void				Activate(LayerItem* layer);

	virtual	void				Draw(BRect updateRect);

	virtual	void				MouseMoved(BPoint point, uint32 transit,
									const BMessage* message);
	virtual	void				MouseDown(BPoint point);
	virtual	void				MouseUp(BPoint point);

			BRect				RasterFrame(const BRect& frame);
			BPoint				FromRasterPoint(const BPoint& point);

	virtual void				LayerChanged(LayerItem* layer) {}

			void				SetSelectionRect(BRect selection);
			void				DeselectAll();
			void				MoveSelectionBy(const BPoint& offset,
									BoxItem* caller);
private:
			LayerList			fLayers;
			LayerItem*			fFocusItem;

			BRect				fSelectionRect;
			bool				fSelectionStarted;
			BPoint				fSelectionStartPoint;
};


class BoxItem : public LayerItem {
public:
								BoxItem(BitmapLayerView* parent, const char* title);

			void				SetFrame(const BRect& frame);
			BRect				Frame() const;
			BRect				RasterFrame() const;
			void				MoveBy(const BPoint& offset);

			void				SetSize(float width, float heigth);
			void				SetPosition(BPoint position);
			BPoint				Position() const;

			void				SetTitle(const char* title);
			const char*			Title() const;

	virtual	void				Draw(BRect updateRect);

	virtual	bool				MouseMoved(BPoint point, uint32 transit,
									const BMessage* message);
	virtual	bool				MouseDown(BPoint point);
	virtual	bool				MouseUp(BPoint point);

protected:
			BRect				fFrame;
			BString				fTitle;

private:
			void				_DrawTitle(const BRect& boxFrame);

			bool				fDragStarted;
			BPoint				fPrevMousePosition;
};


#endif // BITMAP_LAYER_VIEW_H
