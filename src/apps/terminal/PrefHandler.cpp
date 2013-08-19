/*
 * Copyright 2003-2013, Haiku, Inc. All Rights Reserved.
 * Copyright (c) 2004 Daniel Furrer <assimil8or@users.sourceforge.net>
 * Copyright (c) 2003-4 Kian Duffy <myob@users.sourceforge.net>
 * Copyright (c) 1998,99 Kazuho Okui and Takashi Murai.
 *
 * Distributed unter the terms of the MIT License.
 *
 * Authors:
 *		Kian Duffy, myob@users.sourceforge.net
 *		Daniel Furrer, assimil8or@users.sourceforge.net
 *		Siarzhuk Zharski, zharik@gmx.li
 */


#include "PrefHandler.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <Catalog.h>
#include <Directory.h>
#include <Entry.h>
#include <File.h>
#include <FindDirectory.h>
#include <Font.h>
#include <GraphicsDefs.h>
#include <Locale.h>
#include <Message.h>
#include <NodeInfo.h>
#include <Path.h>

#include "TermConst.h"


/*
 * Startup preference settings.
 */
static const pref_defaults kTermDefaults[] = {
	{ PREF_COLS,				"80" },
	{ PREF_ROWS,				"25" },

//	No need for PREF_HALF_FONT_FAMILY/_STYLE defaults here,
//	these entries will be filled with corresponding params
//	of the current system fixed font if they are not
//	available in the settings file

	{ PREF_HALF_FONT_SIZE,		"12" },

	{ PREF_TEXT_FORE_COLOR,		"  0,   0,   0" },
	{ PREF_TEXT_BACK_COLOR,		"255, 255, 255" },
	{ PREF_CURSOR_FORE_COLOR,	"255, 255, 255" },
	{ PREF_CURSOR_BACK_COLOR,	"  0,   0,   0" },
	{ PREF_SELECT_FORE_COLOR,	"255, 255, 255" },
	{ PREF_SELECT_BACK_COLOR,	"  0,   0,   0" },

	{ PREF_IM_FORE_COLOR,		"  0,   0,   0" },
	{ PREF_IM_BACK_COLOR,		"152, 203, 255" },
	{ PREF_IM_SELECT_COLOR,		"255, 152, 152" },

	{ PREF_ANSI_BLACK_COLOR,	" 40,  40,  40" },
	{ PREF_ANSI_RED_COLOR,		"204,   0,   0" },
	{ PREF_ANSI_GREEN_COLOR,	" 78, 154,   6" },
	{ PREF_ANSI_YELLOW_COLOR,	"218, 168,   0" },
	{ PREF_ANSI_BLUE_COLOR,		" 51, 102, 152" },
	{ PREF_ANSI_MAGENTA_COLOR,	"115,  68, 123" },
	{ PREF_ANSI_CYAN_COLOR,		"  6, 152, 154" },
	{ PREF_ANSI_WHITE_COLOR,	"245, 245, 245" },

	{ PREF_ANSI_BLACK_HCOLOR,	"128, 128, 128" },
	{ PREF_ANSI_RED_HCOLOR,		"255,   0,   0" },
	{ PREF_ANSI_GREEN_HCOLOR,	"  0, 255,   0" },
	{ PREF_ANSI_YELLOW_HCOLOR,	"255, 255,   0" },
	{ PREF_ANSI_BLUE_HCOLOR,	"  0,   0, 255" },
	{ PREF_ANSI_MAGENTA_HCOLOR,	"255,   0, 255" },
	{ PREF_ANSI_CYAN_HCOLOR,	"  0, 255, 255" },
	{ PREF_ANSI_WHITE_HCOLOR,	"255, 255, 255" },

	{ PREF_HISTORY_SIZE,		"10000" },

	{ PREF_TEXT_ENCODING,		"UTF-8" },

	{ PREF_IM_AWARE,			"0"},

	{ PREF_TAB_TITLE,			"%1d: %p" },
	{ PREF_WINDOW_TITLE,		"%T %i: %t" },
	{ PREF_BLINK_CURSOR,		PREF_TRUE },
	{ PREF_WARN_ON_EXIT,		PREF_TRUE },
	{ PREF_CURSOR_STYLE,		PREF_BLOCK_CURSOR },
	{ PREF_EMULATE_BOLD,		PREF_FALSE },

	{ NULL, NULL},
};


PrefHandler *PrefHandler::sPrefHandler = NULL;


PrefHandler::PrefHandler()
	:
	fContainer('Pref')
{
	_LoadFromDefault(kTermDefaults);

	BPath path;
	GetDefaultPath(path);
	OpenText(path.Path());

	_ConfirmFont(be_fixed_font);
}


PrefHandler::PrefHandler(const PrefHandler* p)
{
	fContainer = p->fContainer;
}


PrefHandler::~PrefHandler()
{
}


/* static */
PrefHandler *
PrefHandler::Default()
{
	if (sPrefHandler == NULL)
		sPrefHandler = new PrefHandler();
	return sPrefHandler;
}


/* static */
void
PrefHandler::DeleteDefault()
{
	delete sPrefHandler;
	sPrefHandler = NULL;
}


/* static */
void
PrefHandler::SetDefault(PrefHandler *prefHandler)
{
	DeleteDefault();
	sPrefHandler = prefHandler;
}


/* static */
status_t
PrefHandler::GetDefaultPath(BPath& path)
{
	status_t status;
	status = find_directory(B_USER_SETTINGS_DIRECTORY, &path, true);
	if (status != B_OK)
		return status;

	status = path.Append("Terminal");
	if (status != B_OK)
		return status;

	// Just create the directory. Harmless if already there
	status = create_directory(path.Path(), 0755);
	if (status != B_OK)
		return status;

	return path.Append("Default");
}


status_t
PrefHandler::OpenText(const char *path)
{
	return _LoadFromTextFile(path);
}


void
PrefHandler::SaveDefaultAsText()
{
	BPath path;
	if (GetDefaultPath(path) == B_OK)
		SaveAsText(path.Path(), PREFFILE_MIMETYPE);
}


void
PrefHandler::SaveAsText(const char *path, const char *mimetype,
	const char *signature)
{
	// make sure the target path exists
#if 0
	BPath directoryPath(path);
	if (directoryPath.GetParent(&directoryPath) == B_OK)
		create_directory(directoryPath.Path(), 0755);
#endif

	BFile file(path, B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
	char buffer[512];
	type_code type;
	const char *key;

	for (int32 i = 0;
#ifdef B_BEOS_VERSION_DANO
			fContainer.GetInfo(B_STRING_TYPE, i, &key, &type) == B_OK;
#else
			fContainer.GetInfo(B_STRING_TYPE, i, (char**)&key, &type) == B_OK;
#endif
			i++) {
		int len = snprintf(buffer, sizeof(buffer), "\"%s\" , \"%s\"\n",
				key, getString(key));
		file.Write(buffer, len);
	}

	if (mimetype != NULL) {
		BNodeInfo info(&file);
		info.SetType(mimetype);
		info.SetPreferredApp(signature);
	}
}


int32
PrefHandler::getInt32(const char *key)
{
	const char *value = fContainer.FindString(key);
	if (value == NULL)
		return 0;

	return atoi(value);
}


float
PrefHandler::getFloat(const char *key)
{
	const char *value = fContainer.FindString(key);
	if (value == NULL)
		return 0;

	return atof(value);
}


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "Terminal getString"

const char*
PrefHandler::getString(const char *key)
{
	const char *buffer;
	if (fContainer.FindString(key, &buffer) != B_OK)
		buffer = B_TRANSLATE("Error!");

	//printf("%x GET %s: %s\n", this, key, buf);
	return buffer;
}


bool
PrefHandler::getBool(const char *key)
{
	const char *value = fContainer.FindString(key);
	if (value == NULL)
		return false;

	return strcmp(value, PREF_TRUE) == 0;
}


int
PrefHandler::getCursor(const char *key)
{
	const char *value = fContainer.FindString(key);
	if (value != NULL && strcmp(value, PREF_BLOCK_CURSOR) != 0) {
		if (strcmp(value, PREF_UNDERLINE_CURSOR) == 0)
			return UNDERLINE_CURSOR;
		if (strcmp(value, PREF_IBEAM_CURSOR) == 0)
			return IBEAM_CURSOR;
	}
	return BLOCK_CURSOR;
}


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "Terminal getRGB"

/** Returns RGB data from given key. */

rgb_color
PrefHandler::getRGB(const char *key)
{
	rgb_color col;
	int r, g, b;

	if (const char *s = fContainer.FindString(key)) {
		sscanf(s, "%d, %d, %d", &r, &g, &b);
	} else {
		fprintf(stderr,
			"PrefHandler::getRGB(%s) - key not found\n", key);
		r = g = b = 0;
	}

	col.red = r;
	col.green = g;
	col.blue = b;
	col.alpha = 255;
	return col;
}


/** Setting Int32 data with key. */

void
PrefHandler::setInt32(const char *key, int32 data)
{
	char buffer[32];
	snprintf(buffer, sizeof(buffer), "%d", (int)data);
	setString(key, buffer);
}


/** Setting Float data with key */

void
PrefHandler::setFloat(const char *key, float data)
{
	char buffer[32];
	snprintf(buffer, sizeof(buffer), "%g", data);
	setString(key, buffer);
}


/** Setting Bool data with key */

void
PrefHandler::setBool(const char *key, bool data)
{
	if (data)
		setString(key, PREF_TRUE);
	else
		setString(key, PREF_FALSE);
}


/** Setting CString data with key */

void
PrefHandler::setString(const char *key, const char *data)
{
	//printf("%x SET %s: %s\n", this, key, data);
	fContainer.RemoveName(key);
	fContainer.AddString(key, data);
}


/** Setting RGB data with key */

void
PrefHandler::setRGB(const char *key, const rgb_color data)
{
	char buffer[32];
	snprintf(buffer, sizeof(buffer), "%d, %d, %d", data.red, data.green, data.blue);
	setString(key, buffer);
}


/** Check any peference stored or not. */

bool
PrefHandler::IsEmpty() const
{
	return fContainer.IsEmpty();
}


void
PrefHandler::_ConfirmFont(const BFont *fallbackFont)
{
	font_family family;
	font_style style;

	const char *prefFamily = getString(PREF_HALF_FONT_FAMILY);
	int32 familiesCount = (prefFamily != NULL) ? count_font_families() : 0;

	for (int32 i = 0; i < familiesCount; i++) {
		if (get_font_family(i, &family) != B_OK
			|| strcmp(family, prefFamily) != 0)
			continue;

		const char *prefStyle = getString(PREF_HALF_FONT_STYLE);
		int32 stylesCount = (prefStyle != NULL) ? count_font_styles(family) : 0;

		for (int32 j = 0; j < stylesCount; j++) {
			// check style if we can safely use this font
			if (get_font_style(family, j, &style) == B_OK
				&& strcmp(style, prefStyle) == 0)
				return;
		}
	}

	// use fall-back font
	fallbackFont->GetFamilyAndStyle(&family, &style);
	setString(PREF_HALF_FONT_FAMILY, family);
	setString(PREF_HALF_FONT_STYLE, style);
}


status_t
PrefHandler::_LoadFromDefault(const pref_defaults* defaults)
{
	if (defaults == NULL)
		return B_ERROR;

	while (defaults->key) {
		setString(defaults->key, defaults->item);
		++defaults;
	}

	return B_OK;
}


/**	Text is "key","Content"
 *	Comment : Start with '#'
 */

status_t
PrefHandler::_LoadFromTextFile(const char * path)
{
	char buffer[1024];
	char key[B_FIELD_NAME_LENGTH], data[512];
	int n;
	FILE *file;

	file = fopen(path, "r");
	if (file == NULL)
		return B_ENTRY_NOT_FOUND;

	while (fgets(buffer, sizeof(buffer), file) != NULL) {
		if (*buffer == '#')
			continue;

		n = sscanf(buffer, "%*[\"]%[^\"]%*[\"]%*[^\"]%*[\"]%[^\"]", key, data);
		if (n == 2)
			setString(key, data);
	}

	fclose(file);
	return B_OK;
}
