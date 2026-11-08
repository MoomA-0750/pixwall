# Linux 版のビルド。make で build/ に作る。make test で core と Linux の部分のテスト
# make qt で QML 版の設定画面（pixwall-settings-qt）、make test-qt でそのテスト（Qt 6 の開発用ファイルが要る）
CXX ?= g++
CXXFLAGS ?= -O2 -g
CXXFLAGS += -std=c++20 -Wall -Wextra -I../windows/core -Isrc -Ibuild/gen
BUILD := build

CORE := ../windows/core/core.cpp ../windows/core/config.cpp ../windows/core/cli.cpp
COMMON := src/config_file.cpp src/omarchy.cpp src/image_loader.cpp src/bus.cpp

GIO := $(shell pkg-config --cflags --libs gio-2.0 gdk-pixbuf-2.0)
GTK := $(shell pkg-config --cflags --libs gtk4)
ADW := $(shell pkg-config --cflags --libs gtk4 libadwaita-1)
WAYLAND := $(shell pkg-config --cflags --libs wayland-client wayland-egl egl glesv2 gio-2.0 gdk-pixbuf-2.0)
# Qt は使うときだけ訊く（Qt の無い機械でも make・make test が通るように）
QT_MODULES := Qt6Quick Qt6QuickControls2 Qt6Qml Qt6Gui Qt6Core
QT = $(shell pkg-config --cflags --libs $(QT_MODULES))
QT_LIBEXEC = $(shell qmake6 -query QT_HOST_LIBEXECS 2>/dev/null || pkg-config --variable=libexecdir Qt6Core)
MOC = $(QT_LIBEXEC)/moc
RCC = $(QT_LIBEXEC)/rcc

PROTOCOLS := wlr-layer-shell-unstable-v1 viewporter fractional-scale-v1 xdg-shell
PROTO_C := $(PROTOCOLS:%=$(BUILD)/gen/%-protocol.c)
PROTO_O := $(PROTOCOLS:%=$(BUILD)/gen/%-protocol.o)
PROTO_H := $(PROTOCOLS:%=$(BUILD)/gen/%-client-protocol.h)

all: $(BUILD)/pixwallctl $(BUILD)/pixwall $(BUILD)/pixwall-settings $(BUILD)/pixwall-settings-gtk
qt: $(BUILD)/pixwall-settings-qt

$(BUILD)/gen/%-protocol.c: protocols/%.xml
	@mkdir -p $(BUILD)/gen
	wayland-scanner private-code $< $@

$(BUILD)/gen/%-protocol.o: $(BUILD)/gen/%-protocol.c
	$(CC) -O2 -c -o $@ $<

$(BUILD)/gen/%-client-protocol.h: protocols/%.xml
	@mkdir -p $(BUILD)/gen
	wayland-scanner client-header $< $@

$(BUILD)/pixwallctl: src/ctl.cpp src/still.cpp src/monitors_gtk.cpp src/gnome_wallpaper.cpp $(COMMON) $(CORE) $(wildcard src/*.h ../windows/core/*.h)
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) -o $@ src/ctl.cpp src/still.cpp src/monitors_gtk.cpp src/gnome_wallpaper.cpp $(COMMON) $(CORE) $(GTK) $(GIO)

$(BUILD)/pixwall: $(wildcard src/wall/*.cpp src/wall/*.h) $(COMMON) $(CORE) $(PROTO_O) $(PROTO_H)
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) -o $@ $(wildcard src/wall/*.cpp) $(COMMON) $(CORE) $(PROTO_O) $(WAYLAND)

# 設定画面の選び役（シェル）。GTK 版か QML 版に置き換わる
$(BUILD)/pixwall-settings: src/settings/select.sh
	@mkdir -p $(BUILD)
	install -m 755 $< $@

# GTK 版の設定画面（GTK4・libadwaita）
$(BUILD)/pixwall-settings-gtk: src/settings/gtk/main.cpp src/settings/strings.h src/still.cpp src/monitors_gtk.cpp $(COMMON) $(CORE)
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) -o $@ src/settings/gtk/main.cpp src/still.cpp src/monitors_gtk.cpp $(COMMON) $(CORE) $(ADW) $(GIO)

# QML 版の設定画面（Qt Quick・Material）。GTK はリンクしない（GLib・GIO・gdk-pixbuf は中身が使う）
QT_SRC := src/settings/qt/main.cpp src/settings/qt/backend.cpp src/settings/qt/theme.cpp src/settings/qt/monitors.cpp
QT_MOC := $(BUILD)/gen/moc_backend.cpp $(BUILD)/gen/moc_theme.cpp
QT_QML := $(wildcard src/settings/qt/qml/*.qml)
QT_FLAGS = -fPIC $(QT)

$(BUILD)/gen/moc_%.cpp: src/settings/qt/%.h
	@mkdir -p $(BUILD)/gen
	$(MOC) $< -o $@

$(BUILD)/gen/qrc_settings.cpp: src/settings/qt/settings.qrc $(QT_QML)
	@mkdir -p $(BUILD)/gen
	$(RCC) --name settings $< -o $@

$(BUILD)/pixwall-settings-qt: $(QT_SRC) $(QT_MOC) $(BUILD)/gen/qrc_settings.cpp $(wildcard src/settings/qt/*.h) src/settings/strings.h src/still.cpp $(COMMON) $(CORE)
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc/settings/qt -o $@ $(QT_SRC) $(QT_MOC) $(BUILD)/gen/qrc_settings.cpp src/still.cpp $(COMMON) $(CORE) $(QT_FLAGS) $(GIO)

$(BUILD)/theme_tests: tests/theme_tests.cpp src/settings/qt/theme.cpp $(BUILD)/gen/moc_theme.cpp src/settings/qt/theme.h
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc/settings/qt -o $@ tests/theme_tests.cpp src/settings/qt/theme.cpp $(BUILD)/gen/moc_theme.cpp $(QT_FLAGS)

$(BUILD)/core_tests: ../windows/tests/core_tests.cpp $(CORE)
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) -o $@ ../windows/tests/core_tests.cpp $(CORE)

$(BUILD)/linux_tests: tests/linux_tests.cpp src/wall/hyprland.cpp src/gnome_wallpaper.cpp $(COMMON) $(CORE)
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc/wall -o $@ tests/linux_tests.cpp src/wall/hyprland.cpp src/gnome_wallpaper.cpp $(COMMON) $(CORE) $(GIO)

$(BUILD)/layout_dump: tests/layout_dump.cpp $(CORE)
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) -o $@ tests/layout_dump.cpp $(CORE)

test: $(BUILD)/core_tests $(BUILD)/linux_tests $(BUILD)/layout_dump $(BUILD)/pixwallctl
	$(BUILD)/core_tests
	$(BUILD)/linux_tests
	sh tests/settings_select_test.sh
	gjs -m gnome-extension/tests/run.js

test-qt: $(BUILD)/theme_tests
	QT_QPA_PLATFORM=offscreen $(BUILD)/theme_tests

clean:
	rm -rf $(BUILD)

.PHONY: all qt test test-qt clean
.SECONDARY:
