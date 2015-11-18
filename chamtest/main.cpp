#include <wx/wx.h>

#include <chameleon.h>

#include "main.h"
#include "MainFrame.h"

IMPLEMENT_APP(ChameleonApp);

bool ChameleonApp::OnInit()
{
	wxInitAllImageHandlers();
	MainFrame *mainFrame = new MainFrame();
	mainFrame->Show();
	return true;
}
