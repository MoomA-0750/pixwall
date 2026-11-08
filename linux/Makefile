# Linux 版のビルド。make で build/ に作る。make test で core と Linux の部分のテスト
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

PROTOCOLS := wlr-layer-shell-unstable-v1 viewporter fractional-scale-v1 xdg-shell
PROTO_C := $(PROTOCOLS:%=$(BUILD)/gen/%-protocol.c)
PROTO_O := $(PROTOCOLS:%=$(BUILD)/gen/%-protocol.o)
PROTO_H := $(PROTOCOLS:%=$(BUILD)/gen/%-client-protocol.h)

all: $(BUILD)/pixwallctl $(BUILD)/pixwall $(BUILD)/pixwall-settings

$(BUILD)/gen/%-protocol.c: protocols/%.xml
	@mkdir -p $(BUILD)/gen
	wayland-scanner private-code $< $@

$(BUILD)/gen/%-protocol.o: $(BUILD)/gen/%-protocol.c
	$(CC) -O2 -c -o $@ $<

$(BUILD)/gen/%-client-protocol.h: protocols/%.xml
	@mkdir -p $(BUILD)/gen
	wayland-scanner client-header $< $@

$(BUILD)/pixwallctl: src/ctl.cpp src/still.cpp src/gnome_wallpaper.cpp $(COMMON) $(CORE) $(wildcard src/*.h ../windows/core/*.h)
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) -o $@ src/ctl.cpp src/still.cpp src/gnome_wallpaper.cpp $(COMMON) $(CORE) $(GTK) $(GIO)

$(BUILD)/pixwall: $(wildcard src/wall/*.cpp src/wall/*.h) $(COMMON) $(CORE) $(PROTO_O) $(PROTO_H)
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) -o $@ $(wildcard src/wall/*.cpp) $(COMMON) $(CORE) $(PROTO_O) $(WAYLAND)

$(BUILD)/pixwall-settings: $(wildcard src/settings/*.cpp src/settings/*.h) src/still.cpp $(COMMON) $(CORE)
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) -o $@ $(wildcard src/settings/*.cpp) src/still.cpp $(COMMON) $(CORE) $(ADW) $(GIO)

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
	gjs -m gnome-extension/tests/run.js

clean:
	rm -rf $(BUILD)

.PHONY: all test clean
.SECONDARY:
