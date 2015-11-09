#include <ostream>

#include <wx/wx.h>
#include <wx/dcbuffer.h>
#include <wx/wfstream.h>

#include <chameleon.h>
#include <chameleon_internal.h>

#include "MainFrame.h"

MainFrame::MainFrame() : wxFrame(nullptr, wxID_ANY, "Chameleon Test App")
{
	img = nullptr;

	wxMenuBar *menuBar = new wxMenuBar();
	wxMenu *file = new wxMenu();

	file->Append(wxID_OPEN, "&Open\tCtrl+O");
	file->AppendSeparator();
	file->Append(wxID_EXIT, "E&xit\tAlt+F4");

	menuBar->Append(file, "&File");
	SetMenuBar(menuBar);

	Bind(wxEVT_COMMAND_MENU_SELECTED, &MainFrame::onFileOpen, this, wxID_OPEN);
	Bind(wxEVT_COMMAND_MENU_SELECTED, &MainFrame::onFileQuit, this, wxID_EXIT);

	wxBoxSizer *mainSizer = new wxBoxSizer(wxVERTICAL);

	wxBoxSizer *imgSizer = new wxBoxSizer(wxHORIZONTAL);

	imgPanel = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(514, 514), wxBORDER_SUNKEN);
	imgPanel->SetBackgroundStyle(wxBG_STYLE_PAINT);
	imgPanel->Bind(wxEVT_PAINT, &MainFrame::onPaint, this);
	imgPanel->Bind(wxEVT_SIZE, &MainFrame::onResize, this);
	imgSizer->Add(imgPanel, wxSizerFlags(1).Expand().FixedMinSize());

	wxBoxSizer *colorSizer = new wxBoxSizer(wxVERTICAL);

	cp1 = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(128, 128));
	cp1->SetBackgroundColour(wxColor((unsigned long) 0));
	colorSizer->Add(cp1, wxSizerFlags(0).Expand().FixedMinSize());

	cp2 = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(128, 128));
	cp2->SetBackgroundColour(wxColor((unsigned long)0));
	colorSizer->Add(cp2, wxSizerFlags(0).Expand().FixedMinSize());

	cp3 = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(128, 128));
	cp3->SetBackgroundColour(wxColor((unsigned long)0));
	colorSizer->Add(cp3, wxSizerFlags(0).Expand().FixedMinSize());

	cp4 = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(128, 128));
	cp4->SetBackgroundColour(wxColor((unsigned long)0));
	colorSizer->Add(cp4, wxSizerFlags(0).Expand().FixedMinSize());

	imgSizer->Add(colorSizer, wxSizerFlags(0).Expand().FixedMinSize());

	mainSizer->Add(imgSizer, wxSizerFlags(1).Expand().FixedMinSize());

	output = new wxTextCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(512, 128), wxTE_MULTILINE | wxTE_READONLY);
	mainSizer->Add(output, wxSizerFlags(0).Expand().FixedMinSize());
	std::cout.rdbuf(output);

	SetSizerAndFit(mainSizer);
	
	std::cout << "Chameleon Test App" << std::endl;
}

MainFrame::~MainFrame()
{
	if (img != nullptr)
	{
		delete img;
	}

}

void MainFrame::onFileQuit(wxCommandEvent &e)
{
	Close();
}

void MainFrame::onFileOpen(wxCommandEvent &e)
{
	wxFileDialog open(this, "Open Image File", wxEmptyString, wxEmptyString, "Image Files (*.png;*.jpg;*.jpeg;*.bmp)|*.png;*.jpg;*.jpeg;*.bmp", wxFD_OPEN | wxFD_FILE_MUST_EXIST);

	if (open.ShowModal() == wxID_CANCEL)
	{
		return;
	}

	if (img != nullptr)
	{
		delete img;
	}

	wxFileInputStream input(open.GetPath());

	if (input.IsOk())
	{
		img = new wxImage(input);
		std::cout << "Opened image: " << open.GetPath() << std::endl;

		// Quick-n-dirty setup
		size_t w = img->GetWidth();
		size_t h = img->GetHeight();
		uint32_t *imgData = new uint32_t[w * h];

		const unsigned char *wxImgData = img->GetData();

		for (size_t i = 0; i < w*h; ++i)
		{
			imgData[i] = 0xFF000000 | (wxImgData[i * 3 + 2] << 16) | (wxImgData[i * 3 + 1] << 8) | (wxImgData[i * 3 + 0] << 0);
		}

		std::cout << "Processing image..." << std::endl;
		wxMilliClock_t start = wxGetLocalTimeMillis();

		Chameleon *chameleon = createChameleon();

		chameleonProcessImage(chameleon, imgData, w, h);

		chameleonFindKeyColors(chameleon, chameleonDefaultImageParams());

		cp1->SetBackgroundColour(wxColor(chameleonGetColor(chameleon, CHAMELEON_BACKGROUND1)));
		cp2->SetBackgroundColour(wxColor(chameleonGetColor(chameleon, CHAMELEON_BACKGROUND2)));
		cp3->SetBackgroundColour(wxColor(chameleonGetColor(chameleon, CHAMELEON_FOREGROUND1)));
		cp4->SetBackgroundColour(wxColor(chameleonGetColor(chameleon, CHAMELEON_FOREGROUND2)));

		wxMilliClock_t end = wxGetLocalTimeMillis();

		std::cout << "Processed image in " << (end - start) << "ms." << std::endl;

		for (size_t y = 0; y < h; ++y)
		{
			for (size_t x = 0; x < w; ++x)
			{
				uint16_t ci = XRGB5(imgData[x + (y * w)]);

				if (ci != 0 && (chameleon->colors[ci].r + chameleon->colors[ci].g + chameleon->colors[ci].b) == 0)
				{
					wxMessageBox("oops");
				}
				else if (chameleon->colors[ci].r > 1 || chameleon->colors[ci].g > 1 || chameleon->colors[ci].b > 1)
				{
					wxMessageBox("oops 2");
				}

				img->SetRGB(x, y, chameleon->colors[ci].r * 255, chameleon->colors[ci].g * 255, chameleon->colors[ci].b * 255);
			}
		}

		destroyChameleon(chameleon);
		chameleon = nullptr;

		delete[] imgData;

		cp1->Refresh();
		cp2->Refresh();
		cp3->Refresh();
		cp4->Refresh();

		imgPanel->Refresh();
		imgPanel->Update();
	}
	else
	{
		wxMessageBox("Failed to open: " + open.GetPath(), "Error:");
	}
}

void MainFrame::onPaint(wxPaintEvent &e)
{
	wxAutoBufferedPaintDC dc(imgPanel);

	dc.SetBackground(wxBrush(wxColor(0xFF333333)));
	dc.Clear();

	// Draw the image if there is one
	if (img != nullptr)
	{
		size_t w = img->GetWidth();
		size_t h = img->GetHeight();

		wxRect csize = imgPanel->GetClientRect();

		float scalebase = (csize.width < csize.height ? csize.width : csize.height);

		float scale = 1.0f;
		if (w > scalebase || h > scalebase)
		{
			scale = (w > h ? scalebase / w : scalebase / h);
		}

		w *= scale;
		h *= scale;

		dc.DrawBitmap(img->ResampleBicubic(w, h), wxPoint((csize.width - w) / 2, (csize.height - h) / 2));
	}
}

void MainFrame::onResize(wxSizeEvent &e)
{
	imgPanel->Refresh();
	imgPanel->Update();
}
