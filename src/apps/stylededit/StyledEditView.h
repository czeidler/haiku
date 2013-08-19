/*
 * Copyright 2002-2007, Haiku, Inc. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Mattias Sundblad
 *		Andrew Bachmann
 *		Axel Dörfler, axeld@pinc-software.de
 */
#ifndef STYLED_EDIT_VIEW_H
#define STYLED_EDIT_VIEW_H


#include <String.h>
#include <TextView.h>


class BFile;
class BHandler;
class BMessenger;
class BPositionIO;

class StyledEditView : public BTextView {
public:
						StyledEditView(BRect viewframe, BRect textframe,
							BHandler* handler);
	virtual				~StyledEditView();

	virtual	void		FrameResized(float width, float height);
	virtual	void		DeleteText(int32 start, int32 finish);
	virtual	void		InsertText(const char* text, int32 length,
							int32 offset,
							const text_run_array* runs = NULL);
	virtual	void		Select(int32 start, int32 finish);

			void		Reset();
			void		SetSuppressChanges(bool suppressChanges);
			status_t	GetStyledText(BPositionIO* stream,
							const char* forceEncoding = NULL);
			status_t	WriteStyledEditFile(BFile* file);

			void 		SetEncoding(uint32 encoding);
			uint32		GetEncoding() const;

private:
			void		_UpdateStatus();

			BMessenger*	fMessenger;
			bool		fSuppressChanges;
			BString		fEncoding;
};


#endif	// STYLED_EDIT_VIEW_H
