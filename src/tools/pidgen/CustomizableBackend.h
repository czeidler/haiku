/*
 * Copyright 2011 Haiku, Inc.
 * Distributed under the terms of the MIT License.
 *
 * Authors (in chronological order):
 *		Clemens Zeidler (haiku@Clemens-Zeidler.de)
 */
#ifndef CUSTOMIZABLE_BACKEND_H
#define CUSTOMIZABLE_BACKEND_H


#include <iostream>

#include "PaladinBackend.h"

#include <support/TextStream.h>

#include "OutputI.h"
#include "OutputCPP.h"
#include "OutputUtil.h"


using namespace std;


class CustomizableBackend : public PaladinBackend {
public:
	CustomizableBackend(const SString& headerdir, const SString& basehdir,
		const SString& outputdir)
		:
		PaladinBackend(headerdir, basehdir, outputdir)
	{

	}

	
protected:
			bool				_WriteHeader(sptr<ITextOutput> stream,
									SVector<InterfaceRec *> &recs,
									const SString &filename,
									SVector<IncludeRec> & headers,
									IDLStruct& result);

			bool				_WriteCPP(sptr<ITextOutput> stream,
									SVector<InterfaceRec *> &recs,
									const SString & filename,
									const SString & lHeader);
};


#endif // CUSTOMIZABLE_BACKEND_H
