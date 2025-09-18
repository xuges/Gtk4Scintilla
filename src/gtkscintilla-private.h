#pragma once

#include <gtk/gtk.h>

#ifdef _WIN32
#define GSCI_EXTERN __declspec(dllexport) extern
#else
#define GSCI_EXTERN __attribute__((visibility("default")))
#endif

#include "scintilla.h"
#include "scintillaWidget.h"

GSCI_EXTERN
G_DECLARE_DERIVABLE_TYPE(GtkScintilla, gtk_scintilla, GTK, SCINITLLA, ScintillaObject)

struct _GtkScintillaClass
{
	ScintillaObjectClass parent_class;

};

GSCI_EXTERN
const char** gtk_scintilla_get_styles(void);

GSCI_EXTERN
void gtk_scintilla_set_editable(GtkScintilla* self, gboolean enb);

GSCI_EXTERN
void gtk_scintilla_set_style(GtkScintilla* self, gboolean dark);

GSCI_EXTERN
void gtk_scintilla_set_syntax(GtkScintilla* self, const char* syntax);

GSCI_EXTERN
void gtk_scintilla_set_line_number(GtkScintilla* self, gboolean enb);

GSCI_EXTERN
void gtk_scintilla_set_indent_guides(GtkScintilla* self, gboolean enb);

GSCI_EXTERN
void gtk_scintilla_set_fold(GtkScintilla* self, gboolean enb);

GSCI_EXTERN
void gtk_scintilla_set_wrap_mode(GtkScintilla* self, GtkWrapMode mode);

GSCI_EXTERN
void gtk_scintilla_set_tab_width(GtkScintilla* self, int width);

GSCI_EXTERN
void gtk_scintilla_set_text(GtkScintilla* self, const char* text, gint64 length);

GSCI_EXTERN
void gtk_scintilla_append_text(GtkScintilla* self, const char* text, gint64 length);

GSCI_EXTERN
guint64 gtk_scintilla_get_text_length(GtkScintilla* self);

GSCI_EXTERN
guint64 scinitlla_get_text(GtkScintilla* self, char* buf, guint64 length);