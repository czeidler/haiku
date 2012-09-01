/*
 * Copyright 2011 Haiku, Inc.
 * Distributed under the terms of the MIT License.
 *
 * Authors (in chronological order):
 *		Clemens Zeidler (haiku@Clemens-Zeidler.de)
 */
#ifndef PALADIN_BACKEND_H
#define PALADIN_BACKEND_H


#include <iostream>

#include "OutputBackend.h"

#include <support/TextStream.h>

#include "OutputI.h"
#include "OutputCPP.h"
#include "OutputUtil.h"


using namespace std;

/* Interfaces(); // list of strings
 * AsInterface();
 * 
 * Methods();
 * */


class PaladinBackend : public OutputBackend {
public:
	PaladinBackend(const SString& headerdir, const SString& basehdir,
		const SString& outputdir)
		:
		OutputBackend(headerdir, basehdir, outputdir)
	{

	}

	bool
	WriteOutput(SString& idlFileBase, SVector<InterfaceRec*>& ifvector,
		SVector<IncludeRec>& headers, IDLStruct& result)
	{
		SString iheader, cppfile, prevdir;
		setpath(idlFileBase, iheader, cppfile, fHeaderDir, fBaseHDir, fOutputDir, prevdir);

		sptr<ITextOutput> stream;
		BVFSFile* file;

		uint32 flags =  O_WRONLY | O_CREAT | O_TRUNC;

		file = new BVFSFile(iheader.String(), flags);
		if (!file->IsWritable()) {
			cerr << "can't open header for writing: " << iheader << endl; 
			return false;
		}
		stream = new BTextOutput(file);

		_WriteHeader(stream, ifvector, prevdir, headers, result);
		stream = NULL;
		delete file;

		// create cppfile
		file = new BVFSFile(cppfile.String(), flags);
		if (!file->IsWritable()) {
			cerr << "can't open header for writing: " << cppfile << endl;
			return false;
		}
		stream = new BTextOutput(file);

		_WriteCPP(stream, ifvector, idlFileBase, prevdir);

		stream=NULL;
		delete file;

		return true;
	}
	
protected:
	virtual	bool				_WriteHeader(sptr<ITextOutput> stream,
									SVector<InterfaceRec *> &recs,
									const SString &filename,
									SVector<IncludeRec> & headers,
									IDLStruct& result);

	virtual bool				_WriteCPP(sptr<ITextOutput> stream,
									SVector<InterfaceRec *> &recs,
									const SString & filename,
									const SString & lHeader);
};


#endif // PALADIN_BACKEND_H
