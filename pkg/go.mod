module github.com/xuges/gtk4scintilla/pkg

go 1.20

replace (
	github.com/diamondburned/gotk4-adwaita/pkg => github.com/xugtek/gotk4-adwaita/pkg v0.0.0
	github.com/diamondburned/gotk4/pkg => github.com/xugtek/gotk4/pkg v0.0.0
)

require github.com/diamondburned/gotk4/pkg v0.3.1

require (
	github.com/KarpelesLab/weak v0.1.1 // indirect
	go4.org/unsafe/assume-no-moving-gc v0.0.0-20231121144256-b99613f794b6 // indirect
	golang.org/x/sync v0.9.0 // indirect
)
