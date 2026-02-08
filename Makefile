# ─────────────────────────────────────────────────────────────────
# Makefile — Quick-build alternative to CMake
#
# Usage:
#   make              Build the plugin
#   make install      Install to ~/.purple/plugins/
#   make clean        Clean build artifacts
#   make system-install  Install system-wide (needs sudo)
# ─────────────────────────────────────────────────────────────────

PLUGIN_NAME = libwhatsmeow-lite.so
BUILD_DIR   = build

# Compiler and flags
CC      = gcc
CFLAGS  = -fPIC -shared -Wall -Wextra -O2
CFLAGS += $(shell pkg-config --cflags purple glib-2.0)
LDFLAGS = $(shell pkg-config --libs purple glib-2.0) -lpthread -lm -lresolv

# Go settings
GO          = go
GO_SRC_DIR  = src/go
GO_ARCHIVE  = $(BUILD_DIR)/libwhatsmeow-bridge.a

# Paths
PURPLE_PLUGIN_DIR_USER   = $(HOME)/.purple/plugins
PURPLE_PLUGIN_DIR_SYSTEM = $(shell pkg-config --variable=plugindir purple)

# Optional: GDK pixbuf
PIXBUF_CFLAGS = $(shell pkg-config --cflags gdk-pixbuf-2.0 2>/dev/null)
PIXBUF_LIBS   = $(shell pkg-config --libs gdk-pixbuf-2.0 2>/dev/null)
ifneq ($(PIXBUF_CFLAGS),)
    CFLAGS  += $(PIXBUF_CFLAGS) -DHAVE_GDK_PIXBUF
    LDFLAGS += $(PIXBUF_LIBS)
endif

.PHONY: all clean install system-install

all: $(BUILD_DIR)/$(PLUGIN_NAME)

# Step 1: Build Go code as a C static archive
$(GO_ARCHIVE): $(GO_SRC_DIR)/whatsmeow_bridge.go $(GO_SRC_DIR)/bridge.h $(GO_SRC_DIR)/go.mod
	@mkdir -p $(BUILD_DIR)
	@echo "─── Building Go whatsmeow bridge ───"
	cd $(GO_SRC_DIR) && CGO_ENABLED=1 $(GO) build \
		-buildmode=c-archive \
		-o ../../$(GO_ARCHIVE) \
		.
	@echo "─── Go bridge built ✓ ───"

# Step 2: Build the shared library linking C plugin + Go archive
$(BUILD_DIR)/$(PLUGIN_NAME): src/c/plugin.c $(GO_ARCHIVE)
	@echo "─── Building libpurple plugin ───"
	$(CC) $(CFLAGS) \
		-I$(GO_SRC_DIR) \
		-I$(BUILD_DIR) \
		src/c/plugin.c \
		$(GO_ARCHIVE) \
		$(LDFLAGS) \
		-o $(BUILD_DIR)/$(PLUGIN_NAME)
	@echo "─── Plugin built: $(BUILD_DIR)/$(PLUGIN_NAME) ✓ ───"
	@ls -lh $(BUILD_DIR)/$(PLUGIN_NAME)

install: $(BUILD_DIR)/$(PLUGIN_NAME)
	@mkdir -p $(PURPLE_PLUGIN_DIR_USER)
	cp $(BUILD_DIR)/$(PLUGIN_NAME) $(PURPLE_PLUGIN_DIR_USER)/
	@echo "Installed to $(PURPLE_PLUGIN_DIR_USER)/$(PLUGIN_NAME)"
	@echo ""
	@echo "  Next: Restart Pidgin, then Accounts → Add → Protocol: WhatsApp (whatsmeow)"
	@echo "  Username: <country_code><phone>@s.whatsapp.net"

system-install: $(BUILD_DIR)/$(PLUGIN_NAME)
	sudo install -m 644 $(BUILD_DIR)/$(PLUGIN_NAME) $(PURPLE_PLUGIN_DIR_SYSTEM)/
	@echo "Installed to $(PURPLE_PLUGIN_DIR_SYSTEM)/$(PLUGIN_NAME)"

clean:
	rm -rf $(BUILD_DIR)
	cd $(GO_SRC_DIR) && $(GO) clean -cache 2>/dev/null || true
