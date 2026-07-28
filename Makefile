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

BUILD_DIR ?= build
SANITIZE ?=

prefix ?= /usr/local
bindir ?= $(prefix)/bin
datadir ?= $(prefix)/share
applicationsdir ?= $(datadir)/applications
metainfodir ?= $(datadir)/metainfo
icondir ?= $(datadir)/icons/hicolor/512x512/apps
localedir ?= $(datadir)/locale

APPLICATION_ID := io.github.develancer.Losles
APPLICATION_ICON := \
	data/icons/hicolor/512x512/apps/$(APPLICATION_ID).png
DESKTOP_FILE := data/$(APPLICATION_ID).desktop
METAINFO_FILE := data/$(APPLICATION_ID).metainfo.xml

PACKAGES := gtk4 lcms2 libjpeg libturbojpeg libpng colord
PKG_CFLAGS := $(shell $(PKG_CONFIG) --cflags $(PACKAGES) 2>/dev/null)
PKG_LIBS := $(shell $(PKG_CONFIG) --libs $(PACKAGES) 2>/dev/null)

CFLAGS ?= -O2 -g
override CFLAGS += -std=c17 -Wall -Wextra -Wformat=2 -Wno-pedantic
override CPPFLAGS += \
	-Isrc \
	-DLOCALEDIR=\"$(localedir)\" \
	-DLOSLES_SOURCE_ICON_FILE=\"$(abspath $(APPLICATION_ICON))\" \
	-DLOSLES_INSTALLED_ICON_FILE=\"$(icondir)/$(APPLICATION_ID).png\" \
	-DG_LOG_DOMAIN=\"losles\" \
	-DG_LOG_USE_STRUCTURED=1

ifneq ($(strip $(SANITIZE)),)
override CFLAGS += -fsanitize=$(SANITIZE) -fno-omit-frame-pointer
override LDFLAGS += -fsanitize=$(SANITIZE)
endif

COMMON_SOURCES := \
	src/losles-color-manager.c \
	src/losles-image.c \
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

APP_OBJECTS := $(patsubst %.c,$(BUILD_DIR)/%.o,$(APP_SOURCES))
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

APP := $(BUILD_DIR)/losles
JPEG_METADATA_TEST := $(BUILD_DIR)/tests/test-jpeg-metadata
FORMAT_TEST := $(BUILD_DIR)/tests/test-formats
CACHE_POLICY_TEST := $(BUILD_DIR)/tests/test-cache-policy
TEST_PROGRAMS := \
	$(JPEG_METADATA_TEST) \
	$(FORMAT_TEST) \
	$(CACHE_POLICY_TEST)

.PHONY: all check check-deps clean help install run test

all: $(APP) $(APPLICATION_ICON)

check: test

check-deps:
	@$(PKG_CONFIG) --print-errors --exists $(PACKAGES)

$(ALL_OBJECTS): | check-deps

$(BUILD_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(PKG_CFLAGS) $(CFLAGS) -MMD -MP -c $< -o $@

$(APP): $(APP_OBJECTS)
	@mkdir -p $(dir $@)
	$(CC) $(LDFLAGS) -o $@ $^ $(PKG_LIBS) $(LDLIBS)

$(JPEG_METADATA_TEST): $(JPEG_METADATA_TEST_OBJECTS)
	@mkdir -p $(dir $@)
	$(CC) $(LDFLAGS) -o $@ $^ $(PKG_LIBS) $(LDLIBS)

$(FORMAT_TEST): $(FORMAT_TEST_OBJECTS)
	@mkdir -p $(dir $@)
	$(CC) $(LDFLAGS) -o $@ $^ $(PKG_LIBS) $(LDLIBS)

$(CACHE_POLICY_TEST): $(CACHE_POLICY_TEST_OBJECTS)
	@mkdir -p $(dir $@)
	$(CC) $(LDFLAGS) -o $@ $^ $(PKG_LIBS) $(LDLIBS)

test: $(TEST_PROGRAMS)
	$(JPEG_METADATA_TEST)
	$(FORMAT_TEST)
	$(CACHE_POLICY_TEST)

run: $(APP)
	$(APP) $(ARGS)

install: $(APP) $(APPLICATION_ICON)
	$(INSTALL) -d "$(DESTDIR)$(bindir)"
	$(INSTALL) -m 0755 "$(APP)" "$(DESTDIR)$(bindir)/losles"
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
	@echo "  run       Run the viewer; pass a file with ARGS=/path/image.jpg"
	@echo "  install   Install under prefix (default: $(prefix)); honors DESTDIR"
	@echo "  clean     Remove BUILD_DIR (default: $(BUILD_DIR))"
	@echo
	@echo "Useful overrides:"
	@echo "  BUILD_DIR=build-name"
	@echo "  CFLAGS='-O0 -g3'"
	@echo "  SANITIZE=address,undefined"
	@echo "  prefix=/usr"

-include $(DEPENDENCY_FILES)
