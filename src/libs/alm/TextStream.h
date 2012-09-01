/*
 * Copyright 2012, Haiku Inc. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef	TEXT_STREAM_H
#define	TEXT_STREAM_H


#include <String.h>


class BTextOutput {
public:
	virtual						~BTextOutput();

	virtual void				Append(const char* string) = 0;
};


typedef BTextOutput& (*_BTextOutputManipulator)(BTextOutput&);
BTextOutput& endl(BTextOutput& stream);

BTextOutput& operator<<(BTextOutput& stream, const char* string);
BTextOutput& operator<<(BTextOutput& stream, const BString& string);
BTextOutput& operator<<(BTextOutput& stream, char c);
BTextOutput& operator<<(BTextOutput& stream, bool value);
BTextOutput& operator<<(BTextOutput& stream, int value);
BTextOutput& operator<<(BTextOutput& stream, unsigned int value);
BTextOutput& operator<<(BTextOutput& stream, unsigned long value);
BTextOutput& operator<<(BTextOutput& stream, long value);
BTextOutput& operator<<(BTextOutput& stream, unsigned long long value);
BTextOutput& operator<<(BTextOutput& stream, long long value);
// float/double output hardcodes %.2f style formatting
BTextOutput& operator<<(BTextOutput& stream, float value);
BTextOutput& operator<<(BTextOutput& stream, double value);
BTextOutput& operator<<(BTextOutput& stream, _BTextOutputManipulator function);


class BStringOutput : public BTextOutput {
public:
	virtual void			Append(const char* string);

	const	BString&		String();
private:
			BString			fString;
};


class BStandardOutput : public BTextOutput {
public:
	virtual void			Append(const char* string);
};


#endif // TEXT_STREAM_H
