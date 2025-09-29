#include <gtk/gtk.h>

#include "Scintilla.h"
#include "ScintillaWidget.h"

#ifdef _WIN32
#define EXPORT __declspec(dllexport)
#else
#define EXPORT __attribute__((visibility("default")))
#endif

#define GTK_TYPE_SCINTILLA (gtk_scintilla_get_type())
#define GTK_SCINTILLA(obj)				(G_TYPE_CHECK_INSTANCE_CAST((obj), GTK_TYPE_SCINTILLA, GtkScintilla))
#define GTK_SCINTILLA_CLASS(klass)		(G_TYPE_CHECK_CLASS_CAST((klass), GTK_TYPE_SCINTILLA, GtkScintillaClass))
#define GTK_IS_SCINTILLA(obj)			(G_TYPE_CHECK_INSTANCE_TYPE((obj), GTK_TYPE_SCINTILLA))
#define GTK_IS_SCINTILLA_CLASS(klass)	(G_TYPE_CHECK_CLASS_TYPE((klass), GTK_TYPE_SCINTILLA))
#define GTK_SCINTILLA_GET_CLASS(obj)	(G_TYPE_INSTANCE_GET_CLASS((obj), GTK_TYPE_SCINTILLA, GtkScintillaClass))

typedef struct _GtkScintilla GtkScintilla;
typedef struct _GtkScintillaClass GtkScintillaClass;

struct _GtkScintilla {
	ScintillaObject parent_instance;
};

struct _GtkScintillaClass
{
	ScintillaObjectClass parent_class;

	// signals
	void(*text_changed)(GtkScintilla* self);
};

enum
{
	PROP_0,
	PROP_DARK,
	PROP_STYLE,
	PROP_LANGUAGE,
	PROP_EDITABLE,
	PROP_LINES,
	PROP_LINE_NUMBER,
	PROP_FOLD,
	PROP_AUTO_INDENT,
	PROP_INDENT_GUIDES,
	PROP_TAB_WIDTH,
	PROP_WRAP_MODE,
	PROP_COUNT
};

static GParamSpec* props[PROP_COUNT];

enum
{
	SIGNAL_TEXT_CHANGED,
	SIGNAL_COUNT
};

static guint signals[SIGNAL_COUNT];

typedef struct _ScintillaStyle ScintillaStyle;
typedef struct _ScintillaFont ScintillaFont;
typedef struct _ScintillaLanguage ScintillaLanguage;

typedef struct _GtkScintillaPrivate
{
	ScintillaObject* sci;
	const ScintillaStyle* style;
	const ScintillaLanguage* lang;
	gintptr searchPos;
	guint lines;
	GtkWrapMode wrapMode;
	gboolean dark : 1;
	gboolean fold : 1;
	gboolean lineNumber : 1;
	gboolean autoIndent : 1;
	gboolean editable : 1;

} GtkScintillaPrivate;

EXPORT GType gtk_scintilla_get_type(void);
G_DEFINE_TYPE_WITH_PRIVATE(GtkScintilla, gtk_scintilla, SCINTILLA_TYPE_OBJECT)

#define PRIVATE(self) gtk_scintilla_get_instance_private(self)

#define GSCI_NUMBER_MARGIN_INDEX 0
#define GSCI_SYMBOL_MARGIN_INDEX 1
#define GSCI_SYMBOL_MARGIN_WIDTH 6
#define GSCI_FOLD_MARGIN_INDEX 2
#define GSCI_FOLD_MARGIN_WIDTH 12
#define GSCI_CARET_WIDTH 2
#define GSCI_LINE_FRAME_WIDTH 2

#define SSM(sci, msg, wp, lp) scintilla_send_message(SCINTILLA(sci), msg, (uptr_t)wp, (uptr_t)lp)
#define RGB(r, g, b) ((guint32(b) << 16) | (guint32(g) << 8) | guint32(r))
#define RGBA(r, g, b, a) ((guint32(a) << 24) | (guint32(b) << 16) | (guint32(g) << 8) | guint32(r))
#define HEX_RGB(hex) (hex >> 16) | (hex & 0x00FF00) | ((hex & 0x0000FF) << 16)
#define HEX_RGBA(hex) (hex >> 24) | ((hex & 0x00FF0000) >> 8) | ((hex & 0x0000FF00) << 8) | ((hex & 0x000000FF) << 24)

struct _ScintillaFont
{
	const char* name;
	guint64 size : 8;
	guint64 bold : 1;
	guint64 italic : 1;
	guint64 underline : 1;
};

struct _ScintillaStyle
{
	const char* name;
	gboolean(*fgColor)(int index, gboolean dark, guint32* color);
	gboolean(*bgColor)(int index, gboolean dark, guint32* color);
	gboolean(*elemColor)(int index, gboolean dark, guint32* color);
	gboolean(*fonts)(int index, gboolean dark, ScintillaFont* font);
	void(*setProps)(ScintillaObject* sci, gboolean dark);
};

static gboolean vscodeFgColor(int index, gboolean dark, guint32* color);
static gboolean vscodeBgColor(int index, gboolean dark, guint32* color);
static gboolean vscodeElemColor(int index, gboolean dark, guint32* color);
static gboolean vscodeFonts(int index, gboolean dark, ScintillaFont* font);
static void vscodeSetProps(ScintillaObject* sci, gboolean dark);

static const ScintillaStyle GSCI_STYLES[] =
{
	{ "default", NULL, NULL, NULL, NULL },
	{ "vscode", vscodeFgColor, vscodeBgColor, vscodeElemColor, vscodeFonts, vscodeSetProps },
	{ NULL }
};

struct _ScintillaLanguage
{
	const char* language;
	const char* lexer;
	const char** keywords;
	gboolean(*fgColor)(int index, gboolean dark, guint32* defColor);
	gboolean(*bgColor)(int index, gboolean dark, guint32* defColor);
	gboolean(*fonts)(int index, gboolean dark, ScintillaFont* defFont);
	void(*setProps)(ScintillaObject* sci);
};

static const char* jsonKeywords[8] =
{
	"false true null",
	"@id @context @type @value @language @container @list @set @reverse @index @base @vocab @graph",
	NULL
};

static gboolean jsonFgColor(int index, gboolean dark, guint32* defColor);
static gboolean jsonBgColor(int index, gboolean dark, guint32* defColor);
static gboolean jsonFonts(int index, gboolean dark, ScintillaFont* defFont);
static void jsonSetProps(ScintillaObject* sci);

static const ScintillaLanguage GSCI_LANGUAGES[] =
{
	{ "txt", NULL, NULL, NULL, NULL, NULL, NULL },
	{ "json", "json", jsonKeywords, jsonFgColor, jsonBgColor, jsonFonts, jsonSetProps },
	{ NULL }
};

static void updateStyle(GtkScintillaPrivate* priv);
static gboolean updateLanguage(GtkScintillaPrivate* priv);
static void updateFold(GtkScintillaPrivate* priv);
static void updateLineNumber(GtkScintilla* sci);
static void onSciNotify(GtkScintilla* self, gint param, SCNotification* notif, GtkScintillaPrivate* priv);

static void gtk_scintilla_class_install_properties(GtkScintillaClass* klass);
static void gtk_scintilla_class_install_signals(GtkScintillaClass* klass);

static void gtk_scintilla_get_property(GObject* obj, guint prop, GValue* val, GParamSpec* ps);
static void gtk_scintilla_set_property(GObject* obj, guint prop, const GValue* val, GParamSpec* ps);

static void gtk_scintilla_class_init(GtkScintillaClass* klass)
{
	GObjectClass* cls = G_OBJECT_CLASS(klass);

	cls->get_property = gtk_scintilla_get_property;
	cls->set_property = gtk_scintilla_set_property;

	gtk_scintilla_class_install_properties(klass);
	gtk_scintilla_class_install_signals(klass);
}

static void gtk_scintilla_init(GtkScintilla* sci)
{
	GtkScintillaPrivate* priv = PRIVATE(sci);
	priv->sci = SCINTILLA(sci);
	priv->style = &GSCI_STYLES[0];
	priv->lang = NULL;
	priv->wrapMode = GTK_WRAP_NONE;
	priv->lines = 0;
	priv->searchPos = -1;
	priv->dark = false;
	priv->fold = false;
	priv->lineNumber = false;
	priv->autoIndent = false;
	priv->editable = true;

	SSM(sci, SCI_SETBUFFEREDDRAW, 0, 0); // disable buffered draw
	SSM(sci, SCI_SETEOLMODE, SC_EOL_LF, 0); // set EOL LF(\n)

	g_signal_connect(SCINTILLA(sci), "sci-notify", G_CALLBACK(onSciNotify), priv);
}

EXPORT GtkWidget* gtk_scintilla_new(void)
{
	return g_object_new(GTK_TYPE_SCINTILLA, NULL);
}

EXPORT gboolean gtk_scintilla_get_dark(GtkScintilla* self)
{
	GtkScintillaPrivate* priv = PRIVATE(self);
	return priv->dark;
}

EXPORT void gtk_scintilla_set_dark(GtkScintilla* self, gboolean v)
{
	GtkScintillaPrivate* priv = PRIVATE(self);
	priv->dark = v;
	updateStyle(priv);
	g_object_notify_by_pspec(G_OBJECT(self), props[PROP_DARK]);
}

EXPORT const char* gtk_scintilla_get_style(GtkScintilla* sci)
{
	GtkScintillaPrivate* priv = PRIVATE(sci);
	return priv->style->name;
}

EXPORT void gtk_scintilla_set_style(GtkScintilla* self, const char* styleName)
{
	GtkScintillaPrivate* priv = PRIVATE(self);
	priv->style = &GSCI_STYLES[0]; // default
	for (const ScintillaStyle* style = &GSCI_STYLES[1]; style->name != NULL; style++)
	{
		if (strcmp(style->name, styleName) == 0)
		{
			priv->style = style;
			break;
		}
	}
	updateStyle(priv);
	g_object_notify_by_pspec(G_OBJECT(self), props[PROP_STYLE]);
}

EXPORT const char* gtk_scintilla_get_language(GtkScintilla* sci)
{
	GtkScintillaPrivate* priv = PRIVATE(sci);
	if (priv->lang)
		return priv->lang->language;
	return "";
}

EXPORT void gtk_scintilla_set_language(GtkScintilla* self, const char* language)
{
	GtkScintillaPrivate* priv = PRIVATE(self);
	priv->lang = &GSCI_LANGUAGES[0]; // default
	for (const ScintillaLanguage* lang = &GSCI_LANGUAGES[1]; lang->language != NULL; lang++)
	{
		if (strcmp(lang->language, language) == 0)
		{
			priv->lang = lang;
			break;
		}
	}
	updateLanguage(priv);
	g_object_notify_by_pspec(G_OBJECT(self), props[PROP_LANGUAGE]);
}

EXPORT gboolean gtk_scintilla_get_editable(GtkScintilla* self)
{
	GtkScintillaPrivate* priv = PRIVATE(self);
	return priv->editable;
}

EXPORT void gtk_scintilla_set_editable(GtkScintilla* self, gboolean enb)
{
	GtkScintillaPrivate* priv = PRIVATE(self);
	priv->editable = enb;
	SSM(self, SCI_SETREADONLY, !enb, 0);
	g_object_notify_by_pspec(G_OBJECT(self), props[PROP_EDITABLE]);
}

EXPORT gboolean gtk_scintilla_get_line_number(GtkScintilla* self)
{
	GtkScintillaPrivate* priv = PRIVATE(self);
	return priv->lineNumber;
}

EXPORT void gtk_scintilla_set_line_number(GtkScintilla* self, gboolean enb)
{
	GtkScintillaPrivate* priv = PRIVATE(self);
	priv->lineNumber = enb;
	if (enb)
	{
		SSM(self, SCI_SETMARGINTYPEN, GSCI_NUMBER_MARGIN_INDEX, SC_MARGIN_NUMBER);
		updateLineNumber(self);
	}
	else
	{
		SSM(self, SCI_SETMARGINWIDTHN, GSCI_NUMBER_MARGIN_INDEX, 0);
	}
	g_object_notify_by_pspec(G_OBJECT(self), props[PROP_LINE_NUMBER]);
}

EXPORT guint gtk_scintilla_get_lines(GtkScintilla* self)
{
	GtkScintillaPrivate* priv = PRIVATE(self);
	return priv->lines;
}

EXPORT gboolean gtk_scintilla_get_auto_indent(GtkScintilla* self)
{
	GtkScintillaPrivate* priv = PRIVATE(self);
	return priv->autoIndent;
}

EXPORT void gtk_scintilla_set_auto_indent(GtkScintilla* self, gboolean enb)
{
	GtkScintillaPrivate* priv = PRIVATE(self);
	priv->autoIndent = enb;
	g_object_notify_by_pspec(G_OBJECT(self), props[PROP_AUTO_INDENT]);
}

EXPORT gboolean gtk_scintilla_get_indent_guides(GtkScintilla* sci)
{
	return !!SSM(sci, SCI_GETINDENTATIONGUIDES, 0, 0);
}

EXPORT void gtk_scintilla_set_indent_guides(GtkScintilla* sci, gboolean enb)
{
	SSM(sci, SCI_SETINDENTATIONGUIDES, enb ? SC_IV_LOOKBOTH : SC_IV_NONE, 0);
}

EXPORT gboolean gtk_scintilla_get_fold(GtkScintilla* sci)
{
	GtkScintillaPrivate* priv = PRIVATE(sci);
	return priv->fold;
}

EXPORT void gtk_scintilla_set_fold(GtkScintilla* self, gboolean enb)
{
	GtkScintillaPrivate* priv = PRIVATE(self);
	priv->fold = enb;
	updateFold(priv);
	g_object_notify_by_pspec(G_OBJECT(self), props[PROP_FOLD]);
}

EXPORT GtkWrapMode gtk_scintilla_get_wrap_mode(GtkScintilla* sci)
{
	GtkScintillaPrivate* priv = PRIVATE(sci);
	return priv->wrapMode;
}

EXPORT void gtk_scintilla_set_wrap_mode(GtkScintilla* sci, GtkWrapMode mode)
{
	GtkScintillaPrivate* priv = PRIVATE(sci);
	priv->wrapMode = mode;
	switch (mode)
	{
	case GTK_WRAP_NONE:
		SSM(sci, SCI_SETWRAPMODE, SC_WRAP_NONE, 0);
		break;
	case GTK_WRAP_CHAR:
		SSM(sci, SCI_SETWRAPMODE, SC_WRAP_CHAR, 0);
		break;
	case GTK_WRAP_WORD_CHAR:
	case GTK_WRAP_WORD:
		SSM(sci, SCI_SETWRAPMODE, SC_WRAP_WORD, 0);
		break;
	default:
		return;
	}
	g_object_notify_by_pspec(G_OBJECT(sci), props[PROP_WRAP_MODE]);
}

EXPORT guint gtk_scintilla_get_tab_width(GtkScintilla* sci)
{
	return (guint)SSM(sci, SCI_GETTABWIDTH, 0, 0);
}

EXPORT void gtk_scintilla_set_tab_width(GtkScintilla* sci, guint width)
{
	SSM(sci, SCI_SETTABWIDTH, width, 0);
	g_object_notify_by_pspec(G_OBJECT(sci), props[PROP_TAB_WIDTH]);
}

EXPORT void gtk_scintilla_set_text(GtkScintilla* self, const char* text)
{
	GtkScintillaPrivate* priv = PRIVATE(self);
	SSM(self, SCI_SETREADONLY, 0, 0);
	SSM(self, SCI_SETTEXT, 0, text);
	SSM(self, SCI_SETREADONLY, !priv->editable, 0);
}

EXPORT void gtk_scintilla_append_text(GtkScintilla* self, const char* text, gint64 length)
{
	GtkScintillaPrivate* priv = PRIVATE(self);
	SSM(self, SCI_SETREADONLY, 0, 0);
	SSM(self, SCI_APPENDTEXT, length, text);
	SSM(self, SCI_SETREADONLY, !priv->editable, 0);
}

EXPORT guint64 gtk_scintilla_get_text_length(GtkScintilla* sci)
{
	return SSM(sci, SCI_GETLENGTH, 0, 0);
}

EXPORT guint64 gtk_scintilla_get_text(GtkScintilla* sci, char* buf, guint64 length)
{
	SSM(sci, SCI_GETTEXT, length, buf);
}

EXPORT void gtk_scintilla_clear_text(GtkScintilla* self)
{
	GtkScintillaPrivate* priv = PRIVATE(self);
	SSM(self, SCI_SETREADONLY, 0, 0);
	SSM(self, SCI_CLEARALL, 0, 0);
	SSM(self, SCI_SETREADONLY, !priv->editable, 0);
}

EXPORT void gtk_scintilla_clear_undo_redo(GtkScintilla* self)
{
	GtkScintillaPrivate* priv = PRIVATE(self);
	SSM(self, SCI_SETREADONLY, 0, 0);
	SSM(self, SCI_EMPTYUNDOBUFFER, 0, 0);
	SSM(self, SCI_SETREADONLY, !priv->editable, 0);
}

EXPORT void gtk_scintilla_select_range(GtkScintilla* self, gintptr start, gintptr end)
{
	SSM(self, SCI_SETSEL, start, end);
}

EXPORT void gtk_scintilla_scroll_to_line(GtkScintilla* self, gintptr line, gintptr column)
{
	if (line < 0)
	{
		line = SSM(self, SCI_GETLINECOUNT, 0, 0);
		line--;
	}

	if (column < 0)
	{
		column = SSM(self, SCI_LINELENGTH, 0, 0);
		if (column > 0)
			column--;
	}

	SSM(self, SCI_LINESCROLL, column, line);
}

EXPORT void gtk_scintilla_scroll_to_pos(GtkScintilla* self, gintptr pos)
{
	if (pos < 0)
	{
		pos = SSM(self, SCI_GETLENGTH, 0, 0);
		if (pos > 0)
			pos--;
	}

	gintptr line = SSM(self, SCI_LINEFROMPOSITION, pos, 0);
	gintptr colm = SSM(self, SCI_GETCOLUMN, pos, 0);
	SSM(self, SCI_LINESCROLL, colm, line);
}

EXPORT void gtk_scintilla_reset_search(GtkScintilla* self)
{
	GtkScintillaPrivate* priv = PRIVATE(self);
	priv->searchPos = -1;
}

static gintptr searchRange(GtkScintilla* sci, gintptr beg, gintptr end, const char* text, gintptr length, gboolean matchCase, gboolean wholeWord)
{
	gintptr flag = SCFIND_NONE;
	if (matchCase)
		flag |= SCFIND_MATCHCASE;
	if (wholeWord)
		flag |= SCFIND_WHOLEWORD;

	SSM(sci, SCI_SETSEARCHFLAGS, flag, 0);
	SSM(sci, SCI_SETTARGETRANGE, beg, end);

	return SSM(sci, SCI_SEARCHINTARGET, length, text);
}

EXPORT gintptr gtk_scintilla_search_prev(GtkScintilla* self, const char* text, gintptr length, gboolean matchCase, gboolean wholeWord)
{
	if (length < 0)
		length = strlen(text);

	GtkScintillaPrivate* priv = PRIVATE(self);
	if (priv->searchPos > 0)
	{
		gintptr pos = searchRange(self, priv->searchPos - 1, 0, text, length, matchCase, wholeWord);
		if (pos >= 0)
		{
			priv->searchPos = pos;
			return pos;
		}
	}

	// reverse search
	gintptr start = SSM(self, SCI_GETLENGTH, 0, 0);
	gintptr pos = searchRange(self, start, 0, text, length, matchCase, wholeWord);
	priv->searchPos = pos;

	return pos;
}

EXPORT gintptr gtk_scintilla_search_next(GtkScintilla* self, const char* text, gintptr length, gboolean matchCase, gboolean wholeWord)
{
	if (length < 0)
		length = strlen(text);

	GtkScintillaPrivate* priv = PRIVATE(self);
	gintptr start = priv->searchPos + 1;
	gintptr end = SSM(self, SCI_GETLENGTH, 0, 0);
	gintptr pos = searchRange(self, start, end, text, length, matchCase, wholeWord);
	if (pos < 0)
		pos = searchRange(self, 0, end, text, length, matchCase, wholeWord);

	priv->searchPos = pos;
	return pos;
}

// privates

void gtk_scintilla_class_install_properties(GtkScintillaClass* klass)
{
	props[PROP_DARK] = g_param_spec_boolean("dark", NULL, NULL, FALSE, G_PARAM_READWRITE
		| G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	props[PROP_STYLE] = g_param_spec_string("style", NULL, NULL, "default", G_PARAM_READWRITE
		| G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	props[PROP_LANGUAGE] = g_param_spec_string("language", NULL, NULL, "", G_PARAM_READWRITE
		| G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	props[PROP_EDITABLE] = g_param_spec_boolean("editable", NULL, NULL, TRUE, G_PARAM_READWRITE
		| G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	props[PROP_LINES] = g_param_spec_uint("lines", NULL, NULL, 0, G_MAXUINT, 0, G_PARAM_READABLE
		| G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	props[PROP_LINE_NUMBER] = g_param_spec_boolean("line-number", NULL, NULL, FALSE, G_PARAM_READWRITE
		| G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	props[PROP_FOLD] = g_param_spec_boolean("fold", NULL, NULL, FALSE, G_PARAM_READWRITE
		| G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	props[PROP_AUTO_INDENT] = g_param_spec_boolean("auto-indent", NULL, NULL, FALSE, G_PARAM_READWRITE
		| G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	props[PROP_INDENT_GUIDES] = g_param_spec_boolean("indent-guides", NULL, NULL, FALSE, G_PARAM_READWRITE
		| G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	props[PROP_TAB_WIDTH] = g_param_spec_uint("tab-width", NULL, NULL, 1, 32, 8, G_PARAM_READWRITE
		| G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	props[PROP_WRAP_MODE] = g_param_spec_enum("wrap-mode", NULL, NULL, GTK_TYPE_WRAP_MODE, GTK_WRAP_NONE, G_PARAM_READWRITE
		| G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	g_object_class_install_properties(G_OBJECT_CLASS(klass), PROP_COUNT, props);
}

void gtk_scintilla_get_property(GObject* obj, guint prop, GValue* val, GParamSpec* ps)
{
	GtkScintilla* self = GTK_SCINTILLA(obj);
	switch (prop)
	{
	case PROP_DARK:
		g_value_set_boolean(val, gtk_scintilla_get_dark(self));
		break;

	case PROP_STYLE:
		g_value_set_string(val, gtk_scintilla_get_style(self));
		break;

	case PROP_LANGUAGE:
		g_value_set_string(val, gtk_scintilla_get_language(self));
		break;

	case PROP_EDITABLE:
		g_value_set_boolean(val, gtk_scintilla_get_editable(self));
		break;

	case PROP_LINES:
		g_value_set_uint(val, gtk_scintilla_get_lines(self));
		break;

	case PROP_LINE_NUMBER:
		g_value_set_boolean(val, gtk_scintilla_get_line_number(self));
		break;

	case PROP_FOLD:
		g_value_set_boolean(val, gtk_scintilla_get_fold(self));
		break;

	case PROP_AUTO_INDENT:
		g_value_set_boolean(val, gtk_scintilla_get_auto_indent(self));
		break;

	case PROP_INDENT_GUIDES:
		g_value_set_boolean(val, gtk_scintilla_get_indent_guides(self));

		break;
	case PROP_TAB_WIDTH:
		g_value_set_uint(val, gtk_scintilla_get_tab_width(self));
		break;

	case PROP_WRAP_MODE:
		g_value_set_enum(val, gtk_scintilla_get_wrap_mode(self));
		break;

	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID(obj, prop, ps);
		break;
	}
}

void gtk_scintilla_set_property(GObject* obj, guint prop, const GValue* val, GParamSpec* ps)
{
	GtkScintilla* self = GTK_SCINTILLA(obj);
	switch (prop)
	{
	case PROP_DARK:
		gtk_scintilla_set_dark(self, g_value_get_boolean(val));
		break;

	case PROP_STYLE:
		gtk_scintilla_set_style(self, g_value_get_string(val));
		break;

	case PROP_LANGUAGE:
		gtk_scintilla_set_language(self, g_value_get_string(val));
		break;

	case PROP_EDITABLE:
		gtk_scintilla_set_editable(self, g_value_get_boolean(val));
		break;

	case PROP_LINE_NUMBER:
		gtk_scintilla_set_line_number(self, g_value_get_boolean(val));
		break;

	case PROP_FOLD:
		gtk_scintilla_set_fold(self, g_value_get_boolean(val));
		break;

	case PROP_INDENT_GUIDES:
		gtk_scintilla_set_indent_guides(self, g_value_get_boolean(val));
		break;

	case PROP_TAB_WIDTH:
		gtk_scintilla_set_tab_width(self, g_value_get_uint(val));
		break;

	case PROP_WRAP_MODE:
		gtk_scintilla_set_wrap_mode(self, g_value_get_enum(val));
		break;

	case PROP_AUTO_INDENT:
		gtk_scintilla_set_auto_indent(self, g_value_get_boolean(val));
		break;

	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID(obj, prop, ps);
		break;
	}
}

void gtk_scintilla_class_install_signals(GtkScintillaClass* klass)
{
	signals[SIGNAL_TEXT_CHANGED] = g_signal_new(
		"text-changed",
		G_TYPE_FROM_CLASS(klass),
		G_SIGNAL_RUN_LAST,
		G_STRUCT_OFFSET(GtkScintillaClass, text_changed),
		NULL, NULL,
		g_cclosure_marshal_VOID__VOID,
		G_TYPE_NONE, 0
	);
}


#include "SciLexer.h"
#include "Lexilla.h"

void updateLineNumber(GtkScintilla* self)
{
	GtkScintillaPrivate* priv = PRIVATE(self);
	if (priv->lineNumber)
	{
		int lines = SSM(self, SCI_GETLINECOUNT, 0, 0);
		if (priv->lines != lines)
		{
			// notify lines
			priv->lines = lines;
			g_object_notify_by_pspec(G_OBJECT(self), props[PROP_LINES]);

			// update line number margin width
			char buf[16];
			g_snprintf(buf, sizeof(buf), "_%d", lines);
			int width = SSM(self, SCI_TEXTWIDTH, STYLE_LINENUMBER, (sptr_t)buf);
			SSM(self, SCI_SETMARGINWIDTHN, 0, width);
		}
	}
}

static void configStyle(ScintillaObject* sci, const ScintillaStyle* style, gboolean dark)
{
	// set style color
	guint32 color = 0;
	ScintillaFont font = { 0 };
	for (int i = 0; i < STYLE_MAX; i++)
	{
		if (style->fgColor && style->fgColor(i, dark, &color))
			SSM(sci, SCI_STYLESETFORE, i, color);

		if (style->bgColor && style->bgColor(i, dark, &color))
			SSM(sci, SCI_STYLESETBACK, i, color);

		if (style->fonts && style->fonts(i, dark, &font))
		{
			if (font.name)
				SSM(sci, SCI_STYLESETFONT, i, font.name);
			if (font.size)
				SSM(sci, SCI_STYLESETSIZE, i, font.size);

			SSM(sci, SCI_STYLESETBOLD, i, font.bold);
			SSM(sci, SCI_STYLESETITALIC, i, font.italic);
			SSM(sci, SCI_STYLESETUNDERLINE, i, font.underline);
		}
	}

	// set element color
#define ELEMENT_MAX 81
	for (int i = 0; i < ELEMENT_MAX; i++)
	{
		if (style->elemColor && style->elemColor(i, dark, &color))
			SSM(sci, SCI_SETELEMENTCOLOUR, i, color);
	}

	// set other property
	if (style->setProps)
		style->setProps(sci, dark);		
}

static gboolean configLanguage(ScintillaObject* sci, gboolean dark, const ScintillaLanguage* lang)
{
	// set lexer
	SSM(sci, SCI_SETILEXER, 0, 0);
	if (lang->lexer)
	{
		void* lexer = CreateLexer(lang->lexer);
		if (!lexer)
			return false;
		SSM(sci, SCI_SETILEXER, 0, lexer);
	}

	// set keywords
	if (lang->keywords)
	{
		for (int i = 0; i < KEYWORDSET_MAX; i++)
		{
			if (lang->keywords[i] != NULL)
				SSM(sci, SCI_SETKEYWORDS, i, lang->keywords[i]);
		}
	}

	// set colors
	guint32 defFgColor = SSM(sci, SCI_STYLEGETFORE, STYLE_DEFAULT, 0);
	guint32 defBgColor = SSM(sci, SCI_STYLEGETBACK, STYLE_DEFAULT, 0);

	for (int i = 0; i < STYLE_MAX; i++)
	{
		guint32 color = defFgColor;
		if (lang->fgColor && lang->fgColor(i, dark, &color))
			SSM(sci, SCI_STYLESETFORE, i, color);

		color = defBgColor;
		if (lang->bgColor && lang->bgColor(i, dark, &color))
			SSM(sci, SCI_STYLESETBACK, i, color);

		ScintillaFont font = { 0 };
		if (lang->fonts && lang->fonts(i, dark, &font))
		{
			if (font.name)
				SSM(sci, SCI_STYLESETFONT, i, font.name);
			if (font.size)
				SSM(sci, SCI_STYLESETSIZE, i, font.size);
			if (font.bold)
				SSM(sci, SCI_STYLESETBOLD, i, font.bold);
			if (font.italic)
				SSM(sci, SCI_STYLESETITALIC, i, font.italic);
			if (font.underline)
				SSM(sci, SCI_STYLESETUNDERLINE, i, font.underline);
		}
	}

	// set property
	if (lang->setProps)
		lang->setProps(sci);

	return true;
}

static void configFold(ScintillaObject* sci, gboolean enb)
{

	if (enb)
	{
		// enable fold modify event TODO: fix undo disabled fold BUG
		int mask = SSM(sci, SCI_GETMODEVENTMASK, 0, 0);
		SSM(sci, SCI_SETMODEVENTMASK, mask | SC_MOD_CHANGEFOLD, 0);

		// fold margin
		SSM(sci, SCI_SETMARGINWIDTHN, GSCI_FOLD_MARGIN_INDEX, GSCI_FOLD_MARGIN_WIDTH);
		SSM(sci, SCI_SETMARGINTYPEN, GSCI_FOLD_MARGIN_INDEX, SC_MARGIN_SYMBOL);
		SSM(sci, SCI_SETMARGINMASKN, GSCI_FOLD_MARGIN_INDEX, SC_MASK_FOLDERS);
		SSM(sci, SCI_SETMARGINSENSITIVEN, GSCI_FOLD_MARGIN_INDEX, 1);

		// enable fold
		SSM(sci, SCI_SETPROPERTY, "fold", "1");

		// define fold mark
		SSM(sci, SCI_MARKERDEFINE, SC_MARKNUM_FOLDEROPEN, SC_MARK_BOXMINUS);
		SSM(sci, SCI_MARKERDEFINE, SC_MARKNUM_FOLDER, SC_MARK_BOXPLUS);
		SSM(sci, SCI_MARKERDEFINE, SC_MARKNUM_FOLDERSUB, SC_MARK_VLINE);
		SSM(sci, SCI_MARKERDEFINE, SC_MARKNUM_FOLDERTAIL, SC_MARK_LCORNER);
		SSM(sci, SCI_MARKERDEFINE, SC_MARKNUM_FOLDEREND, SC_MARK_BOXPLUSCONNECTED);
		SSM(sci, SCI_MARKERDEFINE, SC_MARKNUM_FOLDEROPENMID, SC_MARK_BOXMINUSCONNECTED);
		SSM(sci, SCI_MARKERDEFINE, SC_MARKNUM_FOLDERMIDTAIL, SC_MARK_TCORNER);
		SSM(sci, SCI_SETAUTOMATICFOLD, SC_AUTOMATICFOLD_SHOW | SC_AUTOMATICFOLD_CLICK | SC_AUTOMATICFOLD_CHANGE, 0);
		SSM(sci, SCI_SETFOLDFLAGS, SC_FOLDFLAG_LINEAFTER_CONTRACTED, 0);
	}
	else
	{
		SSM(sci, SCI_SETMARGINWIDTHN, GSCI_FOLD_MARGIN_INDEX, 0);
	}
}

void updateStyle(GtkScintillaPrivate* priv)
{
	// set style
	if (priv->style)
		configStyle(priv->sci, priv->style, priv->dark);

	// language style
	if (priv->lang)
		configLanguage(priv->sci, priv->dark, priv->lang);

	// fold
	configFold(priv->sci, priv->fold);

	// update color
	SSM(priv->sci, SCI_COLOURISE, 0, -1);
}

gboolean updateLanguage(GtkScintillaPrivate* priv)
{
	return configLanguage(priv->sci, priv->dark, priv->lang);
}

void updateFold(GtkScintillaPrivate* priv)
{
	configFold(priv->sci, priv->fold);
}

static void lineIndent(GtkScintilla* self)
{
	int pos = SSM(self, SCI_GETSELECTIONSTART, 0, 0);
	int line = SSM(self, SCI_LINEFROMPOSITION, pos, 0);
	int prev = line - 1;
	if (prev >= 0)
	{
		int lineStart = SSM(self, SCI_POSITIONFROMLINE, prev, 0);
		int lineEnd = SSM(self, SCI_GETLINEENDPOSITION, prev, 0);
		int lineLen = lineEnd - lineStart;
		if (lineLen != 0)
		{
			int indent = SSM(self, SCI_GETLINEINDENTATION, prev, 0);
			if (indent)
			{
				SSM(self, SCI_SETLINEINDENTATION, line, indent);
				int newPos = SSM(self, SCI_GETLINEENDPOSITION, line, 0);
				SSM(self, SCI_SETSEL, newPos, newPos);
			}
		}
	}
}

void onSciNotify(GtkScintilla* self, gint param, SCNotification* notif, GtkScintillaPrivate* priv)
{
	switch (notif->nmhdr.code)
	{
	case SCN_MODIFIED:
	{
		int mod = notif->modificationType;
		if (mod & SC_MOD_INSERTTEXT || mod & SC_MOD_DELETETEXT)
		{
			updateLineNumber(self);
			g_signal_emit(self, signals[SIGNAL_TEXT_CHANGED], 0);
		}
		break;
	}
	case SCN_UPDATEUI:
		//printf("sci-notify update ui\n");
		break;
	case SCN_CHARADDED:
	{
		if (notif->ch == '\n')
		{
			updateLineNumber(self);
			if (priv->autoIndent)
				lineIndent(self);
		}
		break;
	}
	}
}

#define CASE_COLOR(INDEX, LIGHT_COLOR, DARK_COLOR) case INDEX: *color = dark ? DARK_COLOR : LIGHT_COLOR; return true
#define CASE_COLOR_DEF(INDEX) case INDEX: return true

// vscode style

gboolean vscodeFgColor(int index, gboolean dark, guint32* color)
{
#define DEFAULT_FG_LIGHT HEX_RGB(0x3B3B3B)
#define DEFAULT_FG_DARK  HEX_RGB(0xCBCBCB)

#define DEFAULT_INDENT_LIGHT    HEX_RGB(0xDCDCDC)
#define DEFAULT_INDENT_DARK    HEX_RGB(0x707070)

	switch (index)
	{
		CASE_COLOR(STYLE_DEFAULT, DEFAULT_FG_LIGHT, DEFAULT_FG_DARK);
		CASE_COLOR(STYLE_LINENUMBER, DEFAULT_FG_LIGHT, DEFAULT_FG_DARK);
		CASE_COLOR(STYLE_INDENTGUIDE, DEFAULT_INDENT_LIGHT,DEFAULT_INDENT_DARK);
	}
	return false;
}

gboolean vscodeBgColor(int index, gboolean dark, guint32* color)
{
#define DEFAULT_BG_LIGHT HEX_RGB(0xFFFFFF)
#define DEFAULT_BG_DARK  HEX_RGB(0x1F1F1F)
#define DEFAULT_SELECTION_LIGHT HEX_RGB(0xADD6FF)
#define DEFAULT_SELECTION_DARK  HEX_RGB(0x264F78)


	switch (index)
	{
		CASE_COLOR(STYLE_DEFAULT, DEFAULT_BG_LIGHT, DEFAULT_BG_DARK);
		CASE_COLOR(STYLE_LINENUMBER, DEFAULT_BG_LIGHT, DEFAULT_BG_DARK);
		CASE_COLOR(STYLE_INDENTGUIDE, DEFAULT_BG_LIGHT, DEFAULT_BG_DARK);
	}
	return false;
}

gboolean vscodeElemColor(int index, gboolean dark, guint32* color)
{
#define CARET_LIGHT         HEX_RGBA(0x000000FF)
#define CARET_DARK          HEX_RGBA(0xAEAFADFF)

#define SELECTION_BACK_LIGHT HEX_RGB(0xADD6FF)
#define SELECTION_BACK_DARK  HEX_RGB(0x264F78)

#define DEFAULT_SELECTION_INACTIVE_LIGHT HEX_RGB(0xE5EBF1)
#define DEFAULT_SELECTION_INACTIVE_DARK HEX_RGB(0x3A3D41)

#define DEFAULT_LINE_LIGHT HEX_RGB(0xEEEEEE)
#define DEFAULT_LINE_DARK  HEX_RGB(0x282828)

	switch (index)
	{
		CASE_COLOR(SC_ELEMENT_CARET, CARET_LIGHT, CARET_DARK);
		CASE_COLOR(SC_ELEMENT_SELECTION_BACK, SELECTION_BACK_LIGHT, SELECTION_BACK_DARK);
		CASE_COLOR(SC_ELEMENT_SELECTION_SECONDARY_BACK, SELECTION_BACK_LIGHT, SELECTION_BACK_DARK);
		CASE_COLOR(SC_ELEMENT_SELECTION_ADDITIONAL_BACK, SELECTION_BACK_LIGHT, SELECTION_BACK_DARK);
		CASE_COLOR(SC_ELEMENT_SELECTION_INACTIVE_BACK, DEFAULT_SELECTION_INACTIVE_LIGHT, DEFAULT_SELECTION_INACTIVE_DARK);
		CASE_COLOR(SC_ELEMENT_SELECTION_INACTIVE_ADDITIONAL_BACK, DEFAULT_SELECTION_INACTIVE_LIGHT, DEFAULT_SELECTION_INACTIVE_DARK);
		CASE_COLOR(SC_ELEMENT_CARET_LINE_BACK, DEFAULT_LINE_LIGHT, DEFAULT_LINE_DARK);
	}
	return false;
}

gboolean vscodeFonts(int index, gboolean dark, ScintillaFont* font)
{
#define VSCODE_FONT_NAME "Consolas,'Courier New',monospace"
#define VSCODE_FONT_SIZE 12

	switch (index)
	{
	case STYLE_DEFAULT:
		font->name = VSCODE_FONT_NAME;
		font->size = VSCODE_FONT_SIZE;
		return true;
	}
	return false;
}

void vscodeSetProps(ScintillaObject* sci, gboolean dark)
{
	// set margin width
	SSM(sci, SCI_SETMARGINWIDTHN, GSCI_SYMBOL_MARGIN_INDEX, GSCI_SYMBOL_MARGIN_WIDTH);

	// set current line highlight
	SSM(sci, SCI_SETCARETLINEVISIBLE, true, 0);
	SSM(sci, SCI_GETCARETLINEVISIBLEALWAYS, true, 0);
	SSM(sci, SCI_SETCARETWIDTH, GSCI_CARET_WIDTH, 0);
	SSM(sci, SCI_SETCARETLINEFRAME, GSCI_LINE_FRAME_WIDTH, 0);

	// set fold style
	guint32 fgColor = 0x101010, bgColor = 0xF0F0F0;
	vscodeFgColor(STYLE_LINENUMBER, dark, &fgColor);
	vscodeBgColor(STYLE_LINENUMBER, dark, &bgColor);

	gint markers[] = {
		SC_MARKNUM_FOLDEROPEN,
		SC_MARKNUM_FOLDER,
		SC_MARKNUM_FOLDERSUB,
		SC_MARKNUM_FOLDERTAIL,
		SC_MARKNUM_FOLDEREND,
		SC_MARKNUM_FOLDEROPENMID,
		SC_MARKNUM_FOLDERMIDTAIL
	};

	for (int i = 0; i < 7; i++)
	{
		SSM(sci, SCI_MARKERSETFORE, markers[i], bgColor);
		SSM(sci, SCI_MARKERSETBACK, markers[i], fgColor); // reverse
	}

	SSM(sci, SCI_SETFOLDMARGINHICOLOUR, 0, 0);
	SSM(sci, SCI_SETFOLDMARGINCOLOUR, 0, 0);

	SSM(sci, SCI_SETFOLDMARGINHICOLOUR, 1, bgColor);
	SSM(sci, SCI_SETFOLDMARGINCOLOUR, 1, bgColor);
}

// json language

gboolean jsonFgColor(int index, gboolean dark, guint32* color)
{
#define JSON_KEY_LIGHT     HEX_RGB(0x0451A5)
#define JSON_NUMBER_LIGHT  HEX_RGB(0x098658)
#define JSON_STRING_LIGHT  HEX_RGB(0xA31515)
#define JSON_ESCAPE_LIGHT  HEX_RGB(0xEE0000)
#define JSON_KEYWORD_LIGHT HEX_RGB(0x0000FF)
#define JSON_COMMENT_LIGHT HEX_RGB(0x008000)
#define JSON_ERROR_LIGHT   HEX_RGB(0xE51400)

#define JSON_KEY_DARK      HEX_RGB(0x9CDCFE)
#define JSON_NUMBER_DARK   HEX_RGB(0xB5CEA8)
#define JSON_STRING_DARK   HEX_RGB(0xCE9178)
#define JSON_ESCAPE_DARK   HEX_RGB(0xD7BA7D)
#define JSON_KEYWORD_DARK  HEX_RGB(0x569CD6)
#define JSON_COMMENT_DARK  HEX_RGB(0x6B9955)
#define JSON_ERROR_DARK    HEX_RGB(0xF24C4C)

	switch (index)
	{
		CASE_COLOR_DEF(SCE_JSON_DEFAULT);
		CASE_COLOR(SCE_JSON_NUMBER, JSON_NUMBER_LIGHT, JSON_NUMBER_DARK);
		CASE_COLOR(SCE_JSON_STRING, JSON_STRING_LIGHT, JSON_STRING_DARK);
		CASE_COLOR(SCE_JSON_PROPERTYNAME, JSON_KEY_LIGHT, JSON_KEY_DARK);
		CASE_COLOR(SCE_JSON_ESCAPESEQUENCE, JSON_ESCAPE_LIGHT, JSON_ESCAPE_DARK);
		CASE_COLOR(SCE_JSON_LINECOMMENT, JSON_COMMENT_LIGHT, JSON_COMMENT_DARK);
		CASE_COLOR(SCE_JSON_BLOCKCOMMENT, JSON_COMMENT_LIGHT, JSON_COMMENT_DARK);
		CASE_COLOR_DEF(SCE_JSON_OPERATOR);
		CASE_COLOR(SCE_JSON_URI, JSON_STRING_LIGHT, JSON_STRING_DARK);
		CASE_COLOR(SCE_JSON_STRINGEOL, JSON_STRING_LIGHT, JSON_STRING_DARK);
		CASE_COLOR_DEF(SCE_JSON_COMPACTIRI); 
		CASE_COLOR(SCE_JSON_KEYWORD, JSON_KEYWORD_LIGHT, JSON_KEYWORD_DARK);
		CASE_COLOR(SCE_JSON_LDKEYWORD, JSON_KEYWORD_LIGHT, JSON_KEYWORD_DARK);
		CASE_COLOR(SCE_JSON_ERROR, JSON_ERROR_LIGHT, JSON_ERROR_DARK);
	}
	return false;
}

gboolean jsonBgColor(int index, gboolean dark, guint32* color)
{
	switch (index)
	{
		CASE_COLOR_DEF(SCE_JSON_DEFAULT);
		CASE_COLOR_DEF(SCE_JSON_PROPERTYNAME);
		CASE_COLOR_DEF(SCE_JSON_NUMBER);
		CASE_COLOR_DEF(SCE_JSON_STRING);
		CASE_COLOR_DEF(SCE_JSON_STRINGEOL);
		CASE_COLOR_DEF(SCE_JSON_URI);
		CASE_COLOR_DEF(SCE_JSON_ESCAPESEQUENCE);
		CASE_COLOR_DEF(SCE_JSON_LINECOMMENT);
		CASE_COLOR_DEF(SCE_JSON_BLOCKCOMMENT);
		CASE_COLOR_DEF(SCE_JSON_OPERATOR);
		CASE_COLOR_DEF(SCE_JSON_COMPACTIRI);
		CASE_COLOR_DEF(SCE_JSON_KEYWORD);
		CASE_COLOR_DEF(SCE_JSON_LDKEYWORD);
		CASE_COLOR_DEF(SCE_JSON_ERROR);
	}
	return false;
}

gboolean jsonFonts(int index, gboolean dark, ScintillaFont* font)
{
	switch (index)
	{
	case SCE_JSON_URI:
		font->underline = true;
		return true;
	}
	return false;
}

void jsonSetProps(ScintillaObject* sci)
{
	SSM(sci, SCI_SETPROPERTY, "lexer.json.escape.sequence", "1");
	SSM(sci, SCI_SETPROPERTY, "lexer.json.allow.comments", "1");
}
