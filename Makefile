# Makefile - Snappy Switcher v1.0
CC = gcc
PKG_CFLAGS = $(shell pkg-config --cflags wayland-client cairo pango pangocairo json-c xkbcommon)
PKG_LIBS = $(shell pkg-config --libs wayland-client wayland-cursor cairo pango pangocairo json-c xkbcommon glib-2.0 gobject-2.0)

# Optional SVG support via librsvg
RSVG_CFLAGS = $(shell pkg-config --cflags librsvg-2.0 2>/dev/null)
RSVG_LIBS = $(shell pkg-config --libs librsvg-2.0 2>/dev/null)
ifneq ($(RSVG_LIBS),)
  RSVG_FLAG = -DHAVE_RSVG
endif

CFLAGS = -Wall -Wextra -g -D_POSIX_C_SOURCE=200809L $(PKG_CFLAGS) $(RSVG_CFLAGS) $(RSVG_FLAG)
LIBS = $(PKG_LIBS) $(RSVG_LIBS) -lm

# Installation paths
PREFIX ?= /usr/local
BINDIR = $(PREFIX)/bin
DATADIR = $(PREFIX)/share/snappy-switcher
DOCDIR = $(PREFIX)/share/doc/snappy-switcher
SYSCONFDIR = /etc/xdg/snappy-switcher

# Source files
SRC = src/main.c src/hyprland.c src/render.c src/input.c src/config.c src/icons.c src/socket.c src/backend.c src/wlr_backend.c
OBJ = $(SRC:.c=.o) src/xdg-shell-protocol.o src/wlr-layer-shell-unstable-v1-protocol.o src/wlr-foreign-toplevel-management-unstable-v1-protocol.o
TARGET = snappy-switcher

# Protocol Paths
WAYLAND_PROTOCOLS_DIR = $(shell pkg-config --variable=pkgdatadir wayland-protocols)
WAYLAND_SCANNER = $(shell pkg-config --variable=wayland_scanner wayland-scanner)
XDG_SHELL_XML = $(WAYLAND_PROTOCOLS_DIR)/stable/xdg-shell/xdg-shell.xml
LAYER_SHELL_XML = protocol/wlr-layer-shell-unstable-v1.xml
FOREIGN_TOPLEVEL_XML = protocol/wlr-foreign-toplevel-management-unstable-v1.xml

all: $(TARGET) protocols

# Compile the Main Program
$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS)

# Protocol generation targets
protocols: src/xdg-shell-client-protocol.h src/wlr-layer-shell-unstable-v1-client-protocol.h src/wlr-foreign-toplevel-management-unstable-v1-client-protocol.h

# Generate XDG Shell Protocol
src/xdg-shell-protocol.c:
	$(WAYLAND_SCANNER) private-code $(XDG_SHELL_XML) $@
src/xdg-shell-client-protocol.h:
	$(WAYLAND_SCANNER) client-header $(XDG_SHELL_XML) $@

# Generate Layer Shell Protocol
src/wlr-layer-shell-unstable-v1-protocol.c:
	$(WAYLAND_SCANNER) private-code $(LAYER_SHELL_XML) $@
src/wlr-layer-shell-unstable-v1-client-protocol.h:
	$(WAYLAND_SCANNER) client-header $(LAYER_SHELL_XML) $@

# Generate Foreign Toplevel Management Protocol (新增)
src/wlr-foreign-toplevel-management-unstable-v1-protocol.c:
	$(WAYLAND_SCANNER) private-code $(FOREIGN_TOPLEVEL_XML) $@
src/wlr-foreign-toplevel-management-unstable-v1-client-protocol.h:
	$(WAYLAND_SCANNER) client-header $(FOREIGN_TOPLEVEL_XML) $@

# Compile C files
src/main.o: src/main.c src/xdg-shell-client-protocol.h src/wlr-layer-shell-unstable-v1-client-protocol.h
	$(CC) $(CFLAGS) -c $< -o $@

# 新增：wlr_backend 需要 foreign toplevel 协议头文件
src/wlr_backend.o: src/wlr_backend.c src/wlr-foreign-toplevel-management-unstable-v1-client-protocol.h
	$(CC) $(CFLAGS) -c $< -o $@

src/%.o: src/%.c
	$(CC) $(CFLAGS) -c $< -o $@

# ═══════════════════════════════════════════════════════════════════════════
# INSTALLATION
# ═══════════════════════════════════════════════════════════════════════════
install: $(TARGET)
	@echo "╔═══════════════════════════════════════════════════════════════╗"
	@echo "║           Installing Snappy Switcher v1.0                     ║"
	@echo "╚═══════════════════════════════════════════════════════════════╝"
	@echo ""
	@echo "Installing binaries to $(BINDIR)..."
	install -d $(BINDIR)
	install -m 755 $(TARGET) $(BINDIR)/$(TARGET)
	install -m 755 scripts/snappy-wrapper.sh $(BINDIR)/snappy-wrapper
	@echo ""
	@echo "Installing themes to $(DATADIR)/themes/..."
	install -d $(DATADIR)/themes
	install -m 644 themes/*.ini $(DATADIR)/themes/
	@echo ""
	@echo "Installing documentation to $(DOCDIR)..."
	install -d $(DOCDIR)
	install -m 644 config.ini.example $(DOCDIR)/
	install -m 644 README.md $(DOCDIR)/ 2>/dev/null || true
	@echo ""
	@echo "Installing system config to $(SYSCONFDIR)..."
	install -d $(SYSCONFDIR)
	install -m 644 config.ini.example $(SYSCONFDIR)/config.ini
	@echo ""
	@echo "Installing helper scripts..."
	install -m 755 scripts/install-config.sh $(BINDIR)/snappy-install-config
	@echo ""
	@echo "╔═══════════════════════════════════════════════════════════════╗"
	@echo "║                   Installation Complete!                      ║"
	@echo "╚═══════════════════════════════════════════════════════════════╝"
	@echo ""
	@echo "NEXT STEPS:"
	@echo ""
	@echo "  1. Setup your config:"
	@echo "     snappy-install-config"
	@echo ""
	@echo "  2. Add to ~/.config/hypr/hyprland.conf:"
	@echo "     exec-once = snappy-wrapper"
	@echo "     bind = ALT, Tab, exec, snappy-switcher next"
	@echo "     bind = ALT SHIFT, Tab, exec, snappy-switcher prev"
	@echo ""
	@echo "  3. (Optional) Choose a theme in ~/.config/snappy-switcher/config.ini"
	@echo "     Available: snappy-slate, catppuccin-mocha, nord, dracula, etc."
	@echo ""

install-user: $(TARGET)
	@echo "Installing to user directory (~/.local)..."
	install -d $(HOME)/.local/bin
	install -m 755 $(TARGET) $(HOME)/.local/bin/$(TARGET)
	install -m 755 scripts/snappy-wrapper.sh $(HOME)/.local/bin/snappy-wrapper
	install -d $(HOME)/.config/snappy-switcher/themes
	install -m 644 themes/*.ini $(HOME)/.config/snappy-switcher/themes/
	@if [ ! -f $(HOME)/.config/snappy-switcher/config.ini ]; then \
		install -m 644 config.ini.example $(HOME)/.config/snappy-switcher/config.ini; \
	fi
	@echo "User installation complete!"

uninstall:
	@echo "Removing Snappy Switcher..."
	rm -f $(BINDIR)/$(TARGET)
	rm -f $(BINDIR)/snappy-wrapper
	rm -f $(BINDIR)/snappy-install-config
	rm -rf $(DATADIR)
	rm -rf $(DOCDIR)
	rm -rf $(SYSCONFDIR)
	@echo "Done! (User config in ~/.config/snappy-switcher was NOT removed)"

clean:
	rm -f src/*.o src/*-protocol.* $(TARGET)

test: $(TARGET)
	@chmod +x scripts/stress-test.sh
	@echo "Running stress test..."
	@./scripts/stress-test.sh

.PHONY: all clean install install-user uninstall test
