# Gtk4Scintilla Cross-Platform Makefile
# Supports Linux, macOS, and Windows (MSYS2 + MinGW32)

# Default prefix for installation
PREFIX ?= /usr/local

# Default compilers
CC ?= gcc
CXX ?= g++

# Default flags
CFLAGS ?= -fPIC -DGTK -DPLAT_GTK
CXXFLAGS ?= -fPIC -DGTK -DPLAT_GTK

# Platform detection
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Linux)
    PLATFORM := linux
    LIB_EXT := .so
    SHARED_LIB_FLAGS := -shared -Wl,-soname,libgtk4scintilla.so
    
    # Detect Linux distribution type and set appropriate lib and pkgconfig directories
    # Check for RPM-based systems (RedHat, CentOS, Fedora)
    ifeq ($(shell test -d /etc/redhat-release -o -d /etc/fedora-release && echo 1),1)
        # RPM-based systems (use lib64 for 64-bit)
        LIB_DIR := $(PREFIX)/lib64
        PKGCONFIG_DIR := $(LIB_DIR)/pkgconfig
    else
        # Debian-based systems (Ubuntu, Debian)
        # Debian/Ubuntu 64-bit uses architecture-specific lib directory
		ARCH := $(shell uname -m)
        LIB_DIR := $(PREFIX)/lib/$(ARCH)-linux-gnu
        PKGCONFIG_DIR := $(LIB_DIR)/pkgconfig
    endif
endif
ifeq ($(UNAME_S),Darwin)
    PLATFORM := macos
    LIB_EXT := .dylib
    SHARED_LIB_FLAGS := -dynamiclib -Wl,-install_name,$(PREFIX)/lib/libgtk4scintilla.dylib
    LIB_DIR := $(PREFIX)/lib
    PKGCONFIG_DIR := $(LIB_DIR)/pkgconfig
endif
ifneq (,$(findstring MINGW,$(UNAME_S)))
    PLATFORM := windows
    LIB_EXT := .dll
    SHARED_LIB_FLAGS := -shared -Wl,--out-implib,libgtk4scintilla.dll.a
	PREFIX := $(MINGW_PREFIX)
    # On Windows, DLLs go to bin directory, while import libraries go to lib
    BIN_DIR := $(PREFIX)/bin
    LIB_DIR := $(PREFIX)/lib
    PKGCONFIG_DIR := $(LIB_DIR)/pkgconfig
endif

# Default values if not set by platform detection
LIB_DIR ?= $(PREFIX)/lib
PKGCONFIG_DIR ?= $(LIB_DIR)/pkgconfig

# GTK4 flags via pkg-config
GTK4_CFLAGS := $(shell pkg-config --cflags gtk4)
GTK4_LIBS := $(shell pkg-config --libs gtk4)

# Check if pkg-config found GTK4
ifeq ($(GTK4_CFLAGS),)
    $(error "GTK4 not found. Please install GTK4 development packages")
endif

# pkg-config file
PC_FILE := gtk4scintilla.pc

# Generate pkg-config file
$(PC_FILE): $(TARGET)
	@echo "Generating $(PC_FILE)..."
	@echo 'prefix=$(PREFIX)' > $(PC_FILE)
	@echo 'includedir=$${prefix}/include' >> $(PC_FILE)
	@echo 'libdir=$${prefix}/lib' >> $(PC_FILE)
	@echo '' >> $(PC_FILE)
	@echo 'Name: gtk4scintilla' >> $(PC_FILE)
	@echo 'Description: A GTK4 widget for scintilla text editor' >> $(PC_FILE)
	@echo 'Version: 1.0.0' >> $(PC_FILE)
	@echo 'Requires: gtk4' >> $(PC_FILE)
	@echo 'Libs: -L$${libdir} -lgtk4scintilla' >> $(PC_FILE)
	@echo 'Cflags: -I$${includedir}/gtk4scintilla' >> $(PC_FILE)

# Source directories
ROOT_DIR := .
INCLUDE_DIR := include
SRC_DIR := src
SCINTILLA_DIR := scintilla

# Find all source files recursively (MSYS2 compatible)
C_SOURCES := $(shell find $(SRC_DIR) $(SCINTILLA_DIR) -name "*.c" 2>/dev/null || echo "")
CXX_SOURCES := $(shell find $(SRC_DIR) $(SCINTILLA_DIR) -name "*.cxx" 2>/dev/null || echo "")

# All sources
ALL_SOURCES := $(C_SOURCES) $(CXX_SOURCES)

# Check if we found any sources
ifeq ($(ALL_SOURCES),)
    $(warning "No source files found! Please check the directory structure.")
endif

# Object files directory
BUILD_DIR := build
OBJECTS := $(ALL_SOURCES:%=$(BUILD_DIR)/%.o)

# Target library name
TARGET := libgtk4scintilla$(LIB_EXT)

# Include directories
INCLUDES := -I$(ROOT_DIR) \
	-I$(SRC_DIR) \
	-I$(INCLUDE_DIR) \
	-I$(SCINTILLA_DIR) \
	-I$(SCINTILLA_DIR)/include \
	-I$(SCINTILLA_DIR)/src \
	-I$(SCINTILLA_DIR)/lexlib \
	-I$(SCINTILLA_DIR)/gtk4 \
	$(GTK4_CFLAGS)

# Compiler flags
ALL_CFLAGS := $(CFLAGS) $(INCLUDES)
ALL_CXXFLAGS := $(CXXFLAGS) $(INCLUDES) -std=c++17

# Linker flags
LDFLAGS += $(GTK4_LIBS)

# Default target
.PHONY: all
all: $(TARGET)

# Build the shared library
$(TARGET): $(OBJECTS)
	@echo "Building $(TARGET) for $(PLATFORM)..."
	$(CXX) $(SHARED_LIB_FLAGS) -o $@ $^ $(LDFLAGS)

# Create build directory
$(BUILD_DIR):
	@mkdir -p $(BUILD_DIR)

# Function to create directory path
define make_dir
	@mkdir -p $(dir $(1))
endef

# Compile C source files
$(BUILD_DIR)/%.c.o: %.c | $(BUILD_DIR)
	$(call make_dir,$@)
	@echo "Compiling C: $<..."
	$(CC) $(ALL_CFLAGS) -c $< -o $@

# Compile C++ source files
$(BUILD_DIR)/%.cxx.o: %.cxx | $(BUILD_DIR)
	$(call make_dir,$@)
	@echo "Compiling C++: $<..."
	$(CXX) $(ALL_CXXFLAGS) -c $< -o $@

# Compile C++ source files (.cpp extension)
$(BUILD_DIR)/%.cpp.o: %.cpp | $(BUILD_DIR)
	$(call make_dir,$@)
	@echo "Compiling C++: $<..."
	$(CXX) $(ALL_CXXFLAGS) -c $< -o $@

# Install target
.PHONY: install
install: $(TARGET) $(PC_FILE)
	@echo "Installing $(TARGET) to $(PREFIX)..."
ifeq ($(PLATFORM),windows)
	@echo "Windows detected: installing DLL to bin directory"
	@echo "Using binary directory: $(BIN_DIR)"
	@echo "Using library directory: $(LIB_DIR)"
	@echo "Using pkgconfig directory: $(PKGCONFIG_DIR)"
	install -d $(DESTDIR)$(BIN_DIR)
	install -d $(DESTDIR)$(LIB_DIR)
	install -d $(DESTDIR)$(PREFIX)/include/gtk4scintilla
	install -d $(DESTDIR)$(PKGCONFIG_DIR)
	install -m 644 $(TARGET) $(DESTDIR)$(BIN_DIR)/
	@if [ -f libgtk4scintilla.dll.a ]; then install -m 644 libgtk4scintilla.dll.a $(DESTDIR)$(LIB_DIR)/; fi
else
	@echo "Using library directory: $(LIB_DIR)"
	@echo "Using pkgconfig directory: $(PKGCONFIG_DIR)"
	install -d $(DESTDIR)$(LIB_DIR)
	install -d $(DESTDIR)$(PREFIX)/include/gtk4scintilla
	install -d $(DESTDIR)$(PKGCONFIG_DIR)
	install -m 644 $(TARGET) $(DESTDIR)$(LIB_DIR)/
endif
	install -m 644 $(PC_FILE) $(DESTDIR)$(PKGCONFIG_DIR)/

# Uninstall target
.PHONY: uninstall
uninstall:
	@echo "Uninstalling $(TARGET)..."
ifeq ($(PLATFORM),windows)
	@echo "Windows detected: removing DLL from bin directory"
	rm -f $(DESTDIR)$(BIN_DIR)/$(TARGET)
	rm -f $(DESTDIR)$(LIB_DIR)/libgtk4scintilla.dll.a
else
	rm -f $(DESTDIR)$(LIB_DIR)/$(TARGET)
endif
	rm -f $(DESTDIR)$(PREFIX)/include/gtk4scintilla/gtkscintilla.h
	rm -f $(DESTDIR)$(PKGCONFIG_DIR)/$(PC_FILE)

# Clean target
.PHONY: clean
clean:
	@echo "Cleaning build artifacts..."
	rm -rf $(BUILD_DIR) $(TARGET) $(PC_FILE)
	@if [ -f libgtk4scintilla.dll.a ]; then rm -f libgtk4scintilla.dll.a; fi

# Debug build
.PHONY: debug
debug: CFLAGS += -g -DDEBUG
debug: CXXFLAGS += -g -DDEBUG
debug: $(TARGET)

# Release build
.PHONY: release
release: CFLAGS += -O3 -DNDEBUG
release: CXXFLAGS += -O3 -DNDEBUG
release: $(TARGET) $(PC_FILE)

# Show configuration
.PHONY: config
config:
	@echo "Configuration:"
	@echo "  Platform: $(PLATFORM)"
	@echo "  Target: $(TARGET)"
	@echo "  Prefix: $(PREFIX)"
	@echo "  Library Dir: $(LIB_DIR)"
	@echo "  PkgConfig Dir: $(PKGCONFIG_DIR)"
	@echo "  CC: $(CC)"
	@echo "  CXX: $(CXX)"
	@echo "  CFLAGS: $(ALL_CFLAGS)"
	@echo "  CXXFLAGS: $(ALL_CXXFLAGS)"
	@echo "  LDFLAGS: $(LDFLAGS)"
	@echo "  GTK4_CFLAGS: $(GTK4_CFLAGS)"
	@echo "  GTK4_LIBS: $(GTK4_LIBS)"
	@echo "  Source directories:"
	@echo "    SRC: $(SRC_DIR)"
	@echo "    SCINTILLA: $(SCINTILLA_DIR)"
	@echo "  Found sources:"
	@echo "    C files: $(words $(C_SOURCES))"
	@echo "    CXX files: $(words $(CXX_SOURCES))"
	@echo "    Total: $(words $(ALL_SOURCES))"

# Help target
.PHONY: help
help:
	@echo "Available targets:"
	@echo "  all       - Build the library (default)"
	@echo "  debug     - Build with debug symbols"
	@echo "  release   - Build with optimizations"
	@echo "  install   - Install library and headers"
	@echo "  uninstall - Remove installed files"
	@echo "  clean     - Remove build artifacts"
	@echo "  config    - Show build configuration"
	@echo "  help      - Show this help"
	@echo ""
	@echo "Variables that can be overridden:"
	@echo "  PREFIX    - Installation prefix (default: /usr/local)"
	@echo "  CC        - C compiler (default: gcc)"
	@echo "  CXX       - C++ compiler (default: g++)"
	@echo "  CFLAGS    - C compiler flags"
	@echo "  CXXFLAGS  - C++ compiler flags"
	@echo "  LDFLAGS   - Linker flags"
