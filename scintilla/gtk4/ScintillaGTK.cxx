// Scintilla source code edit control
// ScintillaGTK.cxx - GTK+ specific subclass of ScintillaBase
// Copyright 1998-2004 by Neil Hodgson <neilh@scintilla.org>
// Copyright 2025 by Xuges <xuges@qq.com>
// The License.txt file describes the conditions under which this software may be distributed.

#include <cstddef>
#include <cstdlib>
#include <cstdint>
#include <cassert>
#include <cstring>
#include <cstdio>
#include <ctime>
#include <cmath>

#include <stdexcept>
#include <new>
#include <string>
#include <string_view>
#include <vector>
#include <map>
#include <set>
#include <optional>
#include <algorithm>
#include <memory>

#include <glib.h>
#include <gmodule.h>
#include <gdk/gdk.h>
#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>
#if defined(GDK_WINDOWING_WAYLAND)
#include <gdk/gdkwayland.h>
#endif

#include "ScintillaTypes.h"
#include "ScintillaMessages.h"
#include "ScintillaStructures.h"
#include "ILoader.h"
#include "ILexer.h"

#include "Debugging.h"
#include "Geometry.h"
#include "Platform.h"

#include "Scintilla.h"
#include "ScintillaWidget.h"
#include "CharacterCategoryMap.h"
#include "Position.h"
#include "UniqueString.h"
#include "SplitVector.h"
#include "Partitioning.h"
#include "RunStyles.h"
#include "ContractionState.h"
#include "CellBuffer.h"
#include "CallTip.h"
#include "KeyMap.h"
#include "Indicator.h"
#include "LineMarker.h"
#include "Style.h"
#include "ViewStyle.h"
#include "CharClassify.h"
#include "Decoration.h"
#include "CaseFolder.h"
#include "Document.h"
#include "CaseConvert.h"
#include "UniConversion.h"
#include "Selection.h"
#include "PositionCache.h"
#include "EditModel.h"
#include "MarginView.h"
#include "EditView.h"
#include "Editor.h"
#include "AutoComplete.h"
#include "ScintillaBase.h"

#include "Wrappers.h"
#include "ScintillaGTK.h"
#include "scintilla-marshal.h"
#include "Converter.h"

#define IS_WIDGET_REALIZED(w) (gtk_widget_get_realized(GTK_WIDGET(w)))
#define IS_WIDGET_MAPPED(w) (gtk_widget_get_mapped(GTK_WIDGET(w)))

#define SC_INDICATOR_INPUT INDICATOR_IME
#define SC_INDICATOR_TARGET INDICATOR_IME+1
#define SC_INDICATOR_CONVERTED INDICATOR_IME+2
#define SC_INDICATOR_UNKNOWN INDICATOR_IME_MAX

using namespace Scintilla;
using namespace Scintilla::Internal;

// From PlatGTK.cxx
extern std::string UTF8FromLatin1(std::string_view text);
extern void Platform_Initialise();
extern void Platform_Finalise();

namespace {

enum {
	COMMAND_SIGNAL,
	NOTIFY_SIGNAL,
	LAST_SIGNAL
};

gint scintilla_signals[LAST_SIGNAL] = { 0 };

GtkWidget *PWidget(const Window &w) noexcept {
	return static_cast<GtkWidget *>(w.GetID());
}

bool SettingGet(GtkSettings *settings, const gchar *name, gpointer value) noexcept {
	if (!settings) {
		return false;
	}
	if (!g_object_class_find_property(G_OBJECT_GET_CLASS(
		G_OBJECT(settings)), name)) {
		return false;
	}
	g_object_get(G_OBJECT(settings), name, value, nullptr);
	return true;
}

}

FontOptions::FontOptions(GtkWidget *widget) noexcept {
	UniquePangoContext pcontext(gtk_widget_create_pango_context(widget));
	PLATFORM_ASSERT(pcontext);
	const cairo_font_options_t *options = pango_cairo_context_get_font_options(pcontext.get());
	// options is owned by the PangoContext so must not be freed.
	if (options) {
		// options is NULL on Win32
		antialias = cairo_font_options_get_antialias(options);
		order = cairo_font_options_get_subpixel_order(options);
		hint = cairo_font_options_get_hint_style(options);
	}
}

bool FontOptions::operator==(const FontOptions &other) const noexcept {
	return antialias == other.antialias &&
		order == other.order &&
		hint == other.hint;
}

ScintillaGTK *ScintillaGTK::FromWidget(GtkWidget *widget) noexcept {
	ScintillaObject *scio = SCINTILLA(widget);
	return static_cast<ScintillaGTK *>(scio->pscin);
}

ScintillaGTK::ScintillaGTK(_ScintillaObject *sci_) :
	adjustmentv(nullptr), adjustmenth(nullptr),
	verticalScrollBarWidth(30), horizontalScrollBarHeight(30),
	buttonMouse(0),
	capturedMouse(false), dragWasDropped(false),
	lastKey(0), rectangularSelectionModifier(SCMOD_CTRL),
	parentClass(nullptr),
	preeditInitialized(false),
	im_context(nullptr),
	lastNonCommonScript(G_UNICODE_SCRIPT_INVALID_CODE),
	settings(nullptr),
	settingsHandlerId(0),
	lastWheelMouseTime(0),
	lastWheelMouseDirection(0),
	wheelMouseIntensity(0),
	smoothScrollY(0),
	smoothScrollX(0),
	rgnUpdate(nullptr),
	repaintFullWindow(false),
	styleIdleID(0),
	accessibilityEnabled(SC_ACCESSIBILITY_ENABLED) {
	sci = sci_;
	wMain = GTK_WIDGET(sci);

	rectangularSelectionModifier = SCMOD_ALT;
	linesPerScroll = 4; //TODO: get gsettings scroll lines
	primarySelection = false;

	Init();
}

ScintillaGTK::~ScintillaGTK() {
	if (styleIdleID) {
		g_source_remove(styleIdleID);
		styleIdleID = 0;
	}
	if (scrollBarIdleID) {
		g_source_remove(scrollBarIdleID);
		scrollBarIdleID = 0;
	}
	ClearPrimarySelection();
	//wPreedit.Destroy();
	if (settingsHandlerId) {
		g_signal_handler_disconnect(settings, settingsHandlerId);
	}
}

void ScintillaGTK::RealizeThis(GtkWidget *widget) {
	//Platform::DebugPrintf("ScintillaGTK::realize this\n");

	parentClass->realize(widget);

	gtk_im_context_set_client_widget(im_context.get(), widget);

	preeditInitialized = false;
	//gtk_widget_realize(PWidget(wPreedit));
	//gtk_widget_realize(PWidget(wPreeditDraw));

	//GtkWidget *widtxt = PWidget(wText);
	/*gtk_widget_realize(widtxt);
	gtk_widget_realize(PWidget(scrollbarv));
	gtk_widget_realize(PWidget(scrollbarh));*/
}

void ScintillaGTK::Realize(GtkWidget *widget) {
	ScintillaGTK *sciThis = FromWidget(widget);
	sciThis->RealizeThis(widget);
}

void ScintillaGTK::UnRealizeThis(GtkWidget *widget) {
	try {
		//if (IS_WIDGET_MAPPED(widget)) {
		//	gtk_widget_unmap(widget);
		//}

		gtk_im_context_set_client_widget(im_context.get(), nullptr);
		parentClass->unrealize(widget);

		//gtk_widget_unrealize(PWidget(wText));
		//if (PWidget(scrollbarv))
		//	gtk_widget_unrealize(PWidget(scrollbarv));
		//if (PWidget(scrollbarh))
		//	gtk_widget_unrealize(PWidget(scrollbarh));
		//gtk_widget_unrealize(PWidget(wPreedit));
		//gtk_widget_unrealize(PWidget(wPreeditDraw));
		//im_context.reset();
		//if (GTK_WIDGET_CLASS(parentClass)->unrealize)
		//	GTK_WIDGET_CLASS(parentClass)->unrealize(widget);

		//Finalise();
	} catch (...) {
		errorStatus = Status::Failure;
	}
}

void ScintillaGTK::UnRealize(GtkWidget *widget) {
	ScintillaGTK *sciThis = FromWidget(widget);
	sciThis->UnRealizeThis(widget);
}

void ScintillaGTK::MapThis() {
	try {
		//Platform::DebugPrintf("ScintillaGTK::map this\n");
		//MapWidget(PWidget(wText));
		//MapWidget(PWidget(scrollbarh));
		//MapWidget(PWidget(scrollbarv));
		//wMain.SetCursor(Window::Cursor::arrow);
		//scrollbarv.SetCursor(Window::Cursor::arrow);
		//scrollbarh.SetCursor(Window::Cursor::arrow);
		parentClass->map(PWidget(wMain));
		SetClientRectangle();
		ChangeSize();
	} catch (...) {
		errorStatus = Status::Failure;
	}
}

void ScintillaGTK::Map(GtkWidget *widget) {
	ScintillaGTK *sciThis = FromWidget(widget);
	sciThis->MapThis();
}

void ScintillaGTK::UnMapThis() {
	try {
		//Platform::DebugPrintf("ScintillaGTK::unmap this\n");
		DropGraphics();
		parentClass->unmap(PWidget(wMain));
		//gtk_widget_unmap(PWidget(wText));
		//if (PWidget(scrollbarh))
		//	gtk_widget_unmap(PWidget(scrollbarh));
		//if (PWidget(scrollbarv))
		//	gtk_widget_unmap(PWidget(scrollbarv));
	} catch (...) {
		errorStatus = Status::Failure;
	}
}

void ScintillaGTK::UnMap(GtkWidget *widget) {
	ScintillaGTK *sciThis = FromWidget(widget);
	sciThis->UnMapThis();
}

namespace {

class PreEditString {
public:
	gchar *str;
	gint cursor_pos;
	PangoAttrList *attrs;
	gboolean validUTF8;
	glong uniStrLen;
	gunichar *uniStr;
	GUnicodeScript pscript;

	explicit PreEditString(GtkIMContext *im_context) noexcept {
		gtk_im_context_get_preedit_string(im_context, &str, &attrs, &cursor_pos);
		validUTF8 = g_utf8_validate(str, strlen(str), nullptr);
		uniStr = g_utf8_to_ucs4_fast(str, static_cast<glong>(strlen(str)), &uniStrLen);
		pscript = g_unichar_get_script(uniStr[0]);
	}
	// Deleted so PreEditString objects can not be copied.
	PreEditString(const PreEditString&) = delete;
	PreEditString(PreEditString&&) = delete;
	PreEditString&operator=(const PreEditString&) = delete;
	PreEditString&operator=(PreEditString&&) = delete;
	~PreEditString() {
		g_free(str);
		g_free(uniStr);
		pango_attr_list_unref(attrs);
	}
};

}

gint ScintillaGTK::FocusInThis() {
	try {
		SetFocusState(true);

		if (im_context) {
			gtk_im_context_focus_in(im_context.get());
			/*
			PreEditString pes(im_context.get());
			if (PWidget(wPreedit)) {
				if (!preeditInitialized) {
					GtkWidget *top = gtk_widget_get_toplevel(PWidget(wMain));
					gtk_window_set_transient_for(GTK_WINDOW(PWidget(wPreedit)), GTK_WINDOW(top));
					preeditInitialized = true;
				}

				if (strlen(pes.str) > 0) {
					gtk_widget_show(PWidget(wPreedit));
				} else {
					gtk_widget_hide(PWidget(wPreedit));
				}
			}
			*/
		}
	} catch (...) {
		errorStatus = Status::Failure;
	}
	return FALSE;
}

void ScintillaGTK::FocusNotify(GtkEventControllerFocus* controller, GParamSpec* pspec, ScintillaGTK* sciThis)
{
	bool focus = gtk_event_controller_focus_is_focus(controller);
	if (focus)
		sciThis->FocusInThis();
	else
		sciThis->FocusOutThis();
}

gint ScintillaGTK::FocusOutThis() {
	try {
		SetFocusState(false);
		/*
		if (PWidget(wPreedit))
			gtk_widget_hide(PWidget(wPreedit));
		*/
		if (im_context)
			gtk_im_context_focus_out(im_context.get());

	} catch (...) {
		errorStatus = Status::Failure;
	}
	return FALSE;
}

void ScintillaGTK::SizeRequest(GtkWidget *widget, GtkRequisition *requisition) {
	const ScintillaGTK *sciThis = FromWidget(widget);
	requisition->width = 1;
	requisition->height = 1;
	GtkRequisition child_requisition;
#if GTK_CHECK_VERSION(3,0,0)
	gtk_widget_get_preferred_size(PWidget(sciThis->scrollbarh), nullptr, &child_requisition);
	gtk_widget_get_preferred_size(PWidget(sciThis->scrollbarv), nullptr, &child_requisition);
#else
	gtk_widget_size_request(PWidget(sciThis->scrollbarh), &child_requisition);
	gtk_widget_size_request(PWidget(sciThis->scrollbarv), &child_requisition);
#endif
}

void ScintillaGTK::GetPreferredWidth(GtkWidget *widget, gint *minimalWidth, gint *naturalWidth) {
	GtkRequisition requisition;
	SizeRequest(widget, &requisition);
	*minimalWidth = *naturalWidth = requisition.width;
}

void ScintillaGTK::GetPreferredHeight(GtkWidget *widget, gint *minimalHeight, gint *naturalHeight) {
	GtkRequisition requisition;
	SizeRequest(widget, &requisition);
	*minimalHeight = *naturalHeight = requisition.height;
}

void ScintillaGTK::SizeAllocate(GtkWidget *widget, int width, int height, int baseline) {
	ScintillaGTK *sciThis = FromWidget(widget);
	try {
		sciThis->Resize(width, height);
	} catch (...) {
		sciThis->errorStatus = Status::Failure;
	}
}

void ScintillaGTK::Init() {
	parentClass = static_cast<GtkWidgetClass *>(
			      g_type_class_ref(gtk_widget_get_type()));

	GtkWidget* wid = PWidget(wMain);
	gtk_widget_set_focusable(wid, true);
	gtk_widget_set_can_focus(wid, true);

	im_context.reset(gtk_im_multicontext_new());
	g_signal_connect(G_OBJECT(im_context.get()), "commit",
		G_CALLBACK(Commit), this);
	g_signal_connect(G_OBJECT(im_context.get()), "preedit-changed",
		G_CALLBACK(PreeditChanged), this);
	g_signal_connect(G_OBJECT(im_context.get()), "retrieve-surrounding",
		G_CALLBACK(RetrieveSurrounding), this);
	g_signal_connect(G_OBJECT(im_context.get()), "delete-surrounding",
		G_CALLBACK(DeleteSurrounding), this);

	GtkEventController* motionEvent = gtk_event_controller_motion_new();
	g_signal_connect(G_OBJECT(motionEvent), "motion", G_CALLBACK(Motion), this);
	gtk_widget_add_controller(wid, motionEvent);

	GtkEventController* focusEvent = gtk_event_controller_focus_new();
	g_signal_connect(G_OBJECT(focusEvent), "notify::is-focus", G_CALLBACK(FocusNotify), this);
	gtk_widget_add_controller(wid, focusEvent);

	// primary click
	GtkGesture* clickEvent = gtk_gesture_click_new();
	gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(clickEvent), GDK_BUTTON_PRIMARY);
	g_signal_connect(G_OBJECT(clickEvent), "pressed", G_CALLBACK(MousePress), this);
	g_signal_connect(G_OBJECT(clickEvent), "released", G_CALLBACK(MouseRelease), this);
	gtk_widget_add_controller(wid, GTK_EVENT_CONTROLLER(clickEvent));

	// middle click
	clickEvent = gtk_gesture_click_new();
	gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(clickEvent), GDK_BUTTON_MIDDLE);
	g_signal_connect(G_OBJECT(clickEvent), "pressed", G_CALLBACK(MousePress), this);
	g_signal_connect(G_OBJECT(clickEvent), "released", G_CALLBACK(MouseRelease), this);
	gtk_widget_add_controller(wid, GTK_EVENT_CONTROLLER(clickEvent));

	// right click
	clickEvent = gtk_gesture_click_new();
	gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(clickEvent), GDK_BUTTON_SECONDARY);
	g_signal_connect(G_OBJECT(clickEvent), "pressed", G_CALLBACK(MousePress), this);
	g_signal_connect(G_OBJECT(clickEvent), "released", G_CALLBACK(MouseRelease), this);
	gtk_widget_add_controller(wid, GTK_EVENT_CONTROLLER(clickEvent));

	GtkEventController* keyEvent = gtk_event_controller_key_new();
	g_signal_connect(G_OBJECT(keyEvent), "key-pressed", G_CALLBACK(KeyPress), this);
	//g_signal_connect(G_OBJECT(keyEvent), "key-released", G_CALLBACK(KeyRelease), this);
	gtk_event_controller_key_set_im_context(GTK_EVENT_CONTROLLER_KEY(keyEvent), im_context.get());
	gtk_widget_add_controller(wid, keyEvent);

	// scroll
	auto scrollFlags = GtkEventControllerScrollFlags(GTK_EVENT_CONTROLLER_SCROLL_BOTH_AXES | GTK_EVENT_CONTROLLER_SCROLL_DISCRETE);
	GtkEventController* scrollEvent = gtk_event_controller_scroll_new(scrollFlags);
	g_signal_connect(G_OBJECT(scrollEvent), "scroll", G_CALLBACK(ScrollEvent), this);
	gtk_widget_add_controller(wid, scrollEvent);
	

	wText = gtk_drawing_area_new();
	GtkWidget* widtxt = PWidget(wText);
	gtk_widget_set_parent(widtxt, wid);
	gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(widtxt), (GtkDrawingAreaDrawFunc)ScintillaGTK::DrawTextCb, this, nullptr);
	gtk_widget_set_size_request(widtxt, 100, 100);

	adjustmentv = GTK_ADJUSTMENT(gtk_adjustment_new(0.0, 0.0, 201.0, 1.0, 20.0, 20.0));
	scrollbarv = gtk_scrollbar_new(GTK_ORIENTATION_VERTICAL, GTK_ADJUSTMENT(adjustmentv));
	gtk_widget_set_can_focus(PWidget(scrollbarv), FALSE);
	g_signal_connect(G_OBJECT(adjustmentv), "value_changed",
			 G_CALLBACK(ScrollSignal), this);
	gtk_widget_set_parent(PWidget(scrollbarv), PWidget(wMain));
	gtk_widget_show(PWidget(scrollbarv));

	adjustmenth = GTK_ADJUSTMENT(gtk_adjustment_new(0.0, 0.0, 101.0, 1.0, 20.0, 20.0));
	scrollbarh = gtk_scrollbar_new(GTK_ORIENTATION_HORIZONTAL, GTK_ADJUSTMENT(adjustmenth));
	gtk_widget_set_can_focus(PWidget(scrollbarh), FALSE);
	g_signal_connect(G_OBJECT(adjustmenth), "value_changed",
			 G_CALLBACK(ScrollHSignal), this);
	gtk_widget_set_parent(PWidget(scrollbarh), PWidget(wMain));
	gtk_widget_show(PWidget(scrollbarh));

	gtk_widget_grab_focus(PWidget(wMain));

	// TODO: drag drop support
	//gtk_drag_dest_set(GTK_WIDGET(PWidget(wMain)),
	//		  GTK_DEST_DEFAULT_ALL, clipboardPasteTargets, nClipboardPasteTargets,
	//		  actionCopyOrMove);
	//GtkDragSource* dragSource = gtk_drag_source_new();
	//GtkDropTarget* dropTarget = gtk_drop_target_new();

	/* create pre-edit window */
	//wPreedit = gtk_popover_new();
	//wPreeditDraw = gtk_drawing_area_new();
	//GtkWidget *predrw = PWidget(wPreeditDraw);
	//gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(predrw), GtkDrawingAreaDrawFunc(DrawPreedit), this, nullptr);
	//gtk_widget_set_parent(PWidget(wPreedit), PWidget(wMain));
	//gtk_popover_set_child(GTK_POPOVER(PWidget(wPreedit)), predrw);
	//gtk_popover_present(GTK_POPOVER(PWidget(wPreedit)));

	settings = gtk_settings_get_default();

	// Set caret period based on GTK settings
	gboolean blinkOn = false;
	SettingGet(settings, "gtk-cursor-blink", &blinkOn);
	if (blinkOn) {
		gint value = 500;
		if (SettingGet(settings, "gtk-cursor-blink-time", &value)) {
			caret.period = static_cast<int>(value / 1.75);
		}
	} else {
		caret.period = 0;
	}

	using NotifyLambda = void (*)(GObject*, GParamSpec*, ScintillaGTK*);
	settingsHandlerId = g_signal_connect(settings, "notify::gtk-xft-dpi",
		G_CALLBACK(static_cast<NotifyLambda>([](GObject*, GParamSpec*, ScintillaGTK* sciThis) {
			sciThis->InvalidateStyleRedraw();
			})),
		this);

	gtk_widget_set_cursor_from_name(PWidget(wText), "text");
	gtk_widget_set_cursor_from_name(PWidget(scrollbarv), "default");
	gtk_widget_set_cursor_from_name(PWidget(scrollbarh), "default");

	for (size_t tr = static_cast<size_t>(TickReason::caret); tr <= static_cast<size_t>(TickReason::dwell); tr++) {
		timers[tr].reason = static_cast<TickReason>(tr);
		timers[tr].scintilla = this;
	}

	vs.indicators[SC_INDICATOR_UNKNOWN] = Indicator(IndicatorStyle::Hidden, colourIME);
	vs.indicators[SC_INDICATOR_INPUT] = Indicator(IndicatorStyle::Dots, colourIME);
	vs.indicators[SC_INDICATOR_CONVERTED] = Indicator(IndicatorStyle::CompositionThick, colourIME);
	vs.indicators[SC_INDICATOR_TARGET] = Indicator(IndicatorStyle::StraightBox, colourIME);

	fontOptionsPrevious = FontOptions(PWidget(wText));

	// avoid over-drawing
	constexpr guint FrameRate = 30;
	drawTimer = g_timeout_add(1000 / FrameRate, [](gpointer data)->gboolean
		{
			auto self = (ScintillaGTK*)data;
			if (self->needDraw)
			{
				self->needDraw = false;
				gtk_widget_queue_draw(PWidget(self->wText));
			}
			return G_SOURCE_CONTINUE;
		}, this
	);
}

void ScintillaGTK::Finalise() {
	for (size_t tr = static_cast<size_t>(TickReason::caret); tr <= static_cast<size_t>(TickReason::dwell); tr++) {
		FineTickerCancel(static_cast<TickReason>(tr));
	}

	g_source_remove(drawTimer);
	ScintillaBase::Finalise();
}

bool ScintillaGTK::AbandonPaint() {
	if ((paintState == PaintState::painting) && !paintingAllText) {
		repaintFullWindow = true;
	}
	return false;
}

void ScintillaGTK::DisplayCursor(Window::Cursor c) {
	if (cursorMode == CursorShape::Normal)
		wText.SetCursor(c);
	else
		wText.SetCursor(static_cast<Window::Cursor>(cursorMode));
}

bool ScintillaGTK::DragThreshold(Point ptStart, Point ptNow) {
	return gtk_drag_check_threshold(GTK_WIDGET(PWidget(wMain)),
		static_cast<gint>(ptStart.x), static_cast<gint>(ptStart.y),
		static_cast<gint>(ptNow.x), static_cast<gint>(ptNow.y));
}

void ScintillaGTK::StartDrag() {
	// TODO: add drag support
}

namespace Scintilla::Internal {

std::string ConvertText(const char *s, size_t len, const char *charSetDest,
			const char *charSetSource, bool transliterations, bool silent) {
	// s is not const because of different versions of iconv disagreeing about const
	std::string destForm;
	Converter conv(charSetDest, charSetSource, transliterations);
	if (conv) {
		gsize outLeft = len*3+1;
		destForm = std::string(outLeft, '\0');
		// g_iconv does not actually write to its input argument so safe to cast away const
		char *pin = const_cast<char *>(s);
		gsize inLeft = len;
		char *putf = &destForm[0];
		char *pout = putf;
		const gsize conversions = conv.Convert(&pin, &inLeft, &pout, &outLeft);
		if (conversions == sizeFailure) {
			if (!silent) {
				if (len == 1)
					fprintf(stderr, "iconv %s->%s failed for %0x '%s'\n",
						charSetSource, charSetDest, static_cast<unsigned char>(*s), s);
				else
					fprintf(stderr, "iconv %s->%s failed for %s\n",
						charSetSource, charSetDest, s);
			}
			destForm = std::string();
		} else {
			destForm.resize(pout - putf);
		}
	} else {
		fprintf(stderr, "Can not iconv %s %s\n", charSetDest, charSetSource);
	}
	return destForm;
}
}

// Returns the target converted to UTF8.
// Return the length in bytes.
Sci::Position ScintillaGTK::TargetAsUTF8(char *text) const {
	const Sci::Position targetLength = targetRange.Length();
	if (IsUnicodeMode()) {
		if (text) {
			pdoc->GetCharRange(text, targetRange.start.Position(), targetLength);
		}
	} else {
		// Need to convert
		const char *charSetBuffer = CharacterSetID();
		if (*charSetBuffer) {
			std::string s = RangeText(targetRange.start.Position(), targetRange.end.Position());
			std::string tmputf = ConvertText(&s[0], targetLength, "UTF-8", charSetBuffer, false);
			if (text) {
				memcpy(text, tmputf.c_str(), tmputf.length());
			}
			return tmputf.length();
		} else {
			if (text) {
				pdoc->GetCharRange(text, targetRange.start.Position(), targetLength);
			}
		}
	}
	return targetLength;
}

// Translates a nul terminated UTF8 string into the document encoding.
// Return the length of the result in bytes.
Sci::Position ScintillaGTK::EncodedFromUTF8(const char *utf8, char *encoded) const {
	const Sci::Position inputLength = (lengthForEncode >= 0) ? lengthForEncode : strlen(utf8);
	if (IsUnicodeMode()) {
		if (encoded) {
			memcpy(encoded, utf8, inputLength);
		}
		return inputLength;
	} else {
		// Need to convert
		const char *charSetBuffer = CharacterSetID();
		if (*charSetBuffer) {
			std::string tmpEncoded = ConvertText(utf8, inputLength, charSetBuffer, "UTF-8", true);
			if (encoded) {
				memcpy(encoded, tmpEncoded.c_str(), tmpEncoded.length());
			}
			return tmpEncoded.length();
		} else {
			if (encoded) {
				memcpy(encoded, utf8, inputLength);
			}
			return inputLength;
		}
	}
	// Fail
	return 0;
}

bool ScintillaGTK::ValidCodePage(int codePage) const {
	return codePage == 0
	       || codePage == SC_CP_UTF8
	       || codePage == 932
	       || codePage == 936
	       || codePage == 949
	       || codePage == 950
	       || codePage == 1361;
}

std::string ScintillaGTK::UTF8FromEncoded(std::string_view encoded) const {
	if (IsUnicodeMode()) {
		return std::string(encoded);
	} else {
		const char *charSetBuffer = CharacterSetID();
		return ConvertText(encoded.data(), encoded.length(), "UTF-8", charSetBuffer, true);
	}
}

std::string ScintillaGTK::EncodedFromUTF8(std::string_view utf8) const {
	if (IsUnicodeMode()) {
		return std::string(utf8);
	} else {
		const char *charSetBuffer = CharacterSetID();
		return ConvertText(utf8.data(), utf8.length(), charSetBuffer, "UTF-8", true);
	}
}

sptr_t ScintillaGTK::WndProc(Message iMessage, uptr_t wParam, sptr_t lParam) {
	try {
		switch (iMessage) {

		case Message::GrabFocus:
			gtk_widget_grab_focus(PWidget(wMain));
			break;

		case Message::GetDirectFunction:
			return reinterpret_cast<sptr_t>(DirectFunction);

		case Message::GetDirectStatusFunction:
			return reinterpret_cast<sptr_t>(DirectStatusFunction);

		case Message::GetDirectPointer:
			return reinterpret_cast<sptr_t>(this);

		case Message::TargetAsUTF8:
			return TargetAsUTF8(CharPtrFromSPtr(lParam));

		case Message::EncodedFromUTF8:
			return EncodedFromUTF8(ConstCharPtrFromUPtr(wParam),
					       CharPtrFromSPtr(lParam));

		case Message::SetRectangularSelectionModifier:
			rectangularSelectionModifier = static_cast<int>(wParam);
			break;

		case Message::GetRectangularSelectionModifier:
			return rectangularSelectionModifier;

		case Message::SetReadOnly: {
				const sptr_t ret = ScintillaBase::WndProc(iMessage, wParam, lParam);
				return ret;
			}

		case Message::GetAccessibility:
			return accessibilityEnabled;

		case Message::SetAccessibility:
			accessibilityEnabled = static_cast<int>(wParam);
			break;

		default:
			return ScintillaBase::WndProc(iMessage, wParam, lParam);
		}
	} catch (std::bad_alloc &) {
		errorStatus = Status::BadAlloc;
	} catch (...) {
		errorStatus = Status::Failure;
	}
	return 0;
}

sptr_t ScintillaGTK::DefWndProc(Message, uptr_t, sptr_t) {
	return 0;
}

bool ScintillaGTK::FineTickerRunning(TickReason reason) {
	return timers[static_cast<size_t>(reason)].timer != 0;
}

void ScintillaGTK::FineTickerStart(TickReason reason, int millis, int /* tolerance */) {
	FineTickerCancel(reason);
	const size_t reasonIndex = static_cast<size_t>(reason);
	timers[reasonIndex].timer = g_timeout_add(millis, TimeOut, &timers[reasonIndex]);
}

void ScintillaGTK::FineTickerCancel(TickReason reason) {
	const size_t reasonIndex = static_cast<size_t>(reason);
	if (timers[reasonIndex].timer) {
		g_source_remove(timers[reasonIndex].timer);
		timers[reasonIndex].timer = 0;
	}
}

bool ScintillaGTK::SetIdle(bool on) {
	if (on) {
		// Start idler, if it's not running.
		if (!idler.state) {
			idler.state = true;
			idler.idlerID = GUINT_TO_POINTER(g_idle_add(IdleCallback, this));
		}
	} else {
		// Stop idler, if it's running
		if (idler.state) {
			idler.state = false;
			g_source_remove(GPOINTER_TO_UINT(idler.idlerID));
		}
	}
	return true;
}

void ScintillaGTK::SetMouseCapture(bool on) {
	if (mouseDownCaptures) {
		/*if (on) {
			gtk_grab_add(GTK_WIDGET(PWidget(wMain))); TODO: how to change this?
		} else {
			gtk_grab_remove(GTK_WIDGET(PWidget(wMain)));
		}*/
	}
	capturedMouse = on;
}

bool ScintillaGTK::HaveMouseCapture() {
	return capturedMouse;
}

namespace {

// Is crcTest completely in crcContainer?
bool CRectContains(const cairo_rectangle_t &crcContainer, const cairo_rectangle_t &crcTest) {
	return
		(crcTest.x >= crcContainer.x) && ((crcTest.x + crcTest.width) <= (crcContainer.x + crcContainer.width)) &&
		(crcTest.y >= crcContainer.y) && ((crcTest.y + crcTest.height) <= (crcContainer.y + crcContainer.height));
}

// Is crcTest completely in crcListContainer?
// May incorrectly return false if complex shape
bool CRectListContains(const cairo_rectangle_list_t *crcListContainer, const cairo_rectangle_t &crcTest) {
	for (int r=0; r<crcListContainer->num_rectangles; r++) {
		if (CRectContains(crcListContainer->rectangles[r], crcTest))
			return true;
	}
	return false;
}

}

bool ScintillaGTK::PaintContains(PRectangle rc) {
	// This allows optimization when a rectangle is completely in the update region.
	// It is OK to return false when too difficult to determine as that just performs extra drawing
	bool contains = true;
	if (paintState == PaintState::painting) {
		if (!rcPaint.Contains(rc)) {
			contains = false;
		} else if (rgnUpdate) {

			cairo_rectangle_t grc = {rc.left, rc.top,
						 rc.right - rc.left, rc.bottom - rc.top
						};
			contains = CRectListContains(rgnUpdate, grc);
		}
	}
	return contains;
}

// Redraw all of text area. This paint will not be abandoned.
void ScintillaGTK::FullPaint() {
	wText.InvalidateAll();
}

void ScintillaGTK::SetClientRectangle() {
	rectangleClient = wMain.GetClientPosition();
}

PRectangle ScintillaGTK::GetClientRectangle() const {
	PRectangle rc = rectangleClient;
	if (verticalScrollBarVisible)
		rc.right -= verticalScrollBarWidth;
	if (horizontalScrollBarVisible && !Wrapping())
		rc.bottom -= horizontalScrollBarHeight;
	// Move to origin
	rc.right -= rc.left;
	rc.bottom -= rc.top;
	if (rc.bottom < 0)
		rc.bottom = 0;
	if (rc.right < 0)
		rc.right = 0;
	rc.left = 0;
	rc.top = 0;
	return rc;
}

void ScintillaGTK::ScrollText(Sci::Line linesToMove) {
	NotifyUpdateUI();
	Redraw();
}

void ScintillaGTK::SetVerticalScrollPos() {
	DwellEnd(true);
	if (!scrollBarIdleID)
		gtk_adjustment_set_value(GTK_ADJUSTMENT(adjustmentv), static_cast<gdouble>(topLine));
}

void ScintillaGTK::SetHorizontalScrollPos() {
	DwellEnd(true);
	if (!scrollBarIdleID)
		gtk_adjustment_set_value(GTK_ADJUSTMENT(adjustmenth), xOffset);
}

bool ScintillaGTK::ModifyScrollBars(Sci::Line nMax, Sci::Line nPage) {
	bool modified = false;
	const int pageScroll = static_cast<int>(LinesToScroll());

	if (gtk_adjustment_get_upper(adjustmentv) != (nMax + 1) ||
			gtk_adjustment_get_page_size(adjustmentv) != nPage ||
			gtk_adjustment_get_page_increment(adjustmentv) != pageScroll) {
		gtk_adjustment_set_upper(adjustmentv, nMax + 1.0);
		gtk_adjustment_set_page_size(adjustmentv, static_cast<gdouble>(nPage));
		gtk_adjustment_set_page_increment(adjustmentv, pageScroll);
		gtk_adjustment_set_value(GTK_ADJUSTMENT(adjustmentv), static_cast<gdouble>(topLine));
		modified = true;
	}

	const PRectangle rcText = GetTextRectangle();
	int horizEndPreferred = scrollWidth;
	if (horizEndPreferred < 0)
		horizEndPreferred = 0;
	const unsigned int pageWidth = static_cast<unsigned int>(rcText.Width());
	const unsigned int pageIncrement = pageWidth / 3;
	const unsigned int charWidth = static_cast<unsigned int>(vs.styles[STYLE_DEFAULT].aveCharWidth);
	if (gtk_adjustment_get_upper(adjustmenth) != horizEndPreferred ||
			gtk_adjustment_get_page_size(adjustmenth) != pageWidth ||
			gtk_adjustment_get_page_increment(adjustmenth) != pageIncrement ||
			gtk_adjustment_get_step_increment(adjustmenth) != charWidth) {
		gtk_adjustment_set_upper(adjustmenth, horizEndPreferred);
		gtk_adjustment_set_page_size(adjustmenth, pageWidth);
		gtk_adjustment_set_page_increment(adjustmenth, pageIncrement);
		gtk_adjustment_set_step_increment(adjustmenth, charWidth);
		gtk_adjustment_set_value(GTK_ADJUSTMENT(adjustmenth), xOffset);
		modified = true;
	}
	if (modified && (paintState == PaintState::painting)) {
		repaintFullWindow = true;
	}

	return modified;
}

void ScintillaGTK::ReconfigureScrollBars() {
	const PRectangle rc = wMain.GetClientPosition();
	Resize(static_cast<int>(rc.Width()), static_cast<int>(rc.Height()));
}

void ScintillaGTK::SetScrollBars() {
	if (scrollBarIdleID) {
		// Only allow one scroll bar change to be queued
		return;
	}
	constexpr gint priorityScrollBar = GDK_PRIORITY_REDRAW + 5;
	// On GTK, unlike other platforms, modifying scrollbars inside some events including
	// resizes causes problems. Deferring the modification to a lower priority (125) idle
	// event avoids the problems. This code did not always work when the priority was
	// higher than GTK's resize (GTK_PRIORITY_RESIZE=110) or redraw
	// (GDK_PRIORITY_REDRAW=120) idle tasks.
	scrollBarIdleID = g_idle_add_full(priorityScrollBar,
		[](gpointer pSci) -> gboolean {
			ScintillaGTK *sciThis = static_cast<ScintillaGTK *>(pSci);
			sciThis->ChangeScrollBars();
			sciThis->scrollBarIdleID = 0;
			return FALSE;
		},
		this, nullptr);
}

void ScintillaGTK::NotifyChange() {
	g_signal_emit(G_OBJECT(sci), scintilla_signals[COMMAND_SIGNAL], 0,
		      Platform::LongFromTwoShorts(GetCtrlID(), SCEN_CHANGE), PWidget(wMain));
}

void ScintillaGTK::NotifyFocus(bool focus) {
	if (commandEvents)
		g_signal_emit(G_OBJECT(sci), scintilla_signals[COMMAND_SIGNAL], 0,
			      Platform::LongFromTwoShorts
			      (GetCtrlID(), focus ? SCEN_SETFOCUS : SCEN_KILLFOCUS), PWidget(wMain));
	Editor::NotifyFocus(focus);
}

void ScintillaGTK::NotifyParent(NotificationData scn) {
	scn.nmhdr.hwndFrom = PWidget(wMain);
	scn.nmhdr.idFrom = GetCtrlID();
	g_signal_emit(G_OBJECT(sci), scintilla_signals[NOTIFY_SIGNAL], 0,
		      GetCtrlID(), &scn);
}

void ScintillaGTK::NotifyKey(Keys key, KeyMod modifiers) {
	NotificationData scn = {};
	scn.nmhdr.code = Notification::Key;
	scn.ch = static_cast<int>(key);
	scn.modifiers = modifiers;

	NotifyParent(scn);
}

void ScintillaGTK::NotifyURIDropped(const char *list) {
	NotificationData scn = {};
	scn.nmhdr.code = Notification::URIDropped;
	scn.text = list;

	NotifyParent(scn);
}

const char *CharacterSetID(CharacterSet characterSet);

const char *ScintillaGTK::CharacterSetID() const {
	return ::CharacterSetID(vs.styles[STYLE_DEFAULT].characterSet);
}

namespace {

class CaseFolderDBCS : public CaseFolderTable {
	const char *charSet;
public:
	explicit CaseFolderDBCS(const char *charSet_) noexcept : charSet(charSet_) {
	}
	size_t Fold(char *folded, size_t sizeFolded, const char *mixed, size_t lenMixed) override {
		if ((lenMixed == 1) && (sizeFolded > 0)) {
			folded[0] = mapping[static_cast<unsigned char>(mixed[0])];
			return 1;
		} else if (*charSet) {
			std::string sUTF8 = ConvertText(mixed, lenMixed,
							"UTF-8", charSet, false);
			if (!sUTF8.empty()) {
				UniqueStr mapped(g_utf8_casefold(sUTF8.c_str(), sUTF8.length()));
				size_t lenMapped = strlen(mapped.get());
				if (lenMapped < sizeFolded) {
					memcpy(folded, mapped.get(),  lenMapped);
				} else {
					folded[0] = '\0';
					lenMapped = 1;
				}
				return lenMapped;
			}
		}
		// Something failed so return a single NUL byte
		folded[0] = '\0';
		return 1;
	}
};

}

std::unique_ptr<CaseFolder> ScintillaGTK::CaseFolderForEncoding() {
	if (pdoc->dbcsCodePage == SC_CP_UTF8) {
		return std::make_unique<CaseFolderUnicode>();
	} else {
		const char *charSetBuffer = CharacterSetID();
		if (charSetBuffer) {
			if (pdoc->dbcsCodePage == 0) {
				std::unique_ptr<CaseFolderTable> pcf = std::make_unique<CaseFolderTable>();
				// Only for single byte encodings
				for (int i=0x80; i<0x100; i++) {
					char sCharacter[2] = "A";
					sCharacter[0] = i;
					// Silent as some bytes have no assigned character
					std::string sUTF8 = ConvertText(sCharacter, 1,
									"UTF-8", charSetBuffer, false, true);
					if (!sUTF8.empty()) {
						UniqueStr mapped(g_utf8_casefold(sUTF8.c_str(), sUTF8.length()));
						if (mapped) {
							std::string mappedBack = ConvertText(mapped.get(), strlen(mapped.get()),
											     charSetBuffer, "UTF-8", false, true);
							if ((mappedBack.length() == 1) && (mappedBack[0] != sCharacter[0])) {
								pcf->SetTranslation(sCharacter[0], mappedBack[0]);
							}
						}
					}
				}
				return pcf;
			} else {
				return std::make_unique<CaseFolderDBCS>(charSetBuffer);
			}
		}
		return nullptr;
	}
}

namespace {

struct CaseMapper {
	UniqueStr mapped;
	CaseMapper(const std::string &sUTF8, bool toUpperCase) noexcept {
		if (toUpperCase) {
			mapped.reset(g_utf8_strup(sUTF8.c_str(), sUTF8.length()));
		} else {
			mapped.reset(g_utf8_strdown(sUTF8.c_str(), sUTF8.length()));
		}
	}
};

}

std::string ScintillaGTK::CaseMapString(const std::string &s, CaseMapping caseMapping) {
	if (s.empty() || (caseMapping == CaseMapping::same))
		return s;

	if (IsUnicodeMode()) {
		std::string retMapped(s.length() * maxExpansionCaseConversion, 0);
		const size_t lenMapped = CaseConvertString(&retMapped[0], retMapped.length(), s.c_str(), s.length(),
					 (caseMapping == CaseMapping::upper) ? CaseConversion::upper : CaseConversion::lower);
		retMapped.resize(lenMapped);
		return retMapped;
	}

	const char *charSetBuffer = CharacterSetID();

	if (!*charSetBuffer) {
		CaseMapper mapper(s, caseMapping == CaseMapping::upper);
		return std::string(mapper.mapped.get());
	} else {
		// Change text to UTF-8
		std::string sUTF8 = ConvertText(s.c_str(), s.length(),
						"UTF-8", charSetBuffer, false);
		CaseMapper mapper(sUTF8, caseMapping == CaseMapping::upper);
		return ConvertText(mapper.mapped.get(), strlen(mapper.mapped.get()), charSetBuffer, "UTF-8", false);
	}
}

int ScintillaGTK::KeyDefault(Keys key, KeyMod modifiers) {
	// Pass up to container in case it is an accelerator
	NotifyKey(key, modifiers);
	return 0;
}

void ScintillaGTK::CopyToClipboard(const SelectionText &selectedText) {
	SelectionText *clipText = new SelectionText();
	clipText->Copy(selectedText);
	StoreOnClipboard(clipText);
}

void ScintillaGTK::Copy() {
	if (!sel.Empty()) {
		SelectionText *clipText = new SelectionText();
		CopySelectionRange(clipText);
		StoreOnClipboard(clipText);
	}
}

void ScintillaGTK::RequestSelection() {
	GdkClipboard* clipboard = gtk_widget_get_clipboard(GTK_WIDGET(PWidget(wMain)));
	gdk_clipboard_read_text_async(clipboard, nullptr,
		[](GObject* obj, GAsyncResult* res, gpointer p) {
			ScintillaGTK* sciThis = (ScintillaGTK*)p;
			sciThis->ReceivedClipboard(GDK_CLIPBOARD(obj), res);
		}, this
	);
}

void ScintillaGTK::Paste() {
	RequestSelection();
}

void ScintillaGTK::CreateCallTipWindow(PRectangle rc) {
	if (!ct.wCallTip.Created()) {
		ct.wCallTip = gtk_popover_new();
		ct.wDraw = gtk_drawing_area_new();
		GtkWidget *widcdrw = PWidget(ct.wDraw);
		gtk_popover_set_child(GTK_POPOVER(PWidget(ct.wCallTip)), widcdrw);
		gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(widcdrw), GtkDrawingAreaDrawFunc(DrawCT), this, nullptr);

		GtkGesture* clickEvent = gtk_gesture_click_new();
		gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(clickEvent), GDK_BUTTON_PRIMARY);
		g_signal_connect(G_OBJECT(clickEvent), "pressed", G_CALLBACK(PressCT), this);
		gtk_widget_add_controller(widcdrw, GTK_EVENT_CONTROLLER(clickEvent));

		gtk_widget_set_parent(PWidget(ct.wCallTip), PWidget(wMain));
	}
	const int width = static_cast<int>(rc.Width());
	const int height = static_cast<int>(rc.Height());
	gtk_widget_set_size_request(PWidget(ct.wDraw), width, height);
	ct.wDraw.Show();
}

namespace {
	std::string makeActionName(const char* label, size_t len)
	{
		std::string res;
		res.reserve(len);
		for (int i = 0; i < len; i++)
		{
			if (label[i] != ' ')
				res.push_back(label[i]);
		}
		return res;
	}
	std::string makeDetailedAction(const std::string& name)
	{
		return std::string("menu.").append(name);
	}
}

void ScintillaGTK::AddToPopUp(const char *label, int cmd, bool enabled) {
	size_t len = strlen(label);
	if (len != 0)
	{
		GMenu* menu = (GMenu*)gtk_popover_menu_get_menu_model(GTK_POPOVER_MENU(popup.GetID()));
		GActionMap* group = (GActionMap*)g_object_get_data(G_OBJECT(popup.GetID()), "group");

		std::string name = makeActionName(label, len);
		std::string detailed = makeDetailedAction(name);

		GSimpleAction* action = g_simple_action_new(name.c_str(), nullptr);
		g_simple_action_set_enabled(action, enabled);
		g_object_set_data(G_OBJECT(action), "CmdNum", GINT_TO_POINTER(cmd));
		g_signal_connect(G_OBJECT(action), "activate", G_CALLBACK(PopUpCB), this);
		g_action_map_add_action(group, G_ACTION(action));

		g_menu_append(menu, label, detailed.c_str());
	}
}

bool ScintillaGTK::OwnPrimarySelection() {
	return primarySelection;
}

void ScintillaGTK::ClearPrimarySelection() {
	// TODO: gtk4 not allow "primary clipboard"
	//if (primarySelection) {
	//	inClearSelection++;
	//	// Calls PrimaryClearSelection: primarySelection -> false
	//	gtk_clipboard_clear(gtk_clipboard_get(GDK_SELECTION_PRIMARY));
	//	inClearSelection--;
	//}
}

void ScintillaGTK::ClaimSelection() {
	// X Windows has a 'primary selection' as well as the clipboard.
	// Whenever the user selects some text, we become the primary selection
	ClearPrimarySelection();
	/*if (!sel.Empty()) {
		if (gtk_clipboard_set_with_data(
			gtk_clipboard_get(GDK_SELECTION_PRIMARY),
			clipboardCopyTargets, nClipboardCopyTargets,
			PrimaryGetSelection,
			PrimaryClearSelection,
			this)) {
			primarySelection = true;
		}
	}*/
}

// Detect rectangular text, convert line ends to current mode, convert from or to UTF-8
void ScintillaGTK::GetGtkSelectionText(GdkClipboard* clipboard, GAsyncResult* result, SelectionText& selText) {
	GError* error = nullptr;
	const char* data = gdk_clipboard_read_text_finish(clipboard, result, &error);
	if (error)
		return;

	size_t len = strlen(data);

	// Check for "\n\0" ending to string indicating that selection is rectangular
	bool isRectangular;
	isRectangular = ((len > 2) && (data[len - 1] == 0 && data[len - 2] == '\n'));
	if (isRectangular)
		len--;	// Forget the extra '\0'

	// Win32 includes an ending '\0' byte in 'len' for clipboard text from
	// external applications; ignore it.
	if ((len > 0) && (data[len - 1] == '\0'))
		len--;

	std::string dest(data, len); // UTF-8

	const char* charSetBuffer = CharacterSetID();
	if (!IsUnicodeMode() && *charSetBuffer) {
		// Convert to locale
		dest = ConvertText(dest.c_str(), dest.length(), charSetBuffer, "UTF-8", true);
		selText.Copy(dest, pdoc->dbcsCodePage,
			vs.styles[STYLE_DEFAULT].characterSet, isRectangular, false);
	}
	else {
		selText.Copy(dest, CpUtf8, CharacterSet::Ansi, isRectangular, false);
	}
}

void ScintillaGTK::InsertSelection(GdkClipboard* clipboard, GAsyncResult* res) {
	SelectionText selText;
	GetGtkSelectionText(clipboard, res, selText);

	UndoGroup ug(pdoc);
	ClearSelection(multiPasteMode == MultiPaste::Each);

	InsertPasteShape(selText.Data(), selText.Length(),
		selText.rectangular ? PasteShape::rectangular : PasteShape::stream);
	EnsureCaretVisible();

	Redraw();
}

void ScintillaGTK::ReceivedClipboard(GdkClipboard *clipboard, GAsyncResult* res) noexcept {
	try {
		InsertSelection(clipboard, res);
	} catch (...) {
		errorStatus = Status::Failure;
	}
}

void ScintillaGTK::StoreOnClipboard(SelectionText *clipText) {
	GdkClipboard* clipBoard = gtk_widget_get_clipboard(GTK_WIDGET(PWidget(wMain)));
	gdk_clipboard_set_text(clipBoard, clipText->Data());
}

void ScintillaGTK::Resize(int width, int height) {
	//Platform::DebugPrintf("Resize %d %d\n", width, height);
	//printf("Resize %d %d\n", width, height);

	// GTK+ 3 warns when we allocate smaller than the minimum allocation,
	// so we use these variables to store the minimum scrollbar lengths.
	int minVScrollBarHeight, minHScrollBarWidth;

	// Not always needed, but some themes can have different sizes of scrollbars
	GtkRequisition minimum, requisition;
	gtk_widget_get_preferred_size(PWidget(scrollbarv), &minimum, &requisition);
	minVScrollBarHeight = minimum.height;
	verticalScrollBarWidth = requisition.width;
	gtk_widget_get_preferred_size(PWidget(scrollbarh), &minimum, &requisition);
	minHScrollBarWidth = minimum.width;
	horizontalScrollBarHeight = requisition.height;


	// These allocations should never produce negative sizes as they would wrap around to huge
	// unsigned numbers inside GTK+ causing warnings.
	const bool showSBHorizontal = horizontalScrollBarVisible && !Wrapping();

	GtkAllocation alloc = {};
	if (showSBHorizontal) {
		gtk_widget_show(GTK_WIDGET(PWidget(scrollbarh)));
		alloc.x = 0;
		alloc.y = height - horizontalScrollBarHeight;
		alloc.width = std::max(minHScrollBarWidth, width - verticalScrollBarWidth);
		alloc.height = horizontalScrollBarHeight;
		gtk_widget_size_allocate(GTK_WIDGET(PWidget(scrollbarh)), &alloc, gtk_widget_get_baseline(PWidget(scrollbarh)));
	} else {
		gtk_widget_hide(GTK_WIDGET(PWidget(scrollbarh)));
		horizontalScrollBarHeight = 0; // in case horizontalScrollBarVisible is true.
	}

	if (verticalScrollBarVisible) {
		gtk_widget_show(GTK_WIDGET(PWidget(scrollbarv)));
		alloc.x = width - verticalScrollBarWidth;
		alloc.y = 0;
		alloc.width = verticalScrollBarWidth;
		alloc.height = std::max(minVScrollBarHeight, height - horizontalScrollBarHeight);
		gtk_widget_size_allocate(GTK_WIDGET(PWidget(scrollbarv)), &alloc, gtk_widget_get_baseline(PWidget(scrollbarv)));
	} else {
		gtk_widget_hide(GTK_WIDGET(PWidget(scrollbarv)));
		verticalScrollBarWidth = 0;
	}
	SetClientRectangle();
	if (IS_WIDGET_MAPPED(PWidget(wMain))) {
		ChangeSize();
	} else {
		const PRectangle rcTextArea = GetTextRectangle();
		if (wrapWidth != rcTextArea.Width()) {
			wrapWidth = rcTextArea.Width();
			NeedWrapping();
		}
	}

	alloc.x = 0;
	alloc.y = 0;
	alloc.width = 1;
	alloc.height = 1;

	// please GTK 3.20 and ask wText what size it wants, although we know it doesn't really need
	// anything special as it's ours.
	GtkWidget* widtext = PWidget(wText);
	gtk_widget_get_preferred_size(widtext, &requisition, nullptr);
	alloc.width = requisition.width;
	alloc.height = requisition.height;
	alloc.width = std::max(alloc.width, width - verticalScrollBarWidth);
	alloc.height = std::max(alloc.height, height - horizontalScrollBarHeight);
	gtk_widget_size_allocate(widtext, &alloc, gtk_widget_get_baseline(widtext));
}

namespace {

void SetAdjustmentValue(GtkAdjustment *object, int value) noexcept {
	GtkAdjustment *adjustment = GTK_ADJUSTMENT(object);
	const int maxValue = static_cast<int>(
				     gtk_adjustment_get_upper(adjustment) - gtk_adjustment_get_page_size(adjustment));

	if (value > maxValue)
		value = maxValue;
	if (value < 0)
		value = 0;
	gtk_adjustment_set_value(adjustment, value);
}

int modifierTranslated(int sciModifier) noexcept {
	switch (sciModifier) {
	case SCMOD_SHIFT:
		return GDK_SHIFT_MASK;
	case SCMOD_CTRL:
		return GDK_CONTROL_MASK;
	case SCMOD_ALT:
		return GDK_ALT_MASK;
	case SCMOD_SUPER:
		return GDK_SUPER_MASK;
	default:
		return 0;
	}
}

struct EventData
{
	GdkEvent* event;
	GdkModifierType state;
	guint time;

	Point getPosition() const
	{
		// Use floor as want to round in the same direction (-infinity) so
		// there is no stickiness crossing 0.0.
		gdouble x = 0, y = 0;
		gdk_event_get_position(event, &x, &y);
		return Point(static_cast<XYPOSITION>(std::floor(x)), static_cast<XYPOSITION>(std::floor(y)));
	}
};

EventData getEventData(GtkEventController* ctrl)
{
	EventData data;
	data.event = gtk_event_controller_get_current_event(ctrl);
	data.state = gtk_event_controller_get_current_event_state(ctrl);
	data.time = gtk_event_controller_get_current_event_time(ctrl);
	return data;
}
}

gint ScintillaGTK::MousePressThis(GtkGestureClick* self, gint nPress, gdouble x, gdouble y) {
	try {
		//Platform::DebugPrintf("Press %x time=%d state = %x button = %x\n",this,event->time, event->state, event->button);
		EventData event = getEventData(GTK_EVENT_CONTROLLER(self));
		buttonMouse = gtk_gesture_single_get_current_button(GTK_GESTURE_SINGLE(self));

		//evbtn.reset(gdk_event_copy(reinterpret_cast<GdkEvent *>(event)));

		const Point pt = Point(static_cast<XYPOSITION>(std::floor(x)), static_cast<XYPOSITION>(std::floor(y)));
		//const PRectangle rcClient = GetClientRectangle();
		//Platform::DebugPrintf("Press %0d,%0d in %0d,%0d %0d,%0d\n",
		//	pt.x, pt.y, rcClient.left, rcClient.top, rcClient.right, rcClient.bottom);

		const bool shift = (event.state & GDK_SHIFT_MASK) != 0;
		const bool ctrl = (event.state & GDK_CONTROL_MASK) != 0;
		// On X, instead of sending literal modifiers use the user specified
		// modifier, defaulting to control instead of alt.
		// This is because most X window managers grab alt + click for moving
		//const bool alt = (event.state & modifierTranslated(rectangularSelectionModifier)) != 0; TODO£ºsupport macOS
		const bool alt = (event.state & GDK_ALT_MASK) != 0;
		const bool meta = (event.state & GDK_META_MASK) != 0;

		gtk_widget_grab_focus(PWidget(wMain));
		if (buttonMouse == GDK_BUTTON_PRIMARY) {
			ButtonDownWithModifiers(pt, event.time, ModifierFlags(shift, ctrl, alt, meta));
		} else if (buttonMouse == GDK_BUTTON_MIDDLE) {
			// Grab the primary selection if it exists
			posPrimary = SPositionFromLocation(pt, false, false, UserVirtualSpace());
			if (OwnPrimarySelection() && primary.Empty())
				CopySelectionRange(&primary);

			sel.Clear();
			//RequestSelection(GDK_SELECTION_PRIMARY); // TODO: implement moddle button paste ?
		} else if (buttonMouse == GDK_BUTTON_SECONDARY) {
			if (!PointInSelection(pt))
				SetEmptySelection(PositionFromLocation(pt));
			if (ShouldDisplayPopup(pt)) {
				// PopUp menu

				/*int ox = 0;
				int oy = 0;
				gdk_window_get_origin(PWindow(wMain), &ox, &oy);
				ContextMenu(Point(pt.x + ox, pt.y + oy));*/
				ContextMenu(pt);
			} else {
				RightButtonDownWithModifiers(pt, event.time, ModifierFlags(shift, ctrl, alt, meta));
				return FALSE;
			}
		}
	} catch (...) {
		errorStatus = Status::Failure;
	}
	return TRUE;
}

gint ScintillaGTK::MousePress(GtkGestureClick* self, gint nPress, gdouble x, gdouble y, ScintillaGTK* sciThis) {
	return sciThis->MousePressThis(self, nPress, x, y);
}

gint ScintillaGTK::MouseRelease(GtkGestureClick* self, gint nPress, gdouble x, gdouble y, ScintillaGTK* sciThis) {
	return sciThis->MouseReleaseThis(self, nPress, x, y);
}

gint ScintillaGTK::MouseReleaseThis(GtkGestureClick* self, gint nPress, gdouble x, gdouble y) {
	try {
		//Platform::DebugPrintf("Release %x %d %d\n",sciThis,event->time,event->state);
		EventData event = getEventData(GTK_EVENT_CONTROLLER(self));
		guint eventButton = gtk_gesture_single_get_current_button(GTK_GESTURE_SINGLE(self));

		if (!HaveMouseCapture())
			return FALSE;
		if (eventButton == GDK_BUTTON_PRIMARY) {
			const Point pt = Point(static_cast<XYPOSITION>(std::floor(x)), static_cast<XYPOSITION>(std::floor(y)));

			//Platform::DebugPrintf("Up %x %x %d %d %d\n",
			//	sciThis,event->window,event->time, pt.x, pt.y);

			const bool shift = (event.state & GDK_SHIFT_MASK) != 0;
			const bool ctrl = (event.state & GDK_CONTROL_MASK) != 0;
			// On X, instead of sending literal modifiers use the user specified
			// modifier, defaulting to control instead of alt.
			// This is because most X window managers grab alt + click for moving
			//const bool alt = (event.state & modifierTranslated(rectangularSelectionModifier)) != 0; TODO£ºsupport macOS
			const bool alt = (event.state & GDK_ALT_MASK) != 0;
			const bool meta = (event.state & GDK_META_MASK) != 0;

			ButtonUpWithModifiers(pt, event.time, ModifierFlags(shift, ctrl, alt, meta));
		}
	} catch (...) {
		errorStatus = Status::Failure;
	}
	return FALSE;
}

gint ScintillaGTK::ScrollEvent(GtkEventControllerScroll* self, gdouble dx, gdouble dy, ScintillaGTK* sciThis) {
	return sciThis->ScrollEventThis(self, dx, dy);
}

gint ScintillaGTK::ScrollEventThis(GtkEventControllerScroll* self, gdouble dx, gdouble dy) {
	try {
		EventData event = getEventData(GTK_EVENT_CONTROLLER(self));
		GdkScrollDirection direction = gdk_scroll_event_get_direction(event.event);

		// Smooth scrolling (touch pad)
		if (direction == GDK_SCROLL_SMOOTH) {
			if (dx != 0)
			{
				// horizontal
				int hScroll = gtk_adjustment_get_step_increment(adjustmenth);
				hScroll *= dx * linesPerScroll; // scroll by this many characters
				HorizontalScrollTo(xOffset + hScroll);
			}
			if (dy != 0)
			{
				// vertical
				ScrollTo(topLine + dy * linesPerScroll);
			}
			return TRUE;
		}

		// Compute amount and direction to scroll (even tho on win32 there is
		// intensity of scrolling info in the native message, gtk doesn't
		// support this so we simulate similarly adaptive scrolling)
		// Note that this is disabled on macOS (Darwin) with the X11 backend
		// where the X11 server already has an adaptive scrolling algorithm
		// that fights with this one
		int cLineScroll;

		const gint64 curTime = g_get_monotonic_time();
		const gint64 timeDelta = curTime - lastWheelMouseTime;
		if ((direction == lastWheelMouseDirection) && (timeDelta < 250000)) {
			if (wheelMouseIntensity < 12)
				wheelMouseIntensity++;
			cLineScroll = wheelMouseIntensity;
		} else {
			cLineScroll = linesPerScroll;
			if (cLineScroll == 0)
				cLineScroll = 4;
			wheelMouseIntensity = cLineScroll;
		}
		lastWheelMouseTime = curTime;

		if (direction == GDK_SCROLL_UP || direction == GDK_SCROLL_LEFT) {
			cLineScroll *= -1;
		}
		lastWheelMouseDirection = direction;

		// Note:  Unpatched versions of win32gtk don't set the 'state' value so
		// only regular scrolling is supported there.  Also, unpatched win32gtk
		// issues spurious button 2 mouse events during wheeling, which can cause
		// problems (a patch for both was submitted by archaeopteryx.com on 13Jun2001)

		// Horizontal scrolling
		if (direction == GDK_SCROLL_LEFT || direction == GDK_SCROLL_RIGHT || event.state & GDK_SHIFT_MASK) {
			int hScroll = gtk_adjustment_get_step_increment(adjustmenth);
			hScroll *= cLineScroll; // scroll by this many characters
			HorizontalScrollTo(xOffset + hScroll);

			// Text font size zoom
		} else if (event.state & GDK_CONTROL_MASK) {
			if (cLineScroll < 0) {
				KeyCommand(Message::ZoomIn);
			} else {
				KeyCommand(Message::ZoomOut);
			}

			// Regular scrolling
		} else {
			ScrollTo(topLine + cLineScroll);
		}
		return TRUE;
	} catch (...) {
		errorStatus = Status::Failure;
	}
	return FALSE;
}

gint ScintillaGTK::Motion(GtkEventControllerMotion* self, gdouble x, gdouble y, ScintillaGTK* sciThis) {
	try {
		//Platform::DebugPrintf("Motion %x %d\n",sciThis,event->time);
		GdkModifierType eventState = gtk_event_controller_get_current_event_state(GTK_EVENT_CONTROLLER(self));
		guint32 eventTime = gtk_event_controller_get_current_event_time(GTK_EVENT_CONTROLLER(self));

		const Point pt(static_cast<XYPOSITION>(x), static_cast<XYPOSITION>(y));
		const KeyMod modifiers = ModifierFlags(
					      (eventState & GDK_SHIFT_MASK) != 0,
					      (eventState & GDK_CONTROL_MASK) != 0,
					      //(eventState & modifierTranslated(sciThis->rectangularSelectionModifier)) != 0); // TODO: support macOS
					      (eventState & GDK_ALT_MASK) != 0,
					      (eventState & GDK_META_MASK) != 0);

		sciThis->ButtonMoveWithModifiers(pt, eventTime, modifiers);
	} catch (...) {
		sciThis->errorStatus = Status::Failure;
	}
	return FALSE;
}

namespace {

// Map the keypad keys to their equivalent functions
int KeyTranslate(int keyIn) noexcept {
	switch (keyIn) {
#if GTK_CHECK_VERSION(3,0,0)
	case GDK_KEY_ISO_Left_Tab:
		return SCK_TAB;
	case GDK_KEY_KP_Down:
		return SCK_DOWN;
	case GDK_KEY_KP_Up:
		return SCK_UP;
	case GDK_KEY_KP_Left:
		return SCK_LEFT;
	case GDK_KEY_KP_Right:
		return SCK_RIGHT;
	case GDK_KEY_KP_Home:
		return SCK_HOME;
	case GDK_KEY_KP_End:
		return SCK_END;
	case GDK_KEY_KP_Page_Up:
		return SCK_PRIOR;
	case GDK_KEY_KP_Page_Down:
		return SCK_NEXT;
	case GDK_KEY_KP_Delete:
		return SCK_DELETE;
	case GDK_KEY_KP_Insert:
		return SCK_INSERT;
	case GDK_KEY_KP_Enter:
		return SCK_RETURN;

	case GDK_KEY_Down:
		return SCK_DOWN;
	case GDK_KEY_Up:
		return SCK_UP;
	case GDK_KEY_Left:
		return SCK_LEFT;
	case GDK_KEY_Right:
		return SCK_RIGHT;
	case GDK_KEY_Home:
		return SCK_HOME;
	case GDK_KEY_End:
		return SCK_END;
	case GDK_KEY_Page_Up:
		return SCK_PRIOR;
	case GDK_KEY_Page_Down:
		return SCK_NEXT;
	case GDK_KEY_Delete:
		return SCK_DELETE;
	case GDK_KEY_Insert:
		return SCK_INSERT;
	case GDK_KEY_Escape:
		return SCK_ESCAPE;
	case GDK_KEY_BackSpace:
		return SCK_BACK;
	case GDK_KEY_Tab:
		return SCK_TAB;
	case GDK_KEY_Return:
		return SCK_RETURN;
	case GDK_KEY_KP_Add:
		return SCK_ADD;
	case GDK_KEY_KP_Subtract:
		return SCK_SUBTRACT;
	case GDK_KEY_KP_Divide:
		return SCK_DIVIDE;
	case GDK_KEY_Super_L:
		return SCK_WIN;
	case GDK_KEY_Super_R:
		return SCK_RWIN;
	case GDK_KEY_Menu:
		return SCK_MENU;

#else

	case GDK_ISO_Left_Tab:
		return SCK_TAB;
	case GDK_KP_Down:
		return SCK_DOWN;
	case GDK_KP_Up:
		return SCK_UP;
	case GDK_KP_Left:
		return SCK_LEFT;
	case GDK_KP_Right:
		return SCK_RIGHT;
	case GDK_KP_Home:
		return SCK_HOME;
	case GDK_KP_End:
		return SCK_END;
	case GDK_KP_Page_Up:
		return SCK_PRIOR;
	case GDK_KP_Page_Down:
		return SCK_NEXT;
	case GDK_KP_Delete:
		return SCK_DELETE;
	case GDK_KP_Insert:
		return SCK_INSERT;
	case GDK_KP_Enter:
		return SCK_RETURN;

	case GDK_Down:
		return SCK_DOWN;
	case GDK_Up:
		return SCK_UP;
	case GDK_Left:
		return SCK_LEFT;
	case GDK_Right:
		return SCK_RIGHT;
	case GDK_Home:
		return SCK_HOME;
	case GDK_End:
		return SCK_END;
	case GDK_Page_Up:
		return SCK_PRIOR;
	case GDK_Page_Down:
		return SCK_NEXT;
	case GDK_Delete:
		return SCK_DELETE;
	case GDK_Insert:
		return SCK_INSERT;
	case GDK_Escape:
		return SCK_ESCAPE;
	case GDK_BackSpace:
		return SCK_BACK;
	case GDK_Tab:
		return SCK_TAB;
	case GDK_Return:
		return SCK_RETURN;
	case GDK_KP_Add:
		return SCK_ADD;
	case GDK_KP_Subtract:
		return SCK_SUBTRACT;
	case GDK_KP_Divide:
		return SCK_DIVIDE;
	case GDK_Super_L:
		return SCK_WIN;
	case GDK_Super_R:
		return SCK_RWIN;
	case GDK_Menu:
		return SCK_MENU;
#endif
	default:
		return keyIn;
	}
}

}

gboolean ScintillaGTK::KeyPressThis(GtkEventControllerKey* self, guint keyval, guint keycode, GdkModifierType state) {
	try {
		//printf("SC-key: %d %x\n", keyval, state);
		GdkEvent* event = gtk_event_controller_get_current_event(GTK_EVENT_CONTROLLER(self));

		if (keyval == GDK_KEY_Return ||
			keyval == GDK_KEY_KP_Enter ||
			keyval == GDK_KEY_ISO_Enter ||
			keyval == GDK_KEY_Escape)
			gtk_im_context_reset(im_context.get());

		const bool shift = (state & GDK_SHIFT_MASK) != 0;
		bool ctrl = (state & GDK_CONTROL_MASK) != 0;
		const bool alt = (state & GDK_ALT_MASK) != 0;
		const bool super = (state & GDK_SUPER_MASK) != 0;
		guint key = keyval;
		if ((ctrl || alt) && (key < 128))
			key = toupper(key);
		else if (!ctrl && (key >= GDK_KEY_KP_Multiply && key <= GDK_KEY_KP_9))
			key &= 0x7F;
		// Hack for keys over 256 and below command keys but makes Hungarian work.
		// This will have to change for Unicode
		else if (key >= 0xFE00)
			key = KeyTranslate(key);

		bool consumed = false;
		const bool meta = (state & GDK_META_MASK) != 0;;

		const bool added = KeyDownWithModifiers(static_cast<Keys>(key), ModifierFlags(shift, ctrl, alt, meta, super), &consumed) != 0;
		if (!consumed)
			consumed = added;
		//fprintf(stderr, "SK-key: %d %x %x\n",event->keyval, event->state, consumed);
		/*if (keyval == 0xffffff && event->length > 0) { TODO: what this ?
			ClearSelection();
			const Sci::Position lengthInserted = pdoc->InsertString(CurrentPosition(), event->string, strlen(event->string));
			if (lengthInserted > 0) {
				MovePositionTo(CurrentPosition() + lengthInserted);
			}
		}*/
		return consumed;
	} catch (...) {
		errorStatus = Status::Failure;
	}
	return FALSE;
}

gboolean ScintillaGTK::KeyPress(GtkEventControllerKey* self, guint keyval, guint keycode, GdkModifierType state, ScintillaGTK* sciThis) {
	return sciThis->KeyPressThis(self, keyval, keycode, state);
}

gboolean ScintillaGTK::KeyRelease(GtkEventControllerKey* self, guint keyval, guint keycode, GdkModifierType state, ScintillaGTK* sciThis) {
	//Platform::DebugPrintf("SC-keyrel: %d %x %3s\n",event->keyval, event->state, event->string);
	GdkEvent* event = gtk_event_controller_get_current_event(GTK_EVENT_CONTROLLER(self));
	if (gtk_im_context_filter_keypress(sciThis->im_context.get(), event)) {
		return TRUE;
	}
	return FALSE;
}

gboolean ScintillaGTK::DrawPreeditThis(GtkWidget *, cairo_t *cr) {
	try {
		PreEditString pes(im_context.get());
		UniquePangoLayout layout(gtk_widget_create_pango_layout(PWidget(wText), pes.str));
		pango_layout_set_attributes(layout.get(), pes.attrs);

		cairo_move_to(cr, 0, 0);
		pango_cairo_show_layout(cr, layout.get());
	} catch (...) {
		errorStatus = Status::Failure;
	}
	return TRUE;
}

gboolean ScintillaGTK::DrawPreedit(GtkWidget *widget, cairo_t *cr, int width, int height, ScintillaGTK *sciThis) {
	return sciThis->DrawPreeditThis(widget, cr);
}

bool ScintillaGTK::KoreanIME() {
	PreEditString pes(im_context.get());
	if (pes.pscript != G_UNICODE_SCRIPT_COMMON)
		lastNonCommonScript = pes.pscript;
	return lastNonCommonScript == G_UNICODE_SCRIPT_HANGUL;
}

void ScintillaGTK::MoveImeCarets(Sci::Position pos) {
	// Move carets relatively by bytes
	for (size_t r=0; r<sel.Count(); r++) {
		const Sci::Position positionInsert = sel.Range(r).Start().Position();
		sel.Range(r) = SelectionRange(positionInsert + pos);
	}
}

void ScintillaGTK::DrawImeIndicator(int indicator, Sci::Position len) {
	// Emulate the visual style of IME characters with indicators.
	// Draw an indicator on the character before caret by the character bytes of len
	// so it should be called after InsertCharacter().
	// It does not affect caret positions.
	if (indicator < 8 || indicator > INDICATOR_MAX) {
		return;
	}
	pdoc->DecorationSetCurrentIndicator(indicator);
	for (size_t r=0; r<sel.Count(); r++) {
		const Sci::Position positionInsert = sel.Range(r).Start().Position();
		pdoc->DecorationFillRange(positionInsert - len, 1, len);
	}
}

namespace {

std::vector<int> MapImeIndicators(PangoAttrList *attrs, const char *u8Str) {
	// Map input style to scintilla ime indicator.
	// Attrs position points between UTF-8 bytes.
	// Indicator index to be returned is character based though.
	const glong charactersLen = g_utf8_strlen(u8Str, strlen(u8Str));
	std::vector<int> indicator(charactersLen, SC_INDICATOR_UNKNOWN);

	PangoAttrIterator *iterunderline = pango_attr_list_get_iterator(attrs);
	if (iterunderline) {
		do {
			const PangoAttribute  *attrunderline = pango_attr_iterator_get(iterunderline, PANGO_ATTR_UNDERLINE);
			if (attrunderline) {
				const glong start = g_utf8_strlen(u8Str, attrunderline->start_index);
				const glong end = g_utf8_strlen(u8Str, attrunderline->end_index);
				const int ulinevalue = reinterpret_cast<const PangoAttrInt *>(attrunderline)->value;
				const PangoUnderline uline = static_cast<PangoUnderline>(ulinevalue);
				for (glong i=start; i < end; ++i) {
					switch (uline) {
					case PANGO_UNDERLINE_NONE:
						indicator[i] = SC_INDICATOR_UNKNOWN;
						break;
					case PANGO_UNDERLINE_SINGLE: // normal input
						indicator[i] = SC_INDICATOR_INPUT;
						break;
					case PANGO_UNDERLINE_DOUBLE:
					case PANGO_UNDERLINE_LOW:
					case PANGO_UNDERLINE_ERROR:
					default:
						break;
					}
				}
			}
		} while (pango_attr_iterator_next(iterunderline));
		pango_attr_iterator_destroy(iterunderline);
	}

	PangoAttrIterator *itercolor = pango_attr_list_get_iterator(attrs);
	if (itercolor) {
		do {
			const PangoAttribute *backcolor = pango_attr_iterator_get(itercolor, PANGO_ATTR_BACKGROUND);
			if (backcolor) {
				const glong start = g_utf8_strlen(u8Str, backcolor->start_index);
				const glong end = g_utf8_strlen(u8Str, backcolor->end_index);
				for (glong i=start; i < end; ++i) {
					indicator[i] = SC_INDICATOR_TARGET;  // target converted
				}
			}
		} while (pango_attr_iterator_next(itercolor));
		pango_attr_iterator_destroy(itercolor);
	}
	return indicator;
}

}

void ScintillaGTK::SetCandidateWindowPos() {
	// Composition box accompanies candidate box.
	const Point pt = PointMainCaret();
	GdkRectangle imeBox {};
	imeBox.x = static_cast<gint>(pt.x);
	imeBox.y = static_cast<gint>(pt.y + std::max(4, vs.lineHeight/4));
	// prevent overlapping with current line
	imeBox.height = vs.lineHeight;
	gtk_im_context_set_cursor_location(im_context.get(), &imeBox);
}

void ScintillaGTK::CommitThis(char *commitStr) {
	try {
		// printf("Commit '%s'\n", commitStr);
		view.imeCaretBlockOverride = false;

		if (pdoc->TentativeActive()) {
			pdoc->TentativeUndo();
		}

		const char *charSetSource = CharacterSetID();

		glong uniStrLen = 0;
		gunichar *uniStr = g_utf8_to_ucs4_fast(commitStr, static_cast<glong>(strlen(commitStr)), &uniStrLen);
		for (glong i = 0; i < uniStrLen; i++) {
			gchar u8Char[UTF8MaxBytes+2] = {0};
			const gint u8CharLen = g_unichar_to_utf8(uniStr[i], u8Char);
			std::string docChar = u8Char;
			if (!IsUnicodeMode())
				docChar = ConvertText(u8Char, u8CharLen, charSetSource, "UTF-8", true);

			InsertCharacter(docChar, CharacterSource::DirectInput);
		}
		g_free(uniStr);
		ShowCaretAtCurrentPosition();
	} catch (...) {
		errorStatus = Status::Failure;
	}
}

void ScintillaGTK::Commit(GtkIMContext *, char  *str, ScintillaGTK *sciThis) {
	sciThis->CommitThis(str);
}

void ScintillaGTK::PreeditChangedInlineThis() {
	// Copy & paste by johnsonj with a lot of helps of Neil
	// Great thanks for my foreruners, jiniya and BLUEnLIVE
	try {
		if (pdoc->IsReadOnly() || SelectionContainsProtected()) {
			gtk_im_context_reset(im_context.get());
			return;
		}

		view.imeCaretBlockOverride = false; // If backspace.

		bool initialCompose = false;
		if (pdoc->TentativeActive()) {
			pdoc->TentativeUndo();
		} else {
			// No tentative undo means start of this composition so
			// fill in any virtual spaces.
			initialCompose = true;
		}

		PreEditString preeditStr(im_context.get());
		const char *charSetSource = CharacterSetID();

		if (!preeditStr.validUTF8 || (charSetSource == nullptr)) {
			ShowCaretAtCurrentPosition();
			return;
		}

		if (preeditStr.uniStrLen == 0) {
			ShowCaretAtCurrentPosition();
			return;
		}

		if (initialCompose) {
			ClearBeforeTentativeStart();
		}

		SetCandidateWindowPos();
		pdoc->TentativeStart(); // TentativeActive() from now on

		std::vector<int> indicator = MapImeIndicators(preeditStr.attrs, preeditStr.str);

		for (glong i = 0; i < preeditStr.uniStrLen; i++) {
			gchar u8Char[UTF8MaxBytes+2] = {0};
			const gint u8CharLen = g_unichar_to_utf8(preeditStr.uniStr[i], u8Char);
			std::string docChar = u8Char;
			if (!IsUnicodeMode())
				docChar = ConvertText(u8Char, u8CharLen, charSetSource, "UTF-8", true);

			InsertCharacter(docChar, CharacterSource::TentativeInput);

			DrawImeIndicator(indicator[i], docChar.size());
		}

		// Move caret to ime cursor position.
		const int imeEndToImeCaretU32 = preeditStr.cursor_pos - preeditStr.uniStrLen;
		const Sci::Position imeCaretPosDoc = pdoc->GetRelativePosition(CurrentPosition(), imeEndToImeCaretU32);

		MoveImeCarets(- CurrentPosition() + imeCaretPosDoc);

		if (KoreanIME()) {
#if !PLAT_GTK_WIN32
			if (preeditStr.cursor_pos > 0) {
				int oneCharBefore = pdoc->GetRelativePosition(CurrentPosition(), -1);
				MoveImeCarets(- CurrentPosition() + oneCharBefore);
			}
#endif
			view.imeCaretBlockOverride = true;
		}

		EnsureCaretVisible();
		ShowCaretAtCurrentPosition();
	} catch (...) {
		errorStatus = Status::Failure;
	}
}

void ScintillaGTK::PreeditChangedWindowedThis() {
	try {
		PreEditString pes(im_context.get());
		if (strlen(pes.str) > 0) {
			SetCandidateWindowPos();

			UniquePangoLayout layout(gtk_widget_create_pango_layout(PWidget(wText), pes.str));
			pango_layout_set_attributes(layout.get(), pes.attrs);

			gint w, h;
			pango_layout_get_pixel_size(layout.get(), &w, &h);

			graphene_rect_t rect;
			gtk_widget_compute_bounds(PWidget(wText), PWidget(wMain), &rect);
			gint x = rect.origin.x;
			gint y = rect.origin.y;

			Point pt = PointMainCaret();
			if (pt.x < 0)
				pt.x = 0;
			if (pt.y < 0)
				pt.y = 0;

			// TODO: use GtkFixed layout ?

			GdkRectangle pointing = { 0 };
			pointing.x = x + static_cast<gint>(pt.x);
			pointing.y = y + static_cast<gint>(pt.y);

			//gtk_popover_set_pointing_to(GTK_POPOVER(PWidget(wPreedit)), &pointing);
			//gtk_widget_set_size_request(PWidget(wPreedit), w, h);
			//gtk_popover_popup(GTK_POPOVER(PWidget(wPreedit)));

			/*gtk_window_move(GTK_WINDOW(PWidget(wPreedit)), x + static_cast<gint>(pt.x), y + static_cast<gint>(pt.y));
			gtk_window_resize(GTK_WINDOW(PWidget(wPreedit)), w, h);*/
			//gtk_widget_show(PWidget(wPreedit));
			//gtk_widget_queue_draw(PWidget(wPreeditDraw));
		} else {
			//gtk_popover_popdown(GTK_POPOVER(PWidget(wPreedit)));
			//gtk_widget_hide(PWidget(wPreedit));
		}
	} catch (...) {
		errorStatus = Status::Failure;
	}
}

void ScintillaGTK::PreeditChanged(GtkIMContext *, ScintillaGTK *sciThis) {
	sciThis->PreeditChangedInlineThis();
	// NOTE: cannot implement windowed pre-edit, GTK4 remove the popup GtkWindow
	//if ((sciThis->imeInteraction == IMEInteraction::Inline) || (sciThis->KoreanIME())) {
	//	sciThis->PreeditChangedInlineThis();
	//} else {
	//	sciThis->PreeditChangedWindowedThis();
	//}
}

bool ScintillaGTK::RetrieveSurroundingThis(GtkIMContext *context) {
	try {
		const Sci::Position pos = CurrentPosition();
		const int line = pdoc->LineFromPosition(pos);
		const Sci::Position startByte = pdoc->LineStart(line);
		const Sci::Position endByte = pdoc->LineEnd(line);

		std::string utf8Text;
		gint cursorIndex; // index of the cursor inside utf8Text, in bytes
		const char *charSetBuffer;

		if (IsUnicodeMode() || ! *(charSetBuffer = CharacterSetID())) {
			utf8Text = RangeText(startByte, endByte);
			cursorIndex = pos - startByte;
		} else {
			// Need to convert
			std::string tmpbuf = RangeText(startByte, pos);
			utf8Text = ConvertText(&tmpbuf[0], tmpbuf.length(), "UTF-8", charSetBuffer, false);
			cursorIndex = utf8Text.length();
			if (endByte > pos) {
				tmpbuf = RangeText(pos, endByte);
				utf8Text += ConvertText(&tmpbuf[0], tmpbuf.length(), "UTF-8", charSetBuffer, false);
			}
		}

		gtk_im_context_set_surrounding(context, &utf8Text[0], utf8Text.length(), cursorIndex);

		return true;
	} catch (...) {
		errorStatus = Status::Failure;
	}
	return false;
}

gboolean ScintillaGTK::RetrieveSurrounding(GtkIMContext *context, ScintillaGTK *sciThis) {
	return sciThis->RetrieveSurroundingThis(context);
}

bool ScintillaGTK::DeleteSurroundingThis(GtkIMContext *, gint characterOffset, gint characterCount) {
	try {
		const Sci::Position startByte = pdoc->GetRelativePosition(CurrentPosition(), characterOffset);
		if (startByte == INVALID_POSITION)
			return false;

		const Sci::Position endByte = pdoc->GetRelativePosition(startByte, characterCount);
		if (endByte == INVALID_POSITION)
			return false;

		return pdoc->DeleteChars(startByte, endByte - startByte);
	} catch (...) {
		errorStatus = Status::Failure;
	}
	return false;
}

gboolean ScintillaGTK::DeleteSurrounding(GtkIMContext *context, gint characterOffset, gint characterCount, ScintillaGTK *sciThis) {
	return sciThis->DeleteSurroundingThis(context, characterOffset, characterCount);
}

static GObjectClass *scintilla_class_parent_class;

void ScintillaGTK::Dispose(GObject *object) {
	try {
		ScintillaObject *scio = SCINTILLA(object);
		ScintillaGTK *sciThis = static_cast<ScintillaGTK *>(scio->pscin);

		gtk_widget_unparent(PWidget(sciThis->wText));

		if (PWidget(sciThis->scrollbarv)) {
			gtk_widget_unparent(PWidget(sciThis->scrollbarv));
			sciThis->scrollbarv = nullptr;
		}

		if (PWidget(sciThis->scrollbarh)) {
			gtk_widget_unparent(PWidget(sciThis->scrollbarh));
			sciThis->scrollbarh = nullptr;
		}

		if (sciThis->im_context)
			gtk_im_context_reset(sciThis->im_context.get());

		scintilla_class_parent_class->dispose(object);
	} catch (...) {
		// Its dying so nowhere to save the status
	}
}

void ScintillaGTK::Destroy(GObject *object) {
	try {
		ScintillaObject *scio = SCINTILLA(object);

		// This avoids a double destruction
		if (!scio->pscin)
			return;
		ScintillaGTK *sciThis = static_cast<ScintillaGTK *>(scio->pscin);
		//Platform::DebugPrintf("Destroying %x %x\n", sciThis, object);
		sciThis->Finalise();

		delete sciThis;
		scio->pscin = nullptr;
		scintilla_class_parent_class->finalize(object);
	} catch (...) {
		// Its dead so nowhere to save the status
	}
}

void ScintillaGTK::CheckForFontOptionChange() {
	const FontOptions fontOptionsNow(PWidget(wText));
	if (!(fontOptionsNow == fontOptionsPrevious)) {
		// Clear position caches
		InvalidateStyleData();
	}
	fontOptionsPrevious = fontOptionsNow;
}

gboolean ScintillaGTK::DrawTextThis(cairo_t *cr) {
	try {
		CheckForFontOptionChange();

		paintState = PaintState::painting;
		repaintFullWindow = false;

		rcPaint = GetClientRectangle();

		cairo_rectangle_list_t *oldRgnUpdate = rgnUpdate;
		rgnUpdate = cairo_copy_clip_rectangle_list(cr);
		if (rgnUpdate && rgnUpdate->status != CAIRO_STATUS_SUCCESS) {
			// If not successful then ignore
			fprintf(stderr, "DrawTextThis failed to copy update region %d [%d]\n", rgnUpdate->status, rgnUpdate->num_rectangles);
			cairo_rectangle_list_destroy(rgnUpdate);
			rgnUpdate = nullptr;
		}

		double x1, y1, x2, y2;
		cairo_clip_extents(cr, &x1, &y1, &x2, &y2);
		rcPaint.left = x1;
		rcPaint.top = y1;
		rcPaint.right = x2;
		rcPaint.bottom = y2;
		PRectangle rcClient = GetClientRectangle();
		paintingAllText = rcPaint.Contains(rcClient);
		std::unique_ptr<Surface> surfaceWindow(Surface::Allocate(Technology::Default));
		surfaceWindow->Init(cr, PWidget(wText));
		Paint(surfaceWindow.get(), rcPaint);
		surfaceWindow->Release();
		if ((paintState == PaintState::abandoned) || repaintFullWindow) {
			// Painting area was insufficient to cover new styling or brace highlight positions
			FullPaint();
		}
		paintState = PaintState::notPainting;
		repaintFullWindow = false;

		if (rgnUpdate) {
			cairo_rectangle_list_destroy(rgnUpdate);
		}
		rgnUpdate = oldRgnUpdate;
		paintState = PaintState::notPainting;
	} catch (...) {
		errorStatus = Status::Failure;
	}

	return FALSE;
}

gboolean ScintillaGTK::DrawTextCb(GtkWidget *, cairo_t *cr, int width, int height, ScintillaGTK *sciThis) {
	return sciThis->DrawTextThis(cr);
}

void ScintillaGTK::DrawThis(GtkSnapshot* snapshot) {
	try {
#ifdef GTK_STYLE_CLASS_SCROLLBARS_JUNCTION /* GTK >= 3.4 */
		// if both scrollbars are visible, paint the little square on the bottom right corner
		if (verticalScrollBarVisible && horizontalScrollBarVisible && !Wrapping()) {
			GtkStyleContext *styleContext = gtk_widget_get_style_context(PWidget(wMain));
			PRectangle rc = GetClientRectangle();

			gtk_style_context_save(styleContext);
			gtk_style_context_add_class(styleContext, GTK_STYLE_CLASS_SCROLLBARS_JUNCTION);

			gtk_render_background(styleContext, cr, rc.right, rc.bottom,
					      verticalScrollBarWidth, horizontalScrollBarHeight);
			gtk_render_frame(styleContext, cr, rc.right, rc.bottom,
					 verticalScrollBarWidth, horizontalScrollBarHeight);

			gtk_style_context_restore(styleContext);
		}
#endif
		needDraw = true; // lazy draw
		parentClass->snapshot(PWidget(wMain), snapshot);

		//gtk_container_propagate_draw(
		//	GTK_CONTAINER(PWidget(wMain)), PWidget(scrollbarh), cr);
		//gtk_container_propagate_draw(
		//	GTK_CONTAINER(PWidget(wMain)), PWidget(scrollbarv), cr);

	} catch (...) {
		errorStatus = Status::Failure;
	}
}

void ScintillaGTK::DrawMain(GtkWidget *widget, GtkSnapshot* snapshot) {
	ScintillaGTK *sciThis = FromWidget(widget);
	sciThis->DrawThis(snapshot);
}

void ScintillaGTK::ScrollSignal(GtkAdjustment *adj, ScintillaGTK *sciThis) {
	try {
		sciThis->ScrollTo(static_cast<int>(gtk_adjustment_get_value(adj)), false);
	} catch (...) {
		sciThis->errorStatus = Status::Failure;
	}
}

void ScintillaGTK::ScrollHSignal(GtkAdjustment *adj, ScintillaGTK *sciThis) {
	try {
		sciThis->HorizontalScrollTo(static_cast<int>(gtk_adjustment_get_value(adj)));
	} catch (...) {
		sciThis->errorStatus = Status::Failure;
	}
}

#if 0 // TODO: clipboard and drag support

void ScintillaGTK::SelectionReceived(GtkWidget *widget,
				     GtkSelectionData *selection_data, guint) {
	ScintillaGTK *sciThis = FromWidget(widget);
	//Platform::DebugPrintf("Selection received\n");
	sciThis->ReceivedSelection(selection_data);
}

void ScintillaGTK::SelectionGet(GtkWidget *widget,
				GtkSelectionData *selection_data, guint info, guint) {
	ScintillaGTK *sciThis = FromWidget(widget);
	try {
		//Platform::DebugPrintf("Selection get\n");
		if (SelectionOfGSD(selection_data) == GDK_SELECTION_PRIMARY) {
			if (sciThis->primary.Empty()) {
				sciThis->CopySelectionRange(&sciThis->primary);
			}
			sciThis->GetSelection(selection_data, info, &sciThis->primary);
		}
	} catch (...) {
		sciThis->errorStatus = Status::Failure;
	}
}

gint ScintillaGTK::SelectionClear(GtkWidget *widget, GdkEventSelection *selection_event) {
	ScintillaGTK *sciThis = FromWidget(widget);
	//Platform::DebugPrintf("Selection clear\n");
	sciThis->UnclaimSelection(selection_event);
	if (GTK_WIDGET_CLASS(sciThis->parentClass)->selection_clear_event) {
		return GTK_WIDGET_CLASS(sciThis->parentClass)->selection_clear_event(widget, selection_event);
	}
	return TRUE;
}

gboolean ScintillaGTK::DragMotionThis(GdkDragContext *context,
				      gint x, gint y, guint dragtime) {
	try {
		const Point npt = Point::FromInts(x, y);
		SetDragPosition(SPositionFromLocation(npt, false, false, UserVirtualSpace()));
		GdkDragAction preferredAction = gdk_drag_context_get_suggested_action(context);
		const GdkDragAction actions = gdk_drag_context_get_actions(context);
		const SelectionPosition pos = SPositionFromLocation(npt);
		if ((inDragDrop == DragDrop::dragging) && (PositionInSelection(pos.Position()))) {
			// Avoid dragging selection onto itself as that produces a move
			// with no real effect but which creates undo actions.
			preferredAction = static_cast<GdkDragAction>(0);
		} else if (actions == actionCopyOrMove) {
			preferredAction = GDK_ACTION_MOVE;
		}
		gdk_drag_status(context, preferredAction, dragtime);
	} catch (...) {
		errorStatus = Status::Failure;
	}
	return FALSE;
}

gboolean ScintillaGTK::DragMotion(GtkWidget *widget, GdkDragContext *context,
				  gint x, gint y, guint dragtime) {
	ScintillaGTK *sciThis = FromWidget(widget);
	return sciThis->DragMotionThis(context, x, y, dragtime);
}

void ScintillaGTK::DragLeave(GtkWidget *widget, GdkDragContext * /*context*/, guint) {
	ScintillaGTK *sciThis = FromWidget(widget);
	try {
		sciThis->SetDragPosition(SelectionPosition(Sci::invalidPosition));
		//Platform::DebugPrintf("DragLeave %x\n", sciThis);
	} catch (...) {
		sciThis->errorStatus = Status::Failure;
	}
}

void ScintillaGTK::DragEnd(GtkWidget *widget, GdkDragContext * /*context*/) {
	ScintillaGTK *sciThis = FromWidget(widget);
	try {
		// If drag did not result in drop here or elsewhere
		if (!sciThis->dragWasDropped)
			sciThis->SetEmptySelection(sciThis->posDrag);
		sciThis->SetDragPosition(SelectionPosition(Sci::invalidPosition));
		//Platform::DebugPrintf("DragEnd %x %d\n", sciThis, sciThis->dragWasDropped);
		sciThis->inDragDrop = DragDrop::none;
	} catch (...) {
		sciThis->errorStatus = Status::Failure;
	}
}

gboolean ScintillaGTK::Drop(GtkWidget *widget, GdkDragContext * /*context*/,
			    gint, gint, guint) {
	ScintillaGTK *sciThis = FromWidget(widget);
	try {
		//Platform::DebugPrintf("Drop %x\n", sciThis);
		sciThis->SetDragPosition(SelectionPosition(Sci::invalidPosition));
	} catch (...) {
		sciThis->errorStatus = Status::Failure;
	}
	return FALSE;
}

void ScintillaGTK::DragDataReceived(GtkWidget *widget, GdkDragContext * /*context*/,
				    gint, gint, GtkSelectionData *selection_data, guint /*info*/, guint) {
	ScintillaGTK *sciThis = FromWidget(widget);
	try {
		sciThis->ReceivedDrop(selection_data);
		sciThis->SetDragPosition(SelectionPosition(Sci::invalidPosition));
	} catch (...) {
		sciThis->errorStatus = Status::Failure;
	}
}

void ScintillaGTK::DragDataGet(GtkWidget *widget, GdkDragContext *context,
			       GtkSelectionData *selection_data, guint info, guint) {
	ScintillaGTK *sciThis = FromWidget(widget);
	try {
		sciThis->dragWasDropped = true;
		if (!sciThis->sel.Empty()) {
			sciThis->GetSelection(selection_data, info, &sciThis->drag);
		}
		const GdkDragAction action = gdk_drag_context_get_selected_action(context);
		if (action == GDK_ACTION_MOVE) {
			for (size_t r=0; r<sciThis->sel.Count(); r++) {
				if (sciThis->posDrop >= sciThis->sel.Range(r).Start()) {
					if (sciThis->posDrop > sciThis->sel.Range(r).End()) {
						sciThis->posDrop.Add(-sciThis->sel.Range(r).Length());
					} else {
						sciThis->posDrop.Add(-SelectionRange(sciThis->posDrop, sciThis->sel.Range(r).Start()).Length());
					}
				}
			}
			sciThis->ClearSelection();
		}
		sciThis->SetDragPosition(SelectionPosition(Sci::invalidPosition));
	} catch (...) {
		sciThis->errorStatus = Status::Failure;
	}
}

#endif

int ScintillaGTK::TimeOut(gpointer ptt) {
	TimeThunk *tt = static_cast<TimeThunk *>(ptt);
	tt->scintilla->TickFor(tt->reason);
	return 1;
}

gboolean ScintillaGTK::IdleCallback(gpointer pSci) {
	ScintillaGTK *sciThis = static_cast<ScintillaGTK *>(pSci);
	// Idler will be automatically stopped, if there is nothing
	// to do while idle.
	const bool ret = sciThis->Idle();
	if (!ret) {
		// FIXME: This will remove the idler from GTK, we don't want to
		// remove it as it is removed automatically when this function
		// returns false (although, it should be harmless).
		sciThis->SetIdle(false);
	}
	return ret;
}

gboolean ScintillaGTK::StyleIdle(gpointer pSci) {
	ScintillaGTK *sciThis = static_cast<ScintillaGTK *>(pSci);
	sciThis->IdleWork();
	// Idler will be automatically stopped
	return FALSE;
}

void ScintillaGTK::IdleWork() {
	Editor::IdleWork();
	styleIdleID = 0;
}

void ScintillaGTK::QueueIdleWork(WorkItems items, Sci::Position upTo) {
	Editor::QueueIdleWork(items, upTo);
	if (!styleIdleID) {
		// Only allow one style needed to be queued
		styleIdleID = g_idle_add_full(G_PRIORITY_HIGH_IDLE, StyleIdle, this, nullptr);
	}
}

void ScintillaGTK::SetDocPointer(Document *document) {
	Editor::SetDocPointer(document);
}

void ScintillaGTK::PopUpCB(GObject* obj, GVariant* param, ScintillaGTK *sciThis) {
	guint const action = GPOINTER_TO_UINT(g_object_get_data(obj, "CmdNum"));
	if (action) {
		sciThis->Command(action);
	}
}

gboolean ScintillaGTK::PressCT(GObject* obj, guint nPress, gdouble x, gdouble y, ScintillaGTK *sciThis) {
	try {
		const Point pt = Point(static_cast<XYPOSITION>(std::floor(x)), static_cast<XYPOSITION>(std::floor(y)));
		if (nPress == 1) {
			sciThis->ct.MouseClick(pt);
			sciThis->CallTipClick();
		}
	} catch (...) {
	}
	return TRUE;
}

gboolean ScintillaGTK::DrawCT(GtkWidget *widget, cairo_t *cr, CallTip *ctip) {
	try {
		std::unique_ptr<Surface> surfaceWindow(Surface::Allocate(Technology::Default));
		surfaceWindow->Init(cr, widget);
		surfaceWindow->SetMode(SurfaceMode(ctip->codePage, false));
		ctip->PaintCT(surfaceWindow.get());
		surfaceWindow->Release();
	} catch (...) {
		// No pointer back to Scintilla to save status
	}
	return TRUE;
}

sptr_t ScintillaGTK::DirectFunction(
	sptr_t ptr, unsigned int iMessage, uptr_t wParam, sptr_t lParam) {
	ScintillaGTK *sci = reinterpret_cast<ScintillaGTK *>(ptr);
	return sci->WndProc(static_cast<Message>(iMessage), wParam, lParam);
}

sptr_t ScintillaGTK::DirectStatusFunction(
	sptr_t ptr, unsigned int iMessage, uptr_t wParam, sptr_t lParam, int *pStatus) {
	ScintillaGTK *sci = reinterpret_cast<ScintillaGTK *>(ptr);
	const sptr_t returnValue = sci->WndProc(static_cast<Message>(iMessage), wParam, lParam);
	*pStatus = static_cast<int>(sci->errorStatus);
	return returnValue;
}

/* legacy name for scintilla_object_send_message */
sptr_t scintilla_send_message(ScintillaObject *sci, unsigned int iMessage, uptr_t wParam, sptr_t lParam) {
	ScintillaGTK *psci = static_cast<ScintillaGTK *>(sci->pscin);
	return psci->WndProc(static_cast<Message>(iMessage), wParam, lParam);
}

gintptr scintilla_object_send_message(ScintillaObject *sci, unsigned int iMessage, uptr_t wParam, sptr_t lParam) {
	return scintilla_send_message(sci, iMessage, wParam, lParam);
}

static void scintilla_class_init(ScintillaClass *klass);
static void scintilla_init(ScintillaObject *sci);

/* legacy name for scintilla_object_get_type */
GType scintilla_get_type() {
	static GType scintilla_type = 0;
	try {

		if (!scintilla_type) {
			scintilla_type = g_type_from_name("ScintillaObject");
			if (!scintilla_type) {
				static GTypeInfo scintilla_info = {
					(guint16) sizeof(ScintillaObjectClass),
					nullptr, //(GBaseInitFunc)
					nullptr, //(GBaseFinalizeFunc)
					(GClassInitFunc) scintilla_class_init,
					nullptr, //(GClassFinalizeFunc)
					nullptr, //gconstpointer data
					(guint16) sizeof(ScintillaObject),
					0, //n_preallocs
					(GInstanceInitFunc) scintilla_init,
					nullptr //(GTypeValueTable*)
				};
				scintilla_type = g_type_register_static(
							 GTK_TYPE_WIDGET, "ScintillaObject", &scintilla_info, (GTypeFlags) 0);
			}
		}

	} catch (...) {
	}
	return scintilla_type;
}

GType scintilla_object_get_type() {
	return scintilla_get_type();
}

void ScintillaGTK::ClassInit(OBJECT_CLASS *object_class, GtkWidgetClass *widget_class) {
	Platform_Initialise();
	// Define default signal handlers for the class:  Could move more
	// of the signal handlers here (those that currently attached to wDraw
	// in Init() may require coordinate translation?)

	object_class->dispose = Dispose;
	object_class->finalize = Destroy;

	//widget_class->get_preferred_width = GetPreferredWidth; TODO: implement measure
	//widget_class->get_preferred_height = GetPreferredHeight;
	widget_class->size_allocate = SizeAllocate;
	widget_class->snapshot = DrawMain;

	// TODO: darg support
	//widget_class->drag_data_received = DragDataReceived;
	//widget_class->drag_motion = DragMotion;
	//widget_class->drag_leave = DragLeave;
	//widget_class->drag_end = DragEnd;
	//widget_class->drag_drop = Drop;
	//widget_class->drag_data_get = DragDataGet;

	widget_class->realize = Realize;
	widget_class->unrealize = UnRealize;
	widget_class->map = Map;
	widget_class->unmap = UnMap;
}

static void scintilla_class_init(ScintillaClass *klass) {
	try {
		OBJECT_CLASS *object_class = reinterpret_cast<OBJECT_CLASS *>(klass);
		GtkWidgetClass *widget_class = reinterpret_cast<GtkWidgetClass *>(klass);

		const GSignalFlags sigflags = static_cast<GSignalFlags>(G_SIGNAL_ACTION | G_SIGNAL_RUN_LAST);
		scintilla_signals[COMMAND_SIGNAL] = g_signal_new(
				"command",
				G_TYPE_FROM_CLASS(object_class),
				sigflags,
				G_STRUCT_OFFSET(ScintillaClass, command),
				nullptr, //(GSignalAccumulator)
				nullptr, //(gpointer)
				scintilla_marshal_VOID__INT_OBJECT,
				G_TYPE_NONE,
				2, G_TYPE_INT, GTK_TYPE_WIDGET);

		scintilla_signals[NOTIFY_SIGNAL] = g_signal_new(
				SCINTILLA_NOTIFY,
				G_TYPE_FROM_CLASS(object_class),
				sigflags,
				G_STRUCT_OFFSET(ScintillaClass, notify),
				nullptr, //(GSignalAccumulator)
				nullptr, //(gpointer)
				scintilla_marshal_VOID__INT_BOXED,
				G_TYPE_NONE,
				2, G_TYPE_INT, SCINTILLA_TYPE_NOTIFICATION);

		klass->command = nullptr;
		klass->notify = nullptr;
		scintilla_class_parent_class = G_OBJECT_CLASS(g_type_class_peek_parent(klass));
		ScintillaGTK::ClassInit(object_class, widget_class);
	} catch (...) {
	}
}

static void scintilla_init(ScintillaObject *sci) {
	try {
		gtk_widget_set_can_focus(GTK_WIDGET(sci), TRUE);
		sci->pscin = new ScintillaGTK(sci);
	} catch (...) {
	}
}

/* legacy name for scintilla_object_new */
GtkWidget *scintilla_new() {
	GtkWidget *widget = GTK_WIDGET(g_object_new(scintilla_get_type(), nullptr));
	gtk_widget_set_direction(widget, GTK_TEXT_DIR_LTR);

	return widget;
}

GtkWidget *scintilla_object_new() {
	return scintilla_new();
}

void scintilla_set_id(ScintillaObject *sci, uptr_t id) {
	ScintillaGTK *psci = static_cast<ScintillaGTK *>(sci->pscin);
	psci->ctrlID = static_cast<int>(id);
}

void scintilla_release_resources(void) {
	try {
		Platform_Finalise();
	} catch (...) {
	}
}

/* Define a dummy boxed type because g-ir-scanner is unable to
 * recognize gpointer-derived types. Note that SCNotificaiton
 * is always allocated on stack so copying is not appropriate. */
static void *copy_(void *src) { return src; }
static void free_(void *) { }

GType scnotification_get_type(void) {
	static gsize type_id = 0;
	if (g_once_init_enter(&type_id)) {
		const gsize id = (gsize) g_boxed_type_register_static(
					 g_intern_static_string("SCNotification"),
					 (GBoxedCopyFunc) copy_,
					 (GBoxedFreeFunc) free_);
		g_once_init_leave(&type_id, id);
	}
	return (GType) type_id;
}
