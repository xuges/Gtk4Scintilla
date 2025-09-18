# Demo Makefile for Gtk4Scintilla
# Links against the built libgtk4scintilla library

# Default compiler
CC := gcc

# Project paths
ROOT_DIR := ..
INCLUDE_DIR := $(ROOT_DIR)/include
LIB_DIR := $(ROOT_DIR)

# Source files
SOURCES := main.c
TARGET := demo.exe

# GTK4 flags via pkg-config
GTK4_CFLAGS := $(shell pkg-config --cflags gtk4)
GTK4_LIBS := $(shell pkg-config --libs gtk4)

# Compiler flags
CFLAGS := $(GTK4_CFLAGS) -I$(INCLUDE_DIR)

# Linker flags - link against our library
LDFLAGS := -L$(LIB_DIR) -lgtk4scintilla $(GTK4_LIBS)

# Default target
.PHONY: all
all: $(TARGET)

# Build the demo executable
$(TARGET): $(SOURCES)
	@echo "Building demo executable..."
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# Clean target
.PHONY: clean
clean:
	@echo "Cleaning demo build artifacts..."
	rm -f $(TARGET)
	@if [ -f *.exe ]; then rm -f *.exe; fi

# Run target (Windows)
.PHONY: run
run: $(TARGET)
	@echo "Running demo..."
	@if [ -f $(TARGET) ]; then ./$(TARGET); else echo "Demo not built yet"; fi

# Help target
.PHONY: help
help:
	@echo "Available targets:"
	@echo "  all   - Build the demo executable (default)"
	@echo "  clean - Remove demo executable"
	@echo "  run   - Run the demo (Linux/macOS)"
	@echo ""
	@echo "Make sure libgtk4scintilla.dll (Windows) or libgtk4scintilla.so (Linux)"
	@echo "is in the same directory as the demo executable, or in system library path."