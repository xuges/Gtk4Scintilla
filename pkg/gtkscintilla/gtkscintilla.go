package gtkscintilla

/*
#cgo pkg-config: gtk4scintilla
#include <gtkscintilla.h>
#include <stdlib.h>
#include <glib-object.h>
*/
import "C"

import (
	"runtime"
	"unsafe"

	"github.com/diamondburned/gotk4/pkg/core/gextras"
	coreglib "github.com/diamondburned/gotk4/pkg/core/glib"
	"github.com/diamondburned/gotk4/pkg/gtk/v4"
)

var GTypeScintilla = coreglib.Type(C.gtk_scintilla_get_type())

func init() {
	coreglib.RegisterGValueMarshalers([]coreglib.TypeMarshaler{
		coreglib.TypeMarshaler{
			T: GTypeScintilla,
			F: marshalScintilla,
		},
	})
	coreglib.RegisterClassInfo[*Scintilla, *ScintillaClass, ScintillaOverrides](
		GTypeScintilla,
		initScintillaClass,
		wrapScintilla,
		defaultScintillaOverrides,
	)
}

type Scintilla struct {
	_ [0]func()
	gtk.Widget
}

func NewScintilla() *Scintilla {
	p := C.gtk_scintilla_new()
	return wrapScintilla(coreglib.Take(unsafe.Pointer(p)))
}

func (s *Scintilla) Style() string {
	ret := C.gtk_scintilla_get_style(s.self())
	runtime.KeepAlive(s)
	return C.GoString(ret)
}

func (s *Scintilla) SetStyle(style string) {
	arg := C.CString(style)
	defer C.free(unsafe.Pointer(arg))
	C.gtk_scintilla_set_style(s.self(), arg)
	runtime.KeepAlive(style)
	runtime.KeepAlive(s)
}

func (s *Scintilla) Language() string {
	ret := C.gtk_scintilla_get_language(s.self())
	runtime.KeepAlive(s)
	return C.GoString(ret)
}

func (s *Scintilla) SetLanguage(style string) {
	arg := C.CString(style)
	defer C.free(unsafe.Pointer(arg))
	C.gtk_scintilla_set_language(s.self(), arg)
	runtime.KeepAlive(style)
	runtime.KeepAlive(s)
}

func (s *Scintilla) Editable() bool {
	ret := C.gtk_scintilla_get_editable(s.self())
	runtime.KeepAlive(s)
	return ret != 0
}

func (s *Scintilla) SetEditable(v bool) {
	C.gtk_scintilla_set_editable(s.self(), s.boolean(v))
	runtime.KeepAlive(s)
}

func (s *Scintilla) LineNumber() bool {
	ret := C.gtk_scintilla_get_line_number(s.self())
	runtime.KeepAlive(s)
	return ret != 0
}

func (s *Scintilla) SetLineNumber(v bool) {
	C.gtk_scintilla_set_line_number(s.self(), s.boolean(v))
	runtime.KeepAlive(s)
}

func (s *Scintilla) AutoIndent() bool {
	ret := C.gtk_scintilla_get_auto_indent(s.self())
	runtime.KeepAlive(s)
	return ret != 0
}

func (s *Scintilla) SetAutoIndent(v bool) {
	C.gtk_scintilla_set_auto_indent(s.self(), s.boolean(v))
	runtime.KeepAlive(s)
}

func (s *Scintilla) IndentGuides() bool {
	ret := C.gtk_scintilla_get_indent_guides(s.self())
	runtime.KeepAlive(s)
	return ret != 0
}

func (s *Scintilla) SetIndentGuides(v bool) {
	C.gtk_scintilla_set_indent_guides(s.self(), s.boolean(v))
	runtime.KeepAlive(s)
}

func (s *Scintilla) Fold() bool {
	ret := C.gtk_scintilla_get_fold(s.self())
	runtime.KeepAlive(s)
	return ret != 0
}

func (s *Scintilla) SetFold(v bool) {
	C.gtk_scintilla_set_fold(s.self(), s.boolean(v))
	runtime.KeepAlive(s)
}

func (s *Scintilla) TabWidth() uint {
	ret := C.gtk_scintilla_get_tab_width(s.self())
	runtime.KeepAlive(s)
	return uint(ret)
}

func (s *Scintilla) SetTabWidth(width uint) {
	C.gtk_scintilla_set_tab_width(s.self(), C.guint(width))
	runtime.KeepAlive(s)
}

func (s *Scintilla) WrapMode() gtk.WrapMode {
	ret := C.gtk_scintilla_get_wrap_mode(s.self())
	runtime.KeepAlive(s)
	return gtk.WrapMode(ret)
}

func (s *Scintilla) SetWrapMode(mode gtk.WrapMode) {
	C.gtk_scintilla_set_wrap_mode(s.self(), C.GtkWrapMode(mode))
	runtime.KeepAlive(s)
}

func (s *Scintilla) Lines() uint {
	ret := C.gtk_scintilla_get_lines(s.self())
	runtime.KeepAlive(s)
	return uint(ret)
}

func (s *Scintilla) Text() string {
	length := C.gtk_scintilla_get_text_length(s.self())
	buf := make([]byte, int(length))
	C.gtk_scintilla_get_text(s.self(), (*C.char)(unsafe.Pointer(unsafe.SliceData(buf))), length)
	runtime.KeepAlive(buf)
	runtime.KeepAlive(s)
	return unsafe.String(unsafe.SliceData(buf), len(buf))
}

func (s *Scintilla) SetText(text string) {
	if len(text) != 0 {
		if text[len(text)-1] == 0 {
			arg := unsafe.StringData(text)
			C.gtk_scintilla_set_text(s.self(), (*C.char)(unsafe.Pointer(arg)))
			runtime.KeepAlive(arg)
			runtime.KeepAlive(s)
			return
		}
		arg := C.CString(text)
		defer C.free(unsafe.Pointer(arg))
		C.gtk_scintilla_set_text(s.self(), arg)
		runtime.KeepAlive(s)
	}
}

func (s *Scintilla) AppendText(text string) {
	if len(text) != 0 {
		if text[len(text)-1] == 0 {
			arg := unsafe.StringData(text)
			C.gtk_scintilla_append_text(s.self(), (*C.char)(unsafe.Pointer(arg)), C.gintptr(len(text)-1))
			runtime.KeepAlive(arg)
			runtime.KeepAlive(s)
			return
		}
		arg := C.CString(text)
		defer C.free(unsafe.Pointer(arg))
		C.gtk_scintilla_append_text(s.self(), arg, C.gintptr(len(text)))
		runtime.KeepAlive(s)
	}
}

func (s *Scintilla) ClearText() {
	C.gtk_scintilla_clear_text(s.self())
	runtime.KeepAlive(s)
}

func (s *Scintilla) ClearUndoRedo() {
	C.gtk_scintilla_clear_undo_redo(s.self())
	runtime.KeepAlive(s)
}

func (s *Scintilla) SelectRange(start, end int) {
	C.gtk_scintilla_select_range(s.self(), C.gintptr(start), C.gintptr(end))
	runtime.KeepAlive(s)
}

func (s *Scintilla) ScrollToLine(line, column int) {
	C.gtk_scintilla_scroll_to_line(s.self(), C.gintptr(line), C.gintptr(column))
	runtime.KeepAlive(s)
}

func (s *Scintilla) ScrollToPos(pos int) {
	C.gtk_scintilla_scroll_to_pos(s.self(), C.gintptr(pos))
	runtime.KeepAlive(s)
}

func (s *Scintilla) ResetSearch() {
	C.gtk_scintilla_reset_search(s.self())
	runtime.KeepAlive(s)
}

func (s *Scintilla) SearchPrev(text string, matchCase, wholeWord bool) int {
	str := C.CString(text)
	defer C.free(unsafe.Pointer(str))
	pos := C.gtk_scintilla_search_prev(s.self(), str, C.gintptr(len(text)), s.boolean(matchCase), s.boolean(wholeWord))
	runtime.KeepAlive(s)
	return int(pos)
}

func (s *Scintilla) SearchNext(text string, matchCase, wholeWord bool) int {
	str := C.CString(text)
	defer C.free(unsafe.Pointer(str))
	pos := C.gtk_scintilla_search_next(s.self(), str, C.gintptr(len(text)), s.boolean(matchCase), s.boolean(wholeWord))
	runtime.KeepAlive(s)
	return int(pos)
}

func (s *Scintilla) self() *C.GtkScintilla {
	return (*C.GtkScintilla)(unsafe.Pointer(coreglib.InternObject(s).Native()))
}

func (s *Scintilla) boolean(b bool) C.gboolean {
	if b {
		return C.TRUE
	}
	return C.FALSE
}

type ScintillaOverrides struct {
}

func defaultScintillaOverrides(*Scintilla) ScintillaOverrides {
	return ScintillaOverrides{}
}

type ScintillaClass struct {
	*scintillaClass
}

func initScintillaClass(gclass unsafe.Pointer, overrides ScintillaOverrides, classInitFunc func(*ScintillaClass)) {
	if classInitFunc != nil {
		class := (*ScintillaClass)(gextras.NewStructNative(gclass))
		classInitFunc(class)
	}
}

func wrapScintilla(obj *coreglib.Object) *Scintilla {
	return &Scintilla{
		Widget: gtk.Widget{
			InitiallyUnowned: coreglib.InitiallyUnowned{
				Object: obj,
			},
			Object: obj,
			Accessible: gtk.Accessible{
				Object: obj,
			},
			Buildable: gtk.Buildable{
				Object: obj,
			},
			ConstraintTarget: gtk.ConstraintTarget{
				Object: obj,
			},
		},
	}
}

func marshalScintilla(p uintptr) (any, error) {
	return wrapScintilla(coreglib.ValueFromNative(unsafe.Pointer(p)).Object()), nil
}

type scintillaClass struct {
	native *C.GtkScintillaClass
}

func (s *scintillaClass) ParentClass() *gtk.WidgetClass {
	return (*gtk.WidgetClass)(gextras.NewStructNative(unsafe.Pointer(s.native)))
}
