# losles GNU Make build.
#
# Common commands:
#   make
#   make test
#   make run ARGS=/path/to/photo.jpg
#   make DESTDIR=/tmp/losles-stage install
#   make clean

.DEFAULT_GOAL := all
.DELETE_ON_ERROR:

CC ?= cc
PKG_CONFIG ?= pkg-config
INSTALL ?= install
WINDRES ?= windres

BUILD_DIR ?= build
SANITIZE ?=
VERSION_SCRIPT := tools/version.sh
TARGET_TRIPLE := $(shell $(CC) -dumpmachine 2>/dev/null)
WINDOWS_BUILD := $(if $(findstring mingw,$(TARGET_TRIPLE)),1,)

ifeq ($(origin VERSION), undefined)
VERSION := $(shell $(VERSION_SCRIPT))
endif

ifeq ($(strip $(VERSION)),)
$(error Could not determine the losles version; set VERSION explicitly)
endif

prefix ?= /usr/local
bindir ?= $(prefix)/bin
datadir ?= $(prefix)/share
applicationsdir ?= $(datadir)/applications
metainfodir ?= $(datadir)/metainfo
icondir ?= $(datadir)/icons/hicolor/512x512/apps
localedir ?= $(datadir)/locale
mandir ?= $(datadir)/man
man1dir ?= $(mandir)/man1

APPLICATION_ID := io.github.develancer.losles
APPLICATION_ICON := \
	data/icons/hicolor/512x512/apps/$(APPLICATION_ID).png
WINDOWS_APPLICATION_ICON := \
	data/icons/windows/$(APPLICATION_ID).ico
WINDOWS_RESOURCE := packaging/windows/losles.rc
DESKTOP_FILE := data/$(APPLICATION_ID).desktop
METAINFO_FILE := data/$(APPLICATION_ID).metainfo.xml
MANPAGE := data/losles.1
VERSION_HEADER := $(BUILD_DIR)/generated/losles-version.h
SOURCE_ICON_FILE ?= $(abspath $(APPLICATION_ICON))

BASE_PACKAGES := gtk4 lcms2 libjpeg libturbojpeg libpng
ifeq ($(WINDOWS_BUILD),1)
PACKAGES := $(BASE_PACKAGES)
PLATFORM_SOURCE := src/losles-platform-win32.c
APP_SUFFIX := .exe
APP_LDFLAGS := -mwindows
PLATFORM_CPPFLAGS := -D_WIN32_WINNT=0x0a00 -DWINVER=0x0a00
PLATFORM_LIBS := -lole32 -lshell32 -luuid -lgdi32
APP_RESOURCE_OBJECT := \
	$(BUILD_DIR)/packaging/windows/losles-resource.o
else
PACKAGES := $(BASE_PACKAGES) colord
PLATFORM_SOURCE := src/losles-platform-linux.c
APP_SUFFIX :=
APP_LDFLAGS :=
PLATFORM_CPPFLAGS :=
PLATFORM_LIBS :=
APP_RESOURCE_OBJECT :=
endif
PKG_CFLAGS := $(shell $(PKG_CONFIG) --cflags $(PACKAGES) 2>/dev/null)
PKG_LIBS := $(shell $(PKG_CONFIG) --libs $(PACKAGES) 2>/dev/null)

CFLAGS ?= -O2 -g
override CFLAGS += -std=c17 -Wall -Wextra -Wformat=2 -Wno-pedantic
override CPPFLAGS += \
	-Isrc \
	-I$(BUILD_DIR)/generated \
	-DLOCALEDIR=\"$(localedir)\" \
	-DLOSLES_SOURCE_ICON_FILE=\"$(SOURCE_ICON_FILE)\" \
	-DLOSLES_INSTALLED_ICON_FILE=\"$(icondir)/$(APPLICATION_ID).png\" \
	-DG_LOG_DOMAIN=\"losles\" \
	-DG_LOG_USE_STRUCTURED=1 \
	$(PLATFORM_CPPFLAGS)

ifneq ($(strip $(SANITIZE)),)
override CFLAGS += -fsanitize=$(SANITIZE) -fno-omit-frame-pointer
override LDFLAGS += -fsanitize=$(SANITIZE)
endif

COMMON_SOURCES := \
	src/losles-color-manager.c \
	src/losles-image.c \
	$(PLATFORM_SOURCE) \
	src/losles-rendered-image.c \
	src/formats/losles-format.c \
	src/formats/losles-format-registry.c \
	src/formats/losles-jpeg-format.c \
	src/formats/losles-jpeg-metadata.c \
	src/formats/losles-png-format.c

APP_SOURCES := \
	src/main.c \
	src/losles-application.c \
	src/losles-cache-policy.c \
	src/losles-window.c \
	$(COMMON_SOURCES)

APP_OBJECTS := \
	$(patsubst %.c,$(BUILD_DIR)/%.o,$(APP_SOURCES)) \
	$(APP_RESOURCE_OBJECT)
COMMON_OBJECTS := $(patsubst %.c,$(BUILD_DIR)/%.o,$(COMMON_SOURCES))

JPEG_METADATA_TEST_OBJECTS := \
	$(BUILD_DIR)/tests/test-jpeg-metadata.o \
	$(BUILD_DIR)/src/formats/losles-jpeg-metadata.o

FORMAT_TEST_OBJECTS := \
	$(BUILD_DIR)/tests/test-formats.o \
	$(COMMON_OBJECTS)

CACHE_POLICY_TEST_OBJECTS := \
	$(BUILD_DIR)/tests/test-cache-policy.o \
	$(BUILD_DIR)/src/losles-cache-policy.o

ALL_OBJECTS := $(sort \
	$(APP_OBJECTS) \
	$(CACHE_POLICY_TEST_OBJECTS) \
	$(JPEG_METADATA_TEST_OBJECTS) \
	$(FORMAT_TEST_OBJECTS))
DEPENDENCY_FILES := $(ALL_OBJECTS:.o=.d)

APP := $(BUILD_DIR)/losles$(APP_SUFFIX)
JPEG_METADATA_TEST := $(BUILD_DIR)/tests/test-jpeg-metadata$(APP_SUFFIX)
FORMAT_TEST := $(BUILD_DIR)/tests/test-formats$(APP_SUFFIX)
CACHE_POLICY_TEST := $(BUILD_DIR)/tests/test-cache-policy$(APP_SUFFIX)
TEST_PROGRAMS := \
	$(JPEG_METADATA_TEST) \
	$(FORMAT_TEST) \
	$(CACHE_POLICY_TEST)

.PHONY: all check check-deps clean FORCE help install print-version run test

all: $(APP) $(APPLICATION_ICON)

check: test

check-deps:
	@$(PKG_CONFIG) --print-errors --exists $(PACKAGES)

$(ALL_OBJECTS): | check-deps $(VERSION_HEADER)

$(VERSION_HEADER): FORCE $(VERSION_SCRIPT)
	@mkdir -p $(dir $@)
	@{ \
	  printf '%s\n' '#pragma once' ''; \
	  printf '#define LOSLES_VERSION "%s"\n' "$(VERSION)"; \
	} > "$@.tmp"
	@if ! cmp -s "$@.tmp" "$@" 2>/dev/null; then \
	  mv "$@.tmp" "$@"; \
	else \
	  $(RM) "$@.tmp"; \
	fi

FORCE:

$(BUILD_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(PKG_CFLAGS) $(CFLAGS) -MMD -MP -c $< -o $@

ifeq ($(WINDOWS_BUILD),1)
$(APP_RESOURCE_OBJECT): $(WINDOWS_RESOURCE) $(WINDOWS_APPLICATION_ICON)
	@mkdir -p $(dir $@)
	$(WINDRES) -I. -i $(WINDOWS_RESOURCE) -o $@
endif

$(APP): $(APP_OBJECTS)
	@mkdir -p $(dir $@)
	$(CC) $(LDFLAGS) $(APP_LDFLAGS) -o $@ $^ \
		$(PKG_LIBS) $(PLATFORM_LIBS) $(LDLIBS)

$(JPEG_METADATA_TEST): $(JPEG_METADATA_TEST_OBJECTS)
	@mkdir -p $(dir $@)
	$(CC) $(LDFLAGS) -o $@ $^ $(PKG_LIBS) $(LDLIBS)

$(FORMAT_TEST): $(FORMAT_TEST_OBJECTS)
	@mkdir -p $(dir $@)
	$(CC) $(LDFLAGS) -o $@ $^ \
		$(PKG_LIBS) $(PLATFORM_LIBS) $(LDLIBS)

$(CACHE_POLICY_TEST): $(CACHE_POLICY_TEST_OBJECTS)
	@mkdir -p $(dir $@)
	$(CC) $(LDFLAGS) -o $@ $^ $(PKG_LIBS) $(LDLIBS)

ifeq ($(WINDOWS_BUILD),1)
test: $(TEST_PROGRAMS)
	$(JPEG_METADATA_TEST)
	$(FORMAT_TEST)
	$(CACHE_POLICY_TEST)
else
test: $(TEST_PROGRAMS)
	$(JPEG_METADATA_TEST)
	@test_home="$$(mktemp -d \
	  "$${TMPDIR:-/tmp}/losles-format-test-home.XXXXXX")" || exit 1; \
	trap '$(RM) -r -- "$$test_home"' EXIT HUP INT TERM; \
	HOME="$$test_home" TMPDIR="$$test_home" $(FORMAT_TEST)
	$(CACHE_POLICY_TEST)
endif

run: $(APP)
	$(APP) $(ARGS)

print-version:
	@printf '%s\n' "$(VERSION)"

install: $(APP) $(APPLICATION_ICON)
	$(INSTALL) -d "$(DESTDIR)$(bindir)"
	$(INSTALL) -m 0755 \
		"$(APP)" \
		"$(DESTDIR)$(bindir)/losles$(APP_SUFFIX)"
	$(INSTALL) -d "$(DESTDIR)$(applicationsdir)"
	$(INSTALL) -m 0644 \
		"$(DESKTOP_FILE)" \
		"$(DESTDIR)$(applicationsdir)/$(APPLICATION_ID).desktop"
	$(INSTALL) -d "$(DESTDIR)$(metainfodir)"
	$(INSTALL) -m 0644 \
		"$(METAINFO_FILE)" \
		"$(DESTDIR)$(metainfodir)/$(APPLICATION_ID).metainfo.xml"
	$(INSTALL) -d "$(DESTDIR)$(icondir)"
	$(INSTALL) -m 0644 \
		"$(APPLICATION_ICON)" \
		"$(DESTDIR)$(icondir)/$(APPLICATION_ID).png"
	$(INSTALL) -d "$(DESTDIR)$(man1dir)"
	$(INSTALL) -m 0644 "$(MANPAGE)" "$(DESTDIR)$(man1dir)/losles.1"

clean:
	@build_path="$(abspath $(BUILD_DIR))"; \
	case "$$build_path" in \
	  "$(CURDIR)"/*) ;; \
	  *) echo "Refusing to remove BUILD_DIR outside the project: $$build_path"; \
	     exit 1 ;; \
	esac; \
	$(RM) -r -- "$$build_path"

help:
	@echo "losles GNU Make targets:"
	@echo "  all       Build $(APP) (default)"
	@echo "  test      Build and run all test programs"
	@echo "  check     Alias for test"
	@echo "  print-version"
	@echo "            Print the version derived from Git"
	@echo "  run       Run the viewer; pass a file with ARGS=/path/image.jpg"
	@echo "  install   Install under prefix (default: $(prefix)); honors DESTDIR"
	@echo "  clean     Remove BUILD_DIR (default: $(BUILD_DIR))"
	@echo
	@echo "Useful overrides:"
	@echo "  BUILD_DIR=build-name"
	@echo "  CFLAGS='-O0 -g3'"
	@echo "  SANITIZE=address,undefined"
	@echo "  VERSION=2026.07.1 (for builds without Git metadata)"
	@echo "  WINDOWS_BUILD=1 (normally auto-detected from the compiler)"
	@echo "  prefix=/usr"

-include $(DEPENDENCY_FILES)
