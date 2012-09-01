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

#ifndef CPP_OUTPUT_UTIL_H
#define CPP_OUTPUT_UTIL_H

#include "InterfaceRec.h"

#include <ctype.h>

#include <support/ITextStream.h>


enum {
	MODIFIER_TABS = 2,
	TYPE_TABS = 5,
	TOTAL_TABS = 8,
	PARM_TABS = 9
};


extern sptr<IDLType> FindType(const sptr<IDLType>& typeptr);	// in main.cpp


static void 
WritePropertyDeclarations(sptr<ITextOutput> stream, sptr<IDLNameType> propertyType, bool isAbstract)
{
	// Write both get/set property method declarations into a class defintion
	// append "= 0" if method should be abstract
	
	// The associated comment goes with both the get and set method
	stream << dedent;
	propertyType->OutputComment(stream);
	stream << indent;
	SString type = TypeToCPPType(kInsideClassScope, propertyType->m_type, false);
	stream << "virtual\t" << type << PadString(type, TYPE_TABS) << (char) toupper(propertyType->m_id.ByteAt(0)) << propertyType->m_id.String()+1 << "() const";
	if (isAbstract) {
		stream << " = 0";
	}
	stream << ";" << endl;

	if (propertyType->m_type->HasAttribute(kReadOnly) == false) {	
		type = TypeToCPPType(kInsideClassScope, propertyType->m_type, true);
		stream << dedent;
		propertyType->OutputComment(stream);
		stream << indent;
		stream << "virtual\tvoid\t\t\t\t\tSet" << (char) toupper(propertyType->m_id.ByteAt(0)) << propertyType->m_id.String()+1 << "(" << type << " value)";
		if (isAbstract) {
			stream << " = 0";
		}
		stream << ";" << endl;
	}
}


static void
WriteMethodDeclaration(sptr<ITextOutput> stream, sptr<IDLMethod> method, bool inInterface)
{
	sptr<IDLType> returnType = method->ReturnType();
	SString type = TypeToCPPType(kInsideClassScope, returnType, false);
	stream << dedent;
	method->OutputComment(stream);	
	stream << indent;
	stream << "virtual\t" << type << PadString(type, TYPE_TABS) << method->ID() << "(";
	
	// if the parameters have comments, then we will have newlines and this indenting will
	// have an effect, normally it will be ignored...
	for (int32_t i = 0; i < PARM_TABS; i++) {
		stream << indent;
	}

	int32_t paramCount = method->CountParams();
	for (int32_t i_param = 0; i_param < paramCount; i_param++) {
		sptr<IDLNameType> nt = method->ParamAt(i_param);
		// start off with the comment (if it is a line comment, it will end with a newline)
		if (inInterface && nt->HasComment()) {
			stream << endl;
			nt->OutputComment(stream);
		}
		uint32_t direction = nt->m_type->GetDirection();
		bool isOptional = nt->m_type->HasAttribute(kOptional);
		if ((direction == kInOut) || (direction == kOut)) {	
			type = TypeToCPPType(kInsideClassScope, nt->m_type, false);
			// do we want to distinguish array types here?
			// if so, we would need to lookup type and check for B_VARIABLE_ARRAY_TYPE
			sptr<IDLType> bankType = FindType(nt->m_type);
			stream << type;
			if (type == "BString")
				stream << "& ";
			else
				stream << "* ";
			stream << nt->m_id;	
			if (isOptional) {
				stream << " = NULL";
			}
		}
		else { 
			SString type = TypeToCPPType(kInsideClassScope, nt->m_type, true);
			if (type == "BString")
				stream << "const " << type << "&";
			else
				stream << type;
			stream << " " << nt->m_id;
			if (isOptional) {
				stream << " = " << TypeToDefaultValue(kInsideClassScope, nt->m_type);
			}
		}
		
		// end the parameter with comma (if not the last)
		if (i_param < paramCount-1) {
			stream << ", ";
		}
	}

	if (inInterface && method->HasTrailingComment()) {
		stream << endl;
		method->OutputTrailingComment(stream);
	}
	stream << ")";
	if (method->IsConst()) {
		stream << " const";
	}
	// if the method declaration is inside the interface, then
	// it is abstract also
	if (inInterface) {
		stream << " = 0";
	}

	// clean up our comment indenting
	for (int32_t i = 0; i < PARM_TABS; i++) {
		stream << dedent;
	}

	stream << ";" << endl;
}


static void
WriteEventDeclaration(sptr<ITextOutput> stream, sptr<IDLEvent> event)
{
	stream << indent;
	event->OutputComment(stream);
	stream << dedent;
	stream << "\t\tvoid" << PadString(SString("void"), TYPE_TABS)
		<< "Send" << event->ID() << "(";
				
	int32_t paramCount = event->CountParams();
	// if the parameters have comments, then we will have newlines and this indenting will
	// have an effect, normally it will be ignored...
	for (int32_t i = 0; i < PARM_TABS; i++) {
		stream << indent;
	}
	for (int32_t i_param = 0; i_param < paramCount; i_param++) {
		sptr<IDLNameType> nt = event->ParamAt(i_param);
		if (nt->HasComment()) {
		stream << endl;
		nt->OutputComment(stream);
		}
		SString type = TypeToCPPType(kInsideClassScope, nt->m_type, true);
		if (type == "BString")
			stream << "const " << type << "&";
		else
			stream << type;
		stream << " " << nt->m_id;
		if (i_param < paramCount-1) {
			stream << ", ";
		}
	}
	if (event->HasTrailingComment()) {
		stream << endl;
		event->OutputTrailingComment(stream);
	}
	stream << ");" << endl;
	// clean up our comment indenting
	for (int32_t i = 0; i < PARM_TABS; i++) {
		stream << dedent;
	}
}


#endif //CPP_OUTPUT_UTIL_H
