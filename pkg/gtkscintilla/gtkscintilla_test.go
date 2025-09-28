package gtkscintilla

import (
	"github.com/diamondburned/gotk4/pkg/gtk/v4"
	"testing"
)

func Test_Scintilla(t *testing.T) {
	gtk.Init()
	app := gtk.NewApplication("com.github.xuges.gtk4scintilla.test", 0)
	app.ConnectActivate(func() {
		sci := NewScintilla()
		sci.SetStyle("vscode")
		sci.SetLanguage("json")
		sci.SetLineNumber(true)
		sci.SetIndentGuides(true)
		sci.SetFold(true)
		sci.SetAutoIndent(true)
		sci.SetVExpand(true)
		sci.Connect("text-changed", func() {
			t.Log("text changed")
		})

		win := gtk.NewApplicationWindow(app)
		win.SetChild(sci)
		win.SetDefaultSize(300, 600)
		win.Present()
	})
	app.Run(nil)
}
