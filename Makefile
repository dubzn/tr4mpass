CC       = cc
CFLAGS   = -Wall -Wextra -std=c99 -D_GNU_SOURCE -O2
LDFLAGS  =

# Library discovery via pkg-config (warn but do not fail if missing)
PKG_LIBS = libimobiledevice-1.0 libirecovery-1.0 libusb-1.0 \
           libplist-2.0 openssl libcurl libssh2

CFLAGS  += $(foreach lib,$(PKG_LIBS),$(shell pkg-config --cflags $(lib) 2>/dev/null))
LDFLAGS += $(foreach lib,$(PKG_LIBS),$(shell pkg-config --libs   $(lib) 2>/dev/null))

# Ubuntu 22.04 libplist 2.2.x lacks plist_mem_free and 4-arg plist_from_memory.
PLIST_VER := $(shell pkg-config --modversion libplist-2.0 2>/dev/null)
ifeq ($(shell printf '%s\n' "$(PLIST_VER)" | awk -F. '{ if ($$1<2 || ($$1==2 && $$2<3)) print "legacy" }'),legacy)
CFLAGS += -DTR4MPASS_PLIST_LEGACY -include include/util/plist_compat.h
else
CFLAGS += -include include/util/plist_compat.h
endif

# Ubuntu 22.04 libirecovery 1.0.x lacks have_cpid/have_ecid and send opt enums.
IRECV_VER := $(shell pkg-config --modversion libirecovery-1.0 2>/dev/null)
ifeq ($(shell printf '%s\n' "$(IRECV_VER)" | awk -F. '{ if ($$1<1 || ($$1==1 && $$2==0)) print "legacy" }'),legacy)
CFLAGS += -DTR4MPASS_IRECV_LEGACY
endif

$(foreach lib,$(PKG_LIBS),$(if $(shell pkg-config --exists $(lib) 2>/dev/null && echo ok),,$(warning Library $(lib) not found by pkg-config)))

# Auto-discover all C sources under src/
SRCS     = $(shell find src -name '*.c')
OBJS     = $(SRCS:.c=.o)
TARGET   = tr4mpass

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -Iinclude -c -o $@ $<

clean:
	find src -name '*.o' -delete
	rm -f $(TARGET) $(TEST_TARGET) $(MOCK_TARGET)
	find tests -name '*.o' -delete 2>/dev/null || true

# ------------------------------------------------------------------ #
# Test target: compile only hardware-independent units                #
# No mocks, no real library linkage required for the SUT code.        #
# ------------------------------------------------------------------ #

TEST_UNITS  = src/device/chip_db.c src/bypass/bypass.c src/util/log.c \
              src/util/env_config.c

# Sections linked by both `make test` (hw-independent) and `make test-mocks`.
TEST_SECT_CORE = tests/test_main.c tests/test_parsing.c tests/test_chip_db.c \
                 tests/test_bypass_probe.c tests/test_constants.c \
                 tests/test_cert_extract.c tests/test_signer.c \
                 tests/test_env_config.c

# Sections that require the mocked hardware libraries -- only used by
# `make test-mocks`.  Include stub definitions of their entry points in
# TEST_SECT_STUBS so the hw-independent target still links.
TEST_SECT_MOCK = tests/test_ramdisk.c tests/test_ssh_jailbreak.c

# Aggregate for the mock build: real section bodies.
TEST_SECT     = $(TEST_SECT_CORE) $(TEST_SECT_MOCK)
TEST_TARGET   = tests/run_tests
TEST_CFLAGS   = $(CFLAGS) -Iinclude -Itests -DUNIT_TEST

test: $(TEST_TARGET)
	./$(TEST_TARGET)

# The hw-independent target replaces the mock-only sections with empty
# stubs so run_tests still finds run_ramdisk_tests / run_ssh_jailbreak_tests.
$(TEST_TARGET): $(TEST_SECT_CORE) $(TEST_UNITS) tests/mock_section_stubs.c
	$(CC) $(TEST_CFLAGS) -o $@ $^

# ------------------------------------------------------------------ #
# Mock test target: full suite against mocked hardware libraries.     #
#                                                                    #
# Links the test sections + all tests/mocks/*.c + the production     #
# sources we want to exercise (everything under src/ except main.c   #
# and the hardware-specific checkm8/dfu exploit code).  The real     #
# hardware libs (libimobiledevice/libirecovery/libusb/libssh2/curl)  #
# are NOT linked -- the mocks satisfy those symbols.  libplist,      #
# openssl, and pthread remain real.                                  #
# ------------------------------------------------------------------ #

MOCK_SRCS    = $(shell find tests/mocks -name '*.c')
# Phase 3: pick up end-to-end integration test sources.  `find` is
# guarded so `make` still works before the directory exists.
INT_SRCS      = $(shell find tests/integration -name '*.c' 2>/dev/null)
# Production sources to include in the mock build.  We exclude:
#   - src/main.c (has its own main())
#   - src/exploit/* and src/device/usb_dfu*.c (direct DFU/checkm8 h/w code)
MOCK_SUT_SRCS = $(shell find src -name '*.c' \
                  -not -path 'src/main.c' \
                  -not -path 'src/exploit/*' \
                  -not -path 'src/device/usb_dfu*.c')
MOCK_ALL_SRCS = $(TEST_SECT) $(MOCK_SRCS) $(INT_SRCS) $(MOCK_SUT_SRCS)
MOCK_TARGET   = tests/run_mock_tests

# Include headers for mocked hardware libraries, but only link libs that
# the SUT still needs at runtime and we did NOT mock.
MOCK_HEADER_PKG_LIBS = libimobiledevice-1.0 libirecovery-1.0 libusb-1.0 \
                       libcurl libssh2
MOCK_PKG_LIBS = libplist-2.0 openssl
MOCK_CFLAGS   = -Wall -Wextra -std=c99 -D_GNU_SOURCE -O2 \
                $(foreach lib,$(MOCK_HEADER_PKG_LIBS) $(MOCK_PKG_LIBS),$(shell pkg-config --cflags $(lib) 2>/dev/null)) \
                -Iinclude -Itests -Itests/integration -Itests/mocks \
                -DTEST_MODE -DUNIT_TEST
MOCK_LDFLAGS  = $(foreach lib,$(MOCK_PKG_LIBS),$(shell pkg-config --libs $(lib) 2>/dev/null))

test-mocks: $(MOCK_TARGET)
	./$(MOCK_TARGET)

$(MOCK_TARGET): $(MOCK_ALL_SRCS)
	$(CC) $(MOCK_CFLAGS) -o $@ $^ $(MOCK_LDFLAGS)

.PHONY: all clean test test-mocks
