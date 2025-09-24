// Scintilla source code edit control
// ScintillaGTK.h - GTK+ specific subclass of ScintillaBase
// Copyright 1998-2004 by Neil Hodgson <neilh@scintilla.org>
// Copyright 2025 by Xuges <xuges@qq.com>
// The License.txt file describes the conditions under which this software may be distributed.

#ifndef SCINTILLAGTK_H
#define SCINTILLAGTK_H

namespace Scintilla::Internal {

#define OBJECT_CLASS GObjectClass

struct FontOptions {
	cairo_antialias_t antialias {};
	cairo_subpixel_order_t order {};
	cairo_hint_style_t hint {};
	FontOptions() noexcept = default;
	explicit FontOptions(GtkWidget *widget) noexcept;
	bool operator==(const FontOptions &other) const noexcept;
};

// Scintilla private
class ScintillaGTK : public ScintillaBase {
	_ScintillaObject *sci;
	Window wText;
	Window scrollbarv;
	Window scrollbarh;
	GtkAdjustment *adjustmentv;
	GtkAdjustment *adjustmenth;
	int verticalScrollBarWidth;
	int horizontalScrollBarHeight;

	PRectangle rectangleClient;

	SelectionText primary;
	SelectionPosition posPrimary;

	UniqueGdkEvent evbtn;
	guint buttonMouse;
	bool capturedMouse;
	bool dragWasDropped;
	int lastKey;
	int rectangularSelectionModifier;

	GtkWidgetClass *parentClass;

	size_t inClearSelection = 0;

	bool preeditInitialized;
	//Window wPreedit;
	//Window wPreeditDraw;
	UniqueIMContext im_context;
	GUnicodeScript lastNonCommonScript;

	GtkSettings *settings;
	gulong settingsHandlerId;

	// Wheel mouse support
	unsigned int linesPerScroll;
	gint64 lastWheelMouseTime;
	gint lastWheelMouseDirection;
	gint wheelMouseIntensity;
	gdouble smoothScrollY;
	gdouble smoothScrollX;

	cairo_rectangle_list_t *rgnUpdate;

	bool repaintFullWindow;

	guint styleIdleID;
	guint scrollBarIdleID = 0;
	FontOptions fontOptionsPrevious;
	int accessibilityEnabled;

	guint drawTimer = 0;
	bool needDraw = false;

public:
	explicit ScintillaGTK(_ScintillaObject *sci_);
	// Deleted so ScintillaGTK objects can not be copied.
	ScintillaGTK(const ScintillaGTK &) = delete;
	ScintillaGTK(ScintillaGTK &&) = delete;
	ScintillaGTK &operator=(const ScintillaGTK &) = delete;
	ScintillaGTK &operator=(ScintillaGTK &&) = delete;
	~ScintillaGTK() override;
	static ScintillaGTK *FromWidget(GtkWidget *widget) noexcept;
	static void ClassInit(OBJECT_CLASS *object_class, GtkWidgetClass *widget_class);
private:
	void Init();
	void Finalise() override;
	bool AbandonPaint() override;
	void DisplayCursor(Window::Cursor c) override;
	bool DragThreshold(Point ptStart, Point ptNow) override;
	void StartDrag() override;
	Sci::Position TargetAsUTF8(char *text) const;
	Sci::Position EncodedFromUTF8(const char *utf8, char *encoded) const;
	bool ValidCodePage(int codePage) const override;
	std::string UTF8FromEncoded(std::string_view encoded) const override;
	std::string EncodedFromUTF8(std::string_view utf8) const override;
public: 	// Public for scintilla_send_message
	sptr_t WndProc(Scintilla::Message iMessage, Scintilla::uptr_t wParam, Scintilla::sptr_t lParam) override;
private:
	sptr_t DefWndProc(Scintilla::Message iMessage, Scintilla::uptr_t wParam, Scintilla::sptr_t lParam) override;
	struct TimeThunk {
		TickReason reason;
		ScintillaGTK *scintilla;
		guint timer;
		TimeThunk() noexcept : reason(TickReason::caret), scintilla(nullptr), timer(0) {}
	};
	TimeThunk timers[static_cast<size_t>(TickReason::dwell)+1];
	bool FineTickerRunning(TickReason reason) override;
	void FineTickerStart(TickReason reason, int millis, int tolerance) override;
	void FineTickerCancel(TickReason reason) override;
	bool SetIdle(bool on) override;
	void SetMouseCapture(bool on) override;
	bool HaveMouseCapture() override;
	bool PaintContains(PRectangle rc) override;
	void FullPaint();
	void SetClientRectangle();
	PRectangle GetClientRectangle() const override;
	void ScrollText(Sci::Line linesToMove) override;
	void SetVerticalScrollPos() override;
	void SetHorizontalScrollPos() override;
	bool ModifyScrollBars(Sci::Line nMax, Sci::Line nPage) override;
	void ReconfigureScrollBars() override;
	void SetScrollBars() override;
	void NotifyChange() override;
	void NotifyFocus(bool focus) override;
	void NotifyParent(Scintilla::NotificationData scn) override;
	void NotifyKey(Scintilla::Keys key, Scintilla::KeyMod modifiers);
	void NotifyURIDropped(const char *list);
	const char *CharacterSetID() const;
	std::unique_ptr<CaseFolder> CaseFolderForEncoding() override;
	std::string CaseMapString(const std::string &s, CaseMapping caseMapping) override;
	int KeyDefault(Scintilla::Keys key, Scintilla::KeyMod modifiers) override;
	void CopyToClipboard(const SelectionText &selectedText) override;
	void Copy() override;
	void RequestSelection();
	void Paste() override;
	void CreateCallTipWindow(PRectangle rc) override;
	void AddToPopUp(const char *label, int cmd = 0, bool enabled = true) override;
	bool OwnPrimarySelection();
	void ClaimSelection() override;
	void GetGtkSelectionText(GdkClipboard* clipboard, GAsyncResult* result, SelectionText& selText);
	void InsertSelection(GdkClipboard* clipboard, GAsyncResult* res);
	void ReceivedClipboard(GdkClipboard* clipboard, GAsyncResult* res) noexcept;
private:
	void StoreOnClipboard(SelectionText *clipText);
	void ClearPrimarySelection();

	void Resize(int width, int height);

	// Callback functions
	void RealizeThis(GtkWidget *widget);
	static void Realize(GtkWidget *widget);
	void UnRealizeThis(GtkWidget *widget);
	static void UnRealize(GtkWidget *widget);
	void MapThis();
	static void Map(GtkWidget *widget);
	void UnMapThis();
	static void UnMap(GtkWidget *widget);
	static void FocusNotify(GtkEventControllerFocus* controller, GParamSpec* pspec, ScintillaGTK* sciThis);
	gint FocusInThis();
	gint FocusOutThis();
	static void SizeRequest(GtkWidget *widget, GtkRequisition *requisition);
	static void GetPreferredWidth(GtkWidget *widget, gint *minimalWidth, gint *naturalWidth);
	static void GetPreferredHeight(GtkWidget *widget, gint *minimalHeight, gint *naturalHeight);
	static void SizeAllocate(GtkWidget* widget, int width, int height, int baseline);
	void CheckForFontOptionChange();
	gboolean DrawTextThis(cairo_t *cr);
	static gboolean DrawTextCb(GtkWidget *widget, cairo_t *cr, int width, int height, ScintillaGTK *sciThis);
	void DrawThis(GtkSnapshot* snapshot);
	static void DrawMain(GtkWidget *widget, GtkSnapshot* snapshot);

	static void ScrollSignal(GtkAdjustment *adj, ScintillaGTK *sciThis);
	static void ScrollHSignal(GtkAdjustment *adj, ScintillaGTK *sciThis);
	gint MousePressThis(GtkGestureClick* self, gint nPress, gdouble x, gdouble y);
	static gint MousePress(GtkGestureClick* self, gint nPress, gdouble x, gdouble y, ScintillaGTK* sciThis);
	static gint MouseRelease(GtkGestureClick* self, gint nPress, gdouble x, gdouble y, ScintillaGTK* sciThis);
	gint MouseReleaseThis(GtkGestureClick* self, gint nPress, gdouble x, gdouble y);
	static gint ScrollEvent(GtkEventControllerScroll* self, gdouble dx, gdouble dy, ScintillaGTK* sciThis);
	gint ScrollEventThis(GtkEventControllerScroll* self, gdouble dx, gdouble dy);
	static gint Motion(GtkEventControllerMotion* self, gdouble x, gdouble y, ScintillaGTK* sciThis);
	gboolean KeyPressThis(GtkEventControllerKey* self, guint keyval, guint keycode, GdkModifierType state);
	static gboolean KeyPress(GtkEventControllerKey* self, guint keyval, guint keycode, GdkModifierType state, ScintillaGTK* sciThis);
	static gboolean KeyRelease(GtkEventControllerKey* self, guint keyval, guint keycode, GdkModifierType state, ScintillaGTK* sciThis);
	gboolean DrawPreeditThis(GtkWidget *widget, cairo_t *cr);
	static gboolean DrawPreedit(GtkWidget *widget, cairo_t *cr, int width, int height, ScintillaGTK *sciThis);

	bool KoreanIME();
	void CommitThis(char *commitStr);
	static void Commit(GtkIMContext *context, char *str, ScintillaGTK *sciThis);
	void PreeditChangedInlineThis();
	void PreeditChangedWindowedThis();
	static void PreeditChanged(GtkIMContext *context, ScintillaGTK *sciThis);
	bool RetrieveSurroundingThis(GtkIMContext *context);
	static gboolean RetrieveSurrounding(GtkIMContext *context, ScintillaGTK *sciThis);
	bool DeleteSurroundingThis(GtkIMContext *context, gint characterOffset, gint characterCount);
	static gboolean DeleteSurrounding(GtkIMContext *context, gint characterOffset, gint characterCount,
					  ScintillaGTK *sciThis);
	void MoveImeCarets(Sci::Position pos);
	void DrawImeIndicator(int indicator, Sci::Position len);
	void SetCandidateWindowPos();

	static void Dispose(GObject *object);
	static void Destroy(GObject *object);
	/*static void SelectionReceived(GtkWidget *widget, GtkSelectionData *selection_data,
				      guint time);
	static void SelectionGet(GtkWidget *widget, GtkSelectionData *selection_data,
				 guint info, guint time);
	static gint SelectionClear(GtkWidget *widget, GdkEventSelection *selection_event);
	gboolean DragMotionThis(GdkDragContext *context, gint x, gint y, guint dragtime);
	static gboolean DragMotion(GtkWidget *widget, GdkDragContext *context,
				   gint x, gint y, guint dragtime);
	static void DragLeave(GtkWidget *widget, GdkDragContext *context,
			      guint time);
	static void DragEnd(GtkWidget *widget, GdkDragContext *context);
	static gboolean Drop(GtkWidget *widget, GdkDragContext *context,
			     gint x, gint y, guint time);
	static void DragDataReceived(GtkWidget *widget, GdkDragContext *context,
				     gint x, gint y, GtkSelectionData *selection_data, guint info, guint time);
	static void DragDataGet(GtkWidget *widget, GdkDragContext *context,
				GtkSelectionData *selection_data, guint info, guint time);*/
	static gboolean TimeOut(gpointer ptt);
	static gboolean IdleCallback(gpointer pSci);
	static gboolean StyleIdle(gpointer pSci);
	void IdleWork() override;
	void QueueIdleWork(WorkItems items, Sci::Position upTo) override;
	void SetDocPointer(Document *document) override;
	static void PopUpCB(GObject* obj, GVariant* param, ScintillaGTK* sciThis);

	static gboolean DrawCT(GtkWidget *widget, cairo_t *cr, CallTip *ctip);
	static gboolean PressCT(GObject* obj, guint nPress, gdouble x, gdouble y, ScintillaGTK* sciThis);

	static sptr_t DirectFunction(sptr_t ptr,
				     unsigned int iMessage, uptr_t wParam, sptr_t lParam);
	static sptr_t DirectStatusFunction(sptr_t ptr,
				     unsigned int iMessage, uptr_t wParam, sptr_t lParam, int *pStatus);
};

// helper class to watch a GObject lifetime and get notified when it dies
class GObjectWatcher {
	GObject *weakRef;

	void WeakNotifyThis([[maybe_unused]] GObject *obj) {
		PLATFORM_ASSERT(obj == weakRef);

		Destroyed();
		weakRef = nullptr;
	}

	static void WeakNotify(gpointer data, GObject *obj) {
		static_cast<GObjectWatcher *>(data)->WeakNotifyThis(obj);
	}

public:
	GObjectWatcher(GObject *obj) :
		weakRef(obj) {
		g_object_weak_ref(weakRef, WeakNotify, this);
	}

	// Deleted so GObjectWatcher objects can not be copied.
	GObjectWatcher(const GObjectWatcher&) = delete;
	GObjectWatcher(GObjectWatcher&&) = delete;
	GObjectWatcher&operator=(const GObjectWatcher&) = delete;
	GObjectWatcher&operator=(GObjectWatcher&&) = delete;

	virtual ~GObjectWatcher() {
		if (weakRef) {
			g_object_weak_unref(weakRef, WeakNotify, this);
		}
	}

	virtual void Destroyed() {}

	bool IsDestroyed() const {
		return weakRef != nullptr;
	}
};

std::string ConvertText(const char *s, size_t len, const char *charSetDest,
			const char *charSetSource, bool transliterations, bool silent=false);

}

#endif
