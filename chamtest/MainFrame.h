class MainFrame : public wxFrame
{
	wxPanel *imgPanel;

	wxPanel *cp1;
	wxPanel *cp2;
	wxPanel *cp3;
	wxPanel *cp4;

	wxTextCtrl *output;

	wxImage *img;

	void onFileQuit(wxCommandEvent &e);
	void onFileOpen(wxCommandEvent &e);

	void onPaint(wxPaintEvent &e);
	void onResize(wxSizeEvent &e);
public:
	MainFrame();
	~MainFrame();
};