/*
 * Copyright 2011, Haiku, Inc. All rights reserved.
 * Copyright 2011, Clemens Zeidler <haiku@clemens-zeidler.de>
 * Distributed under the terms of the MIT License.
 */


#include "TextStream.h"

#include <stdio.h>


BTextOutput::~BTextOutput()
{
}


BTextOutput& endl(BTextOutput& stream)
{
	stream.Append("\n");
	return stream;
}


BTextOutput& operator<<(BTextOutput& stream, const char* string)
{
	stream.Append(string);
	return stream;
}


BTextOutput& operator<<(BTextOutput& stream, const BString& string)
{
	stream.Append(string);
	return stream;
}


BTextOutput& operator<<(BTextOutput& stream, char c)
{
	BString buffer;
	buffer << c;
	stream.Append(buffer);
	return stream;
}


BTextOutput& operator<<(BTextOutput& stream, bool value)
{
	BString buffer;
	buffer << value;
	stream.Append(buffer);
	return stream;
}


BTextOutput& operator<<(BTextOutput& stream, int value)
{
	BString buffer;
	buffer << value;
	stream.Append(buffer);
	return stream;
}


BTextOutput& operator<<(BTextOutput& stream, unsigned int value)
{
	BString buffer;
	buffer << value;
	stream.Append(buffer);
	return stream;
}


BTextOutput& operator<<(BTextOutput& stream, unsigned long value)
{
	BString buffer;
	buffer << value;
	stream.Append(buffer);
	return stream;
}


BTextOutput& operator<<(BTextOutput& stream, long value)
{
	BString buffer;
	buffer << value;
	stream.Append(buffer);
	return stream;
}


BTextOutput& operator<<(BTextOutput& stream, unsigned long long value)
{
	BString buffer;
	buffer << value;
	stream.Append(buffer);
	return stream;
}


BTextOutput& operator<<(BTextOutput& stream, long long value)
{
	BString buffer;
	buffer << value;
	stream.Append(buffer);
	return stream;
}


BTextOutput& operator<<(BTextOutput& stream, float value)
{
	BString buffer;
	buffer << value;
	stream.Append(buffer);
	return stream;
}


BTextOutput& operator<<(BTextOutput& stream, double value)
{
	BString buffer;
	buffer << value;
	stream.Append(buffer);
	return stream;
}


BTextOutput& operator<<(BTextOutput& stream, _BTextOutputManipulator function)
{
	return (*function)(stream);
}


void
BStringOutput::Append(const char* string)
{
	fString.Append(string);
}


const BString&
BStringOutput::String()
{
	return fString;
}


void
BStandardOutput::Append(const char* string)
{
	printf("%s", string);
}
