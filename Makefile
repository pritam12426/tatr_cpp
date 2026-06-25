UNAME_S := $(shell uname -s)

PREFIX ?= /usr/local
MANPREFIX ?= $(PREFIX)/share/man

STRIP ?= strip
PKG_CONFIG ?= pkg-config
INSTALL ?= install

CFLAGS_OPTIMIZATION ?= -O3

BUILD = build
BIN   = tatr

HEADERS   = $(wildcard src/*.hpp)
SRC       = $(wildcard src/*.cpp)


CPPFLAGS += -Isrc -Ithird_party -std=c++20 \
            -DCOMPILED_TIME_PREFIX='"$(PREFIX)"' \
            -DLOG_SHOW_TIME_STAMP

# CPPFLAGS +=  -Wall -Wextra -Wpedantic \
#              -Wstrict-prototypes -Wmissing-prototypes \
#              -Wshadow -Wconversion \
#              -Wno-missing-field-initializers


# convert targets to flags for backwards compatibility
O_DEBUG := 0  # debug binary

ifneq ($(filter debug,$(MAKECMDGOALS)),)
	O_DEBUG := 1
endif

ifeq ($(strip $(O_DEBUG)),1)
	CPPFLAGS += -g3 -DDEBUG -DLOG_SHOW_SOURCE_LOCATION
else
	CPPFLAGS += $(CFLAGS_OPTIMIZATION)
endif

# Check if the OS is macOS
ifeq ($(UNAME_S),Darwin)
    # LDLIBS += -largp
else # Else every thing is linux
    CPPFLAGS += -D_GNU_SOURCE
endif


# ── Third-party libs ─────────────────────────────────────────────────────────────────
ifeq ($(shell $(PKG_CONFIG) --exists zlib && echo 1),1)
	CPPFLAGS  += $(shell $(PKG_CONFIG) --cflags zlib)
	LDFLAGS += $(shell $(PKG_CONFIG) --libs   zlib)
else
    $(error "zlib not found. Install it via your package manager.")
endif

ifeq ($(shell $(PKG_CONFIG) --exists openssl && echo 1),1)
	CPPFLAGS  += $(shell $(PKG_CONFIG) --cflags openssl)
	LDFLAGS += $(shell $(PKG_CONFIG) --libs   openssl)
else
    $(error "openssl not found. Install it via your package manager.")
endif

ifeq ($(shell $(PKG_CONFIG) --exists libcrypto && echo 1),1)
	CPPFLAGS  += $(shell $(PKG_CONFIG) --cflags libcrypto)
	LDFLAGS += $(shell $(PKG_CONFIG) --libs   libcrypto)
else
    $(error "libcrypto not found. Install it via your package manager.")
endif

OUT := $(patsubst %.cpp,$(BUILD)/%.o,$(filter %.cpp,$(SRC)))

all: $(BIN)

help: ## Show this help
	@grep -E '^[a-zA-Z_-]+:.*?## .*$$' $(MAKEFILE_LIST) | \
	awk 'BEGIN {FS = ":.*?## "}; {printf "\033[33m%-20s\033[0m %s\n", $$1, $$2}'

$(BUILD): ## Create build directories automatically
	mkdir -p $(BUILD)

$(BUILD)/%.o: %.cpp $(SHARED_HDR) $(DAEMON_HDR)
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) -c $< -o $@

$(BIN): $(OUT) $(HEADERS) ## Build the tatr binary
	$(CXX) $(LDFLAGS) -o $@ $(OUT) $(LDLIBS)

debug: $(BIN) ## Build the debug binary run `make debug -B O_DEBUG=1`

install: all ## Install the tatr binary
	$(INSTALL) -m 0755 -d $(DESTDIR)$(PREFIX)/bin
	$(INSTALL) -m 0755 $(BIN) $(DESTDIR)$(PREFIX)/bin

clean: ## Clean up tatr artifacts
	$(RM) $(OUT) $(BIN)

uninstall: ## Uninstall the tatr binary
	$(RM) $(DESTDIR)$(PREFIX)/bin/$(BIN)

strip: $(BIN) ## Strip the tatr binary
	$(STRIP) $^

.PHONY: all install uninstall strip clean debug
