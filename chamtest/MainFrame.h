struct Chameleon;
struct ChameleonParams;

class MainFrame : public wxFrame
{
	wxPanel *imgPanel;

	wxPanel *cp1;
	wxPanel *cp2;
	wxPanel *cp3;
	wxPanel *cp4;

	wxChoice *weightChoice;

	wxTextCtrl *output;

	wxImage *img;

	Chameleon *chameleon;
	ChameleonParams chamParams[4];
	ChameleonParams editParams;

	wxTextCtrl *countWeight;
	wxTextCtrl *edgeWeight;
	wxTextCtrl *bg1DistanceWeight;
	wxTextCtrl *fg1DistanceWeight;
	wxTextCtrl *saturationWeight;
	wxTextCtrl *contrastWeight;

	void onFileQuit(wxCommandEvent &e);
	void onFileOpen(wxCommandEvent &e);

	void onPaint(wxPaintEvent &e);
	void onResize(wxSizeEvent &e);

	void onWeightChoiceChanged(wxCommandEvent &e);
	void onWeightChanged(wxCommandEvent &e);

	void updateChameleon();
public:
	MainFrame();
	~MainFrame();
};