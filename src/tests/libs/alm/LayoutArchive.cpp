/*
 * Copyright 2012, Clemens Zeidler <haiku@clemens-zeidler.de>
 * Distributed under the terms of the MIT License.
 */


#include <Alert.h>
#include <Application.h>
#include <Button.h>
#include <File.h>
#include <Path.h>
#include <RadioButton.h>
#include <Roster.h>
#include <Window.h>

#include <ALMLayout.h>
#include <ALMLayoutBuilder.h>

#include <editor/LayoutArchive.h>


const char* kGUIFileName = "TestLayout";


class LayoutArchiveWindow : public BWindow {
public:
	LayoutArchiveWindow(BRect frame) 
		:
		BWindow(frame, "Layout Archive", B_TITLED_WINDOW, B_QUIT_ON_WINDOW_CLOSE
			| B_AUTO_UPDATE_SIZE_LIMITS)
	{
		fLayout = new BALMLayout(10, 10);
		BALM::BALMLayoutBuilder builder(this, fLayout);

		// Read gui specifications relative to the app binary path.
		BPath guiFilePath;
		_GetGuiPath(kGUIFileName, guiFilePath);

		// Restore gui specifications. 
		BFile guiFile(guiFilePath.Path(), B_READ_ONLY);
		LayoutArchive layoutArchive(fLayout);
		if (layoutArchive.RestoreFromAttribute(&guiFile, "layout") != B_OK) {
			BString text = "Can't find layout specification file: \"";
			text << kGUIFileName;
			text << "\"";
			BAlert* alert = new BAlert("Layout Specifications Not Found",
				text, "Quit");
			alert->Go();
			PostMessage(B_QUIT_REQUESTED);
		}

		// Access the views in the layout.

		BButton* button = layoutArchive.FindView<BButton>("ButtonTest");
		if (button != NULL)
			button->SetLabel("Hey");

		BRadioButton* radioButton
			= layoutArchive.FindView<BRadioButton>("RadioButtonTest");
		if (radioButton != NULL)
			radioButton->SetLabel("World");
	}

private:
	void _GetGuiPath(const char* name, BPath& path)
	{
		app_info appInfo;
		be_app->GetAppInfo(&appInfo);
		BPath appPath(&appInfo.ref);
		appPath.GetParent(&path);
		path.Append(name);
	}

private:
			BALMLayout*			fLayout;
};


int
main()
{
	BApplication app("application/x-vnd.haiku.ALELayoutArchive");

	BWindow* window = new LayoutArchiveWindow(BRect(100, 100, 500, 350));
	window->Show();

	app.Run();
	return 0;
}

