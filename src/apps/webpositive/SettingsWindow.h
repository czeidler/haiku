/*
 * Copyright (C) 2010 Stephan Aßmus <superstippi@gmx.de>
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
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
#ifndef SETTINGS_WINDOW_H
#define SETTINGS_WINDOW_H

#include <Window.h>

class BButton;
class BCheckBox;
class BMenu;
class BMenuField;
class BMenuItem;
class BTextControl;
class FontSelectionView;
class SettingsMessage;


class SettingsWindow : public BWindow {
public:
								SettingsWindow(BRect frame,
									SettingsMessage* settings);
	virtual						~SettingsWindow();

	virtual	void				MessageReceived(BMessage* message);
	virtual	bool				QuitRequested();

	virtual	void				Show();

private:
			BView*				_CreateGeneralPage(float spacing);
			BView*				_CreateFontsPage(float spacing);
			BView*				_CreateProxyPage(float spacing);
			void				_BuildSizesMenu(BMenu* menu,
									uint32 messageWhat);
			void				_SetupFontSelectionView(
									FontSelectionView* view,
									BMessage* message);

			bool				_CanApplySettings() const;
			void				_ApplySettings();
			void				_RevertSettings();
			void				_ValidateControlsEnabledStatus();

			uint32				_NewWindowPolicy() const;
			uint32				_NewTabPolicy() const;
			int32				_MaxHistoryAge() const;

			void				_SetSizesMenuValue(BMenu* menu, int32 value);
			int32				_SizesMenuValue(BMenu* menu) const;

			BFont				_FindDefaultSerifFont() const;

			uint32				_ProxyPort() const;

private:
			SettingsMessage*	fSettings;

			BTextControl*		fStartPageControl;
			BTextControl*		fSearchPageControl;
			BTextControl*		fDownloadFolderControl;

			BMenuField*			fNewWindowBehaviorMenu;
			BMenuItem*			fNewWindowBehaviorOpenHomeItem;
			BMenuItem*			fNewWindowBehaviorOpenSearchItem;
			BMenuItem*			fNewWindowBehaviorOpenBlankItem;

			BMenuField*			fNewTabBehaviorMenu;
			BMenuItem*			fNewTabBehaviorCloneCurrentItem;
			BMenuItem*			fNewTabBehaviorOpenHomeItem;
			BMenuItem*			fNewTabBehaviorOpenSearchItem;
			BMenuItem*			fNewTabBehaviorOpenBlankItem;

			BTextControl*		fDaysInHistoryMenuControl;
			BCheckBox*			fShowTabsIfOnlyOnePage;
			BCheckBox*			fAutoHideInterfaceInFullscreenMode;
			BCheckBox*			fAutoHidePointer;
			BCheckBox*			fShowHomeButton;

			FontSelectionView*	fStandardFontView;
			FontSelectionView*	fSerifFontView;
			FontSelectionView*	fSansSerifFontView;
			FontSelectionView*	fFixedFontView;

			BCheckBox*			fUseProxyCheckBox;
			BTextControl*		fProxyAddressControl;
			BTextControl*		fProxyPortControl;
			BCheckBox*			fUseProxyAuthCheckBox;
			BTextControl*		fProxyUsernameControl;
			BTextControl*		fProxyPasswordControl;

			BButton*			fApplyButton;
			BButton*			fCancelButton;
			BButton*			fRevertButton;

			BMenuField*			fStandardSizesMenu;
			BMenuField*			fFixedSizesMenu;
};


#endif // SETTINGS_WINDOW_H

