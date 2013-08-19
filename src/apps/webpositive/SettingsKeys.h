/*
 * Copyright (C) 2010 Stephan Aßmus <superstippi@gmx.de>
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *	notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *	notice, this list of conditions and the following disclaimer in the
 *	documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE COMPUTER, INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE COMPUTER, INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#ifndef SETTINGS_KEYS_H
#define SETTINGS_KEYS_H

#include <SupportDefs.h>


extern const char* kSettingsKeyDownloadPath;
extern const char* kSettingsKeyShowTabsIfSinglePageOpen;
extern const char* kSettingsKeyAutoHideInterfaceInFullscreenMode;
extern const char* kSettingsKeyAutoHidePointer;
extern const char* kSettingsKeyShowHomeButton;

extern const char* kSettingsKeyNewWindowPolicy;
extern const char* kSettingsKeyNewTabPolicy;
extern const char* kSettingsKeyStartPageURL;
extern const char* kSettingsKeySearchPageURL;

extern const char* kDefaultDownloadPath;
extern const char* kDefaultStartPageURL;
extern const char* kDefaultSearchPageURL;

extern const char* kSettingsKeyUseProxy;
extern const char* kSettingsKeyProxyAddress;
extern const char* kSettingsKeyProxyPort;
extern const char* kSettingsKeyUseProxyAuth;
extern const char* kSettingsKeyProxyUsername;
extern const char* kSettingsKeyProxyPassword;

#endif	// SETTINGS_KEYS_H
