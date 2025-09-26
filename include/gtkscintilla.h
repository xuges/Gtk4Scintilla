#pragma once
#ifndef GTK_SCINTILLA_H
#define GTK_SCINTILLA_H

#include <gtk/gtk.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef GSCI_EXTERN
#ifdef _WIN32
#define GSCI_EXTERN __declspec(dllimport) extern
#else
#define GSCI_EXTERN
#endif
#endif

typedef struct _ScintillaObject ScintillaObject;
typedef struct _ScintillaObjectClass  ScintillaObjectClass;

struct _ScintillaObject {
	GtkWidget parent;
	gpointer padding;
};

struct _ScintillaObjectClass {
	GtkWidgetClass parent_class;
	gpointer padding[2];
};


G_BEGIN_DECLS

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

GSCI_EXTERN GType gtk_scintilla_get_type(void);
GSCI_EXTERN GtkWidget* gtk_scintilla_new(void);
GSCI_EXTERN const char* gtk_scintilla_get_style(GtkScintilla* self);
GSCI_EXTERN gboolean gtk_scintilla_set_style(GtkScintilla* self, const char* styleName);
GSCI_EXTERN const char* gtk_scintilla_get_language(GtkScintilla* self);
GSCI_EXTERN gboolean gtk_scintilla_set_language(GtkScintilla* self, const char* language);
GSCI_EXTERN gboolean gtk_scintilla_get_editable(GtkScintilla* self);
GSCI_EXTERN void gtk_scintilla_set_editable(GtkScintilla* self, gboolean enb);
GSCI_EXTERN gboolean gtk_scintilla_get_line_number(GtkScintilla* self);
GSCI_EXTERN void gtk_scintilla_set_line_number(GtkScintilla* self, gboolean enb);
GSCI_EXTERN guint gtk_scintilla_get_lines(GtkScintilla* self);
GSCI_EXTERN gboolean gtk_scintilla_get_auto_indent(GtkScintilla* self);
GSCI_EXTERN void gtk_scintilla_set_auto_indent(GtkScintilla* self, gboolean enb);
GSCI_EXTERN gboolean gtk_scintilla_get_indent_guides(GtkScintilla* self);
GSCI_EXTERN void gtk_scintilla_set_indent_guides(GtkScintilla* self, gboolean enb);
GSCI_EXTERN gboolean gtk_scintilla_get_fold(GtkScintilla* self);
GSCI_EXTERN void gtk_scintilla_set_fold(GtkScintilla* self, gboolean enb);
GSCI_EXTERN GtkWrapMode gtk_scintilla_get_wrap_mode(GtkScintilla* self);
GSCI_EXTERN void gtk_scintilla_set_wrap_mode(GtkScintilla* self, GtkWrapMode mode);
GSCI_EXTERN guint gtk_scintilla_get_tab_width(GtkScintilla* self);
GSCI_EXTERN void gtk_scintilla_set_tab_width(GtkScintilla* self, guint width);
GSCI_EXTERN void gtk_scintilla_set_text(GtkScintilla* self, const char* text);
GSCI_EXTERN void gtk_scintilla_append_text(GtkScintilla* self, const char* text, gint64 length);
GSCI_EXTERN guint64 gtk_scintilla_get_text_length(GtkScintilla* self);
GSCI_EXTERN guint64 gtk_scinitlla_get_text(GtkScintilla* self, char* buf, guint64 length);
GSCI_EXTERN void gtk_scintilla_clear_text(GtkScintilla* self);
GSCI_EXTERN void gtk_scintilla_clear_undo_redo(GtkScintilla* self);

G_BEGIN_DECLS

#ifdef __cplusplus
}
#endif

#endif