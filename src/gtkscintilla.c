#include <gtk/gtk.h>

#include "scintilla.h"
#include "scintillaWidget.h"

#ifdef _WIN32
#define EXPORT __declspec(dllexport)
#else
#define EXPORT __attribute__((visibility("default")))
#endif

#define GTK_TYPE_SCINTILLA (gtk_scintilla_get_type())

typedef struct _GtkScintilla GtkScintilla;
typedef struct _GtkScintillaClass GtkScintillaClass;

struct _GtkScintilla {
	ScintillaObject parent_instance;
};

struct _GtkScintillaClass
{
	ScintillaObjectClass parent_class;
	// TODO: add signals
};

typedef struct _ScintillaStyle ScintillaStyle;
typedef struct _ScintillaFont ScintillaFont;
typedef struct _ScintillaLanguage ScintillaLanguage;

typedef struct _GtkScintillaPrivate
{
	ScintillaObject* sci;
	const ScintillaStyle* style;
	const ScintillaLanguage* lang;
	void* lexer;
	GtkSettings* settings;
	gboolean dark;
	gulong sigDarkChanged;

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
	{ "json", "json", jsonKeywords, jsonFgColor, jsonBgColor, jsonFonts, jsonSetProps },
	{ NULL }
};

static gboolean getDark(GtkSettings* set);
static void updateStyle(GtkScintillaPrivate* priv);
static gboolean updateLanguage(GtkScintillaPrivate* priv);
static void onDarkChanged(GtkSettings* self, GParamSpec* spec, GtkScintillaPrivate* priv);

static void gtk_scintilla_finalize(GObject* self);

static void gtk_scintilla_class_init(GtkScintillaClass* klass)
{
	// TODO: init property and signal
	GObjectClass* cls = G_OBJECT_CLASS(klass);
	cls->finalize = gtk_scintilla_finalize;
}

static void gtk_scintilla_init(GtkScintilla* sci)
{
	GtkScintillaPrivate* priv = PRIVATE(sci);
	priv->sci = SCINTILLA(sci);
	priv->style = NULL;
	priv->lang = NULL;
	priv->settings = gtk_settings_get_default();
	priv->dark = getDark(priv->settings);
	priv->sigDarkChanged = g_signal_connect(priv->settings, "notify::gtk-application-prefer-dark-theme", G_CALLBACK(onDarkChanged), priv);

	SSM(sci, SCI_SETBUFFEREDDRAW, 0, 0);
}

static void gtk_scintilla_finalize(GObject* self)
{
	GtkScintillaPrivate* priv = PRIVATE((GtkScintilla*)self);
	g_signal_handler_disconnect(priv->settings, priv->sigDarkChanged);
}


EXPORT GtkWidget* gtk_scintilla_new(void)
{
	return g_object_new(GTK_TYPE_SCINTILLA, NULL);
}

EXPORT gboolean gtk_scintilla_set_style(GtkScintilla* sci, const char* styleName)
{
	for (const ScintillaStyle* style = &GSCI_STYLES[0]; style->name != NULL; style++)
	{
		if (strcmp(style->name, styleName) == 0)
		{
			GtkScintillaPrivate* priv = PRIVATE(sci);
			priv->style = style;
			priv->dark = getDark(priv->settings);
			updateStyle(priv);
			return true;
		}
	}
	return false;
}

EXPORT gboolean gtk_scintilla_set_language(GtkScintilla* sci, const char* language)
{
	for (const ScintillaLanguage* lang = &GSCI_LANGUAGES[0]; lang->language != NULL; lang++)
	{
		if (strcmp(lang->language, language) == 0)
		{
			GtkScintillaPrivate* priv = PRIVATE(sci);
			priv->lang = lang;
			return updateLanguage(priv);
		}
	}
	return false;
}

EXPORT void gtk_scintilla_set_editable(GtkScintilla* sci, gboolean enb)
{
	SSM(sci, SCI_SETREADONLY, enb, 0);
}

static void updateLineNumberWidth(GtkScintilla* sci);

EXPORT void gtk_scintilla_set_line_number(GtkScintilla* sci, gboolean enb)
{
	if (enb)
	{
		SSM(sci, SCI_SETMARGINTYPEN, GSCI_NUMBER_MARGIN_INDEX, SC_MARGIN_NUMBER);
		updateLineNumberWidth(sci);
	}
	else
	{
		SSM(sci, SCI_SETMARGINWIDTHN, GSCI_NUMBER_MARGIN_INDEX, 0);
	}
}

EXPORT void gtk_scintilla_set_indent_guides(GtkScintilla* sci, gboolean enb)
{
	SSM(sci, SCI_SETINDENTATIONGUIDES, enb ? SC_IV_LOOKBOTH : SC_IV_NONE, 0);
}

EXPORT void gtk_scintilla_set_fold(GtkScintilla* sci, gboolean enb)
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

EXPORT void gtk_scintilla_set_wrap_mode(GtkScintilla* sci, GtkWrapMode mode)
{
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
}

EXPORT void gtk_scintilla_set_tab_width(GtkScintilla* sci, int width)
{
	SSM(sci, SCI_SETTABWIDTH, width, 0);
}

EXPORT void gtk_scintilla_set_text(GtkScintilla* sci, const char* text, gint64 length)
{
	SSM(sci, SCI_SETTEXT, length, text);
}

EXPORT void gtk_scintilla_append_text(GtkScintilla* sci, const char* text, gint64 length)
{
	SSM(sci, SCI_APPENDTEXT, length, text);
}

EXPORT guint64 gtk_scintilla_get_text_length(GtkScintilla* sci)
{
	return SSM(sci, SCI_GETLENGTH, 0, 0);
}

EXPORT guint64 scinitlla_get_text(GtkScintilla* sci, char* buf, guint64 length)
{
	SSM(sci, SCI_GETTEXT, length, buf);
}

EXPORT void gtk_scintilla_clear_text(GtkScintilla* self)
{
	SSM(self, SCI_CLEARALL, 0, 0);
}

EXPORT void gtk_scintilla_clear_undo(GtkScintilla* self)
{
	SSM(self, SCI_EMPTYUNDOBUFFER, 0, 0);
}


// privates

#include "SciLexer.h"
#include "Lexilla.h"

void updateLineNumberWidth(GtkScintilla* sci)
{
	char buf[16];
	int lines = SSM(sci, SCI_GETLINECOUNT, 0, 0);
	g_snprintf(buf, sizeof(buf), "_%d", lines);
	int width = SSM(sci, SCI_TEXTWIDTH, STYLE_LINENUMBER, (sptr_t)buf);
	SSM(sci, SCI_SETMARGINWIDTHN, 0, width);
}

void configStyle(ScintillaObject* sci, const ScintillaStyle* style, gboolean dark)
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

gboolean configLanguage(ScintillaObject* sci, gboolean dark, const ScintillaLanguage* lang)
{
	// set lexer
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

void updateStyle(GtkScintillaPrivate* priv)
{
	// set style
	if (priv->style)
		configStyle(priv->sci, priv->style, priv->dark);

	// language style
	if (priv->lang)
		configLanguage(priv->sci, priv->dark, priv->lang);

	// update color
	SSM(priv->sci, SCI_COLOURISE, 0, -1);
}

gboolean updateLanguage(GtkScintillaPrivate* priv)
{
	return configLanguage(priv->sci, priv->dark, priv->lang);
}

gboolean getDark(GtkSettings* set)
{
	gboolean val = false;
	g_object_get(G_OBJECT(set), "gtk-application-prefer-dark-theme", &val);
	return val;
}

void onDarkChanged(GtkSettings* self, GParamSpec* spec, GtkScintillaPrivate* priv)
{
	priv->dark = getDark(priv->settings);
	updateStyle(priv);
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
