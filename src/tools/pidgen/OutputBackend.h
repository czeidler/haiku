/*
 * Copyright 2011 Haiku, Inc.
 * Distributed under the terms of the MIT License.
 *
 * Authors (in chronological order):
 *		Clemens Zeidler (haiku@Clemens-Zeidler.de)
 */
#ifndef OUTPUT_BACKEND_H
#define OUTPUT_BACKEND_H


#include <support/String.h>
#include <support/Vector.h>

#include "idlstruct.h"
#include "InterfaceRec.h"


#define PRINT_PATHS(x) // x


//idl file, name of new header, name of new cpp file, specified headerdir, specified outputdir
static status_t
setpath(
	SString& filebase, SString& iheader,
	SString& cppfile, SString& header, const SString& basehdir,
	SString& output, SString& prevdir)
{
	SString origbase = filebase;

	int32_t pos = -1;

	filebase = filebase.PathLeaf();
	PRINT_PATHS(bout << "Initial filebase: " << filebase << endl);
	
	// no path is given in association with the idl file
	if (output == "") {
		if (pos>=0) {
			origbase.PathGetParent(&output);
		}
		else {
			output.PathSetTo(".");
		}
	}
	if (filebase.Length()<=0) {
		return B_ERROR;// usage();
	}
		
	pos=filebase.FindLast('.');
	if (pos>=0) {
		if (0==strcmp(filebase.String()+pos, ".idl")) {
			filebase.Truncate(pos);
		}
	}
	PRINT_PATHS(bout << "truncated filebase=" << filebase << endl);

	// if no header dir specified, default is output dir for .h
	if (header != "") {
		iheader.PathSetTo(header);
	}
	else {
		iheader.PathSetTo(output); 
	}

	iheader.PathAppend(filebase);
	iheader.Append(".h");

	cppfile.PathSetTo(output);
	cppfile.PathAppend(filebase);
	cppfile.Append(".cpp");

	PRINT_PATHS(bout << "Header : " << header << endl);
	PRINT_PATHS(bout << "I Header : " << iheader << endl);
	PRINT_PATHS(bout << "CPP File : " << cppfile << endl);

	/*if (basehdir == "") {
		// Try to infer the path to the header -- just assume it is
		// one level deep.
		prevdir.PathSetTo(SString(header.PathLeaf()));
	} else {
		// Strip 'basehdir' off the front of the full header path.
		int32_t p = header.FindFirst(basehdir);
		if (p == 0) {
			header.CopyInto(prevdir, basehdir.Length(), header.Length()-basehdir.Length());
			if (prevdir.FindFirst(SString::PathDelimiter()) == 0) {
				prevdir.Remove(0, strlen(SString::PathDelimiter()));
			}
		}
	}*/

	PRINT_PATHS(bout << "prevdir=" << prevdir << endl);
	prevdir.PathAppend(SString(iheader.PathLeaf()));
	PRINT_PATHS(bout << "printableIHeader=" << prevdir << endl);

	return B_OK;
}


class OutputBackend {
public:
	OutputBackend(const SString& headerdir, const SString& basehdir,
		const SString& outputdir)
		:
		fHeaderDir(headerdir),
		fBaseHDir(basehdir),
		fOutputDir(outputdir)
	{

	}

	virtual						~OutputBackend() {}

	virtual	bool				WriteOutput(SString& idlFileBase,
									SVector<InterfaceRec*>& ifvector,
									SVector<IncludeRec>& headers,
									IDLStruct& result) = 0;

protected:
			SString				fHeaderDir;
			SString				fBaseHDir;
			SString				fOutputDir;

};


#endif // OUTPUT_BACKEND_H
