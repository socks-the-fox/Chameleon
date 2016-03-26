#include <ostream>

#include <wx/wx.h>
#include <wx/dcbuffer.h>
#include <wx/wfstream.h>
#include <wx/valnum.h>

#include <chameleon.h>
#include <chameleon_internal.h>

#include "MainFrame.h"

MainFrame::MainFrame() : wxFrame(nullptr, wxID_ANY, "Chameleon Test App")
{
	img = nullptr;
	chameleon = nullptr;

	wxMenuBar *menuBar = new wxMenuBar();
	wxMenu *file = new wxMenu();

	// Copy parameters to our temporary setup
	const ChameleonParams *defParams = chameleonDefaultImageParams();

	for (size_t i = 0; i < 4; ++i)
	{
		chamParams[i] = defParams[i];
	}

	editParams = chamParams[CHAMELEON_BACKGROUND1];

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
	imgPanel->Bind(wxEVT_MOTION, &MainFrame::onMouseMove, this);
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

	wxPanel *dataPanel = new wxPanel(this);
	wxBoxSizer *dataSizer = new wxBoxSizer(wxHORIZONTAL);

	colorData = new wxStaticText(dataPanel, wxID_ANY, "p(-) e(-) b(-) f(-) s(-) c(-) t(-)");
	dataSizer->Add(colorData, wxSizerFlags(1).Border(wxALL, 4).Expand().FixedMinSize());

	dataPanel->SetSizerAndFit(dataSizer);

	mainSizer->Add(dataPanel, wxSizerFlags(0).Expand().FixedMinSize());

	wxPanel *choicePanel = new wxPanel(this);
	wxBoxSizer *choiceSizer = new wxBoxSizer(wxHORIZONTAL);

	choiceSizer->Add(new wxStaticText(choicePanel, wxID_ANY, "Weights for"), wxSizerFlags(0).Border(wxALL, 4).Align(wxALIGN_LEFT | wxALIGN_CENTER_VERTICAL));

	weightChoice = new wxChoice(choicePanel, wxID_ANY);
	weightChoice->Insert("Background 1", CHAMELEON_BACKGROUND1);
	weightChoice->Insert("Foreground 1", CHAMELEON_FOREGROUND1);
	weightChoice->Insert("Background 2", CHAMELEON_BACKGROUND2);
	weightChoice->Insert("Foreground 2", CHAMELEON_FOREGROUND2);
	weightChoice->Select(CHAMELEON_BACKGROUND1);

	weightChoice->Bind(wxEVT_CHOICE, &MainFrame::onWeightChoiceChanged, this);

	choiceSizer->Add(weightChoice, wxSizerFlags(0).Border(wxALL, 4).Align(wxALIGN_LEFT | wxALIGN_CENTER_VERTICAL));

	choiceSizer->AddStretchSpacer();

	choicePanel->SetSizerAndFit(choiceSizer);
	mainSizer->Add(choicePanel, wxSizerFlags(0).Expand().FixedMinSize());

	wxPanel *editPanel = new wxPanel(this);
	wxGridSizer *editSizer = new wxFlexGridSizer(2, 6, 0, 0);

	wxFloatingPointValidator<float> countVal(3, &editParams.countWeight);
	countWeight = new wxTextCtrl(editPanel, wxID_ANY, wxString::FromDouble(editParams.countWeight, 3), wxDefaultPosition, wxDefaultSize, 0, countVal);
	countWeight->Bind(wxEVT_TEXT, &MainFrame::onWeightChanged, this);
	editSizer->Add(new wxStaticText(editPanel, wxID_ANY, "Population:"), wxSizerFlags(0).Border(wxALL, 4).Align(wxALIGN_RIGHT | wxALIGN_CENTER_VERTICAL));
	editSizer->Add(countWeight, wxSizerFlags(0).Border(wxALL, 4).Align(wxALIGN_LEFT | wxALIGN_CENTER_VERTICAL));

	wxFloatingPointValidator<float> bg1Val(3, &editParams.bg1distanceWeight);
	bg1DistanceWeight = new wxTextCtrl(editPanel, wxID_ANY, wxString::FromDouble(editParams.bg1distanceWeight, 3), wxDefaultPosition, wxDefaultSize, 0, bg1Val);
	bg1DistanceWeight->Bind(wxEVT_TEXT, &MainFrame::onWeightChanged, this);
	editSizer->Add(new wxStaticText(editPanel, wxID_ANY, "BG1 Dist:"), wxSizerFlags(0).Border(wxALL, 4).Align(wxALIGN_RIGHT | wxALIGN_CENTER_VERTICAL));
	editSizer->Add(bg1DistanceWeight, wxSizerFlags(0).Border(wxALL, 4).Align(wxALIGN_LEFT | wxALIGN_CENTER_VERTICAL));

	wxFloatingPointValidator<float> satVal(3, &editParams.saturationWeight);
	saturationWeight = new wxTextCtrl(editPanel, wxID_ANY, wxString::FromDouble(editParams.saturationWeight, 3), wxDefaultPosition, wxDefaultSize, 0, satVal);
	saturationWeight->Bind(wxEVT_TEXT, &MainFrame::onWeightChanged, this);
	editSizer->Add(new wxStaticText(editPanel, wxID_ANY, "Saturation:"), wxSizerFlags(0).Border(wxALL, 4).Align(wxALIGN_RIGHT | wxALIGN_CENTER_VERTICAL));
	editSizer->Add(saturationWeight, wxSizerFlags(0).Border(wxALL, 4).Align(wxALIGN_LEFT | wxALIGN_CENTER_VERTICAL));

	wxFloatingPointValidator<float> edgeVal(3, &editParams.edgeWeight);
	edgeWeight = new wxTextCtrl(editPanel, wxID_ANY, wxString::FromDouble(editParams.edgeWeight, 3), wxDefaultPosition, wxDefaultSize, 0, edgeVal);
	edgeWeight->Bind(wxEVT_TEXT, &MainFrame::onWeightChanged, this);
	editSizer->Add(new wxStaticText(editPanel, wxID_ANY, "Edge Pop:"), wxSizerFlags(0).Border(wxALL, 4).Align(wxALIGN_RIGHT | wxALIGN_CENTER_VERTICAL));
	editSizer->Add(edgeWeight, wxSizerFlags(0).Border(wxALL, 4).Align(wxALIGN_LEFT | wxALIGN_CENTER_VERTICAL));

	wxFloatingPointValidator<float> fg1Val(3, &editParams.fg1distanceWeight);
	fg1DistanceWeight = new wxTextCtrl(editPanel, wxID_ANY, wxString::FromDouble(editParams.fg1distanceWeight, 3), wxDefaultPosition, wxDefaultSize, 0, fg1Val);
	fg1DistanceWeight->Bind(wxEVT_TEXT, &MainFrame::onWeightChanged, this);
	editSizer->Add(new wxStaticText(editPanel, wxID_ANY, "FG1 Dist:"), wxSizerFlags(0).Border(wxALL, 4).Align(wxALIGN_RIGHT | wxALIGN_CENTER_VERTICAL));
	editSizer->Add(fg1DistanceWeight, wxSizerFlags(0).Border(wxALL, 4).Align(wxALIGN_LEFT | wxALIGN_CENTER_VERTICAL));

	wxFloatingPointValidator<float> contVal(3, &editParams.fg1distanceWeight);
	contrastWeight = new wxTextCtrl(editPanel, wxID_ANY, wxString::FromDouble(editParams.contrastWeight, 3), wxDefaultPosition, wxDefaultSize, 0, contVal);
	contrastWeight->Bind(wxEVT_TEXT, &MainFrame::onWeightChanged, this);
	editSizer->Add(new wxStaticText(editPanel, wxID_ANY, "Contrast:"), wxSizerFlags(0).Border(wxALL, 4).Align(wxALIGN_RIGHT | wxALIGN_CENTER_VERTICAL));
	editSizer->Add(contrastWeight, wxSizerFlags(0).Border(wxALL, 4).Align(wxALIGN_LEFT | wxALIGN_CENTER_VERTICAL));

	editPanel->SetSizerAndFit(editSizer);
	mainSizer->Add(editPanel, wxSizerFlags(0).Expand().FixedMinSize());

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

	if (chameleon != nullptr)
	{
		destroyChameleon(chameleon);
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

		if (chameleon != nullptr)
		{
			destroyChameleon(chameleon);
			chameleon = nullptr;
		}
		chameleon = createChameleon();

		chameleonProcessImage(chameleon, imgData, w, h);

		updateChameleon();

		wxMilliClock_t end = wxGetLocalTimeMillis();

		std::cout << "Processed image in " << (end - start) << "ms." << std::endl;

		for (size_t y = 0; y < h; ++y)
		{
			for (size_t x = 0; x < w; ++x)
			{
				uint16_t ci = XRGB5(imgData[x + (y * w)]);

				// Some pixels get set to black. These pixels are skipped during the downsampling process
				// (it's a very simplistic process) and so Chameleon never sees them.

				img->SetRGB(x, y, chameleon->colors[ci].r * 255, chameleon->colors[ci].g * 255, chameleon->colors[ci].b * 255);
			}
		}

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

		float scalew = (float) csize.width / w;
		float scaleh = (float) csize.height / h;
		float scale = (scalew > scaleh)? scaleh : scalew;

		w *= scale;
		h *= scale;

		resizedImg = img->ResampleBicubic(w, h);

		dc.DrawBitmap(resizedImg, wxPoint((csize.width - w) / 2, (csize.height - h) / 2));
	}
}

void MainFrame::onResize(wxSizeEvent &e)
{
	imgPanel->Refresh();
	imgPanel->Update();
}

void MainFrame::onMouseMove(wxMouseEvent &e)
{
	// Draw the image if there is one
	if (img != nullptr)
	{
		size_t w = img->GetWidth();
		size_t h = img->GetHeight();

		wxRect csize = imgPanel->GetClientRect();

		float scalew = (float)csize.width / w;
		float scaleh = (float)csize.height / h;
		float scale = (scalew > scaleh) ? scaleh : scalew;

		w *= scale;
		h *= scale;

		wxRect imgRect(wxPoint((csize.width - w) / 2, (csize.height - h) / 2), wxSize(w, h));

		if (imgRect.Contains(wxPoint(e.GetX(), e.GetY())))
		{
			size_t x = e.GetX() - ((csize.width - w) / 2);
			size_t y = e.GetY() - ((csize.height - h) / 2);

			uint16_t index = XRGB5(resizedImg.GetRed(x, y) | (resizedImg.GetGreen(x, y) << 8) | (resizedImg.GetBlue(x, y) << 16));

			ColorStat *stat = &chameleon->colors[index];

			float p, e, b, f, s, c, t;
			p = stat->count * editParams.countWeight;
			e = stat->edgeCount * editParams.edgeWeight;
			b = distance(stat, &chameleon->colors[chameleon->colorIndex[CHAMELEON_BACKGROUND1]]) * editParams.bg1distanceWeight;
			f = distance(stat, &chameleon->colors[chameleon->colorIndex[CHAMELEON_FOREGROUND1]]) * editParams.fg1distanceWeight;
			s = saturation(stat) * editParams.saturationWeight;
			c = contrast(stat, &chameleon->colors[chameleon->colorIndex[CHAMELEON_BACKGROUND1]]) * editParams.contrastWeight;

			t = p + e + b + f + s + c;

			wxString str = "p(" + wxString::FromDouble(p) + ")";
			str += " e(" + wxString::FromDouble(e) + ")";
			str += " b(" + wxString::FromDouble(b) + ")";
			str += " f(" + wxString::FromDouble(f) + ")";
			str += " s(" + wxString::FromDouble(s) + ")";
			str += " c(" + wxString::FromDouble(c) + ")";
			str += " t(" + wxString::FromDouble(t) + ")";

			if (index == chameleon->colorIndex[weightChoice->GetSelection()])
			{
				str += " *";
			}

			colorData->SetLabel(str);
		}
		else
		{
			colorData->SetLabel("p(-) e(-) b(-) f(-) s(-) c(-) t(-)");
		}
	}
	else
	{
		colorData->SetLabel("p(-) e(-) b(-) f(-) s(-) c(-) t(-)");
	}
}

void MainFrame::onWeightChoiceChanged(wxCommandEvent &e)
{
	size_t i = weightChoice->GetSelection();

	editParams = chamParams[i];

	countWeight->ChangeValue(wxString::FromDouble(editParams.countWeight, 3));
	edgeWeight->ChangeValue(wxString::FromDouble(editParams.edgeWeight, 3));
	bg1DistanceWeight->ChangeValue(wxString::FromDouble(editParams.bg1distanceWeight, 3));
	fg1DistanceWeight->ChangeValue(wxString::FromDouble(editParams.fg1distanceWeight, 3));
	saturationWeight->ChangeValue(wxString::FromDouble(editParams.saturationWeight, 3));
	contrastWeight->ChangeValue(wxString::FromDouble(editParams.contrastWeight, 3));
}

void MainFrame::onWeightChanged(wxCommandEvent &e)
{
	size_t i = weightChoice->GetSelection();
	double tmp;

	if (!countWeight->GetValue().ToDouble(&tmp))
	{
		tmp = 0;
	}
	editParams.countWeight = tmp;

	if(!edgeWeight->GetValue().ToDouble(&tmp))
	{
		tmp = 0;
	}
	editParams.edgeWeight = tmp;

	if(!bg1DistanceWeight->GetValue().ToDouble(&tmp))
	{
		tmp = 0;
	}
	editParams.bg1distanceWeight = tmp;

	if (!fg1DistanceWeight->GetValue().ToDouble(&tmp))
	{
		tmp = 0;
	}
	editParams.fg1distanceWeight = tmp;

	if (!saturationWeight->GetValue().ToDouble(&tmp))
	{
		tmp = 0;
	}
	editParams.saturationWeight = tmp;

	if (!contrastWeight->GetValue().ToDouble(&tmp))
	{
		tmp = 0;
	}
	editParams.contrastWeight = tmp;

	chamParams[i] = editParams;

	updateChameleon();
}

void MainFrame::updateChameleon()
{
	// Don't refresh Chameleon's colors if it doesn't exist yet...
	if (chameleon == nullptr)
	{
		return;
	}

	chameleonFindKeyColors(chameleon, chamParams, false);

	cp1->SetBackgroundColour(wxColor(chameleonGetColor(chameleon, CHAMELEON_BACKGROUND1)));
	cp1->Refresh();
	cp2->SetBackgroundColour(wxColor(chameleonGetColor(chameleon, CHAMELEON_BACKGROUND2)));
	cp2->Refresh();
	cp3->SetBackgroundColour(wxColor(chameleonGetColor(chameleon, CHAMELEON_FOREGROUND1)));
	cp3->Refresh();
	cp4->SetBackgroundColour(wxColor(chameleonGetColor(chameleon, CHAMELEON_FOREGROUND2)));
	cp4->Refresh();
}