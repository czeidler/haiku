/*
 * Copyright (c) 2005 Palmsource, Inc.
 * 
 * This software is licensed as described in the file LICENSE, which
 * you should have received as part of this distribution. The terms
 * are also available at http://www.openbinder.org/license.html.
 * 
 * This software consists of voluntary contributions made by many
 * individuals. For the exact contribution history, see the revision
 * history and logs, available at http://www.openbinder.org
 */
#ifndef OPENBINDER_OUTPUT_H
#define OPENBINDER_OUTPUT_H


#include <iostream>

#include "OutputBackend.h"

#include "OutputI.h"
#include "OutputCPP.h"
#include "OutputUtil.h"


using namespace std;


class OpenBinderBackend : public OutputBackend {
public:
	OpenBinderBackend(const SString& headerdir, const SString& basehdir,
		const SString& outputdir)
		:
		OutputBackend(headerdir, basehdir, outputdir)
	{

	}

	bool
	WriteOutput(SString& idlFileBase, SVector<InterfaceRec*>& ifvector,
		SVector<IncludeRec>& headers, IDLStruct& result)
	{
		bool system = false;

		// if input was valid, create header and cppfile name
		SString iheader, cppfile, prevdir;
		setpath(idlFileBase, iheader, cppfile, fHeaderDir, fBaseHDir, fOutputDir, prevdir);
		PRINT_PATHS(bout << "idlFileBase: " << idlFileBase << ", iheader: " << iheader
			<< ", cppfile: " << cppfile << ", headerdir: " << fHeaderDir
			<< ", outputdir: " << fOutputDir << ", prevdir: " << prevdir << endl);

		sptr<ITextOutput> stream;
		BVFSFile* file;

		// create interface header
		uint32_t flags =  O_WRONLY | O_CREAT | O_TRUNC;

		file=new BVFSFile(iheader.String(), flags);
		if (!file->IsWritable()) {
			cerr << "pidgen Overwrite failed - could not open " << iheader << " for writing" << endl; 
			return false;
		}
		stream=new BTextOutput(file);

		// A word about include files...
		// The current .h file is specified with prevdir, which uses the leaf of the
		// -S directory (so that the result is "widget/IWidget.h" rather than "IWidget.h"
		// For other header files, if found on search paths, it uses the last directory
		// in the path, so that once again, the result is a "widget/IWidget.h" format.
		// This should probably be controlled with a -include_parent_dir to indicate
		// what you want there.  .idl files outside the normal build directory structures
		// might not fit this pattern.
		// Also, there should be a -system_include or -local_include type option which would
		// dictate the #include using <> or "".
		WriteIHeader(stream, ifvector, prevdir, headers, result.BeginComments(),
			result.EndComments(), system);
		stream=NULL;
		delete file;

		unlink(cppfile.String());
		
		// create cppfile
		file=new BVFSFile(cppfile.String(), flags);
		if (!file->IsWritable()) {
			cerr << "pidgen: Overwrite failed - could not open " << cppfile << " for writing" << endl; 
			return false;
		}
		stream=new BTextOutput(file);

		WriteCPP(stream, ifvector, idlFileBase, prevdir, system);

		stream=NULL;
		delete file;

		return true;
	}

};


#endif // OPENBINDER_OUTPUT_H
