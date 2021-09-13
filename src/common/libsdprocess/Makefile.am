AM_CFLAGS = \
        $(WARNING_CFLAGS) \
        $(CODE_COVERAGE_CFLAGS)

AM_LDFLAGS = \
        $(CODE_COVERAGE_LDFLAGS)

AM_CPPFLAGS = \
	-I$(top_srcdir) \
	-I$(top_srcdir)/src/include \
	-I$(top_builddir)/src/common/libflux

noinst_LTLIBRARIES = \
	libsdprocess.la

libsdprocess_la_SOURCES = \
	sdprocess.c

fluxcoreinclude_HEADERS = \
	sdprocess.h

libsdprocess_la_LIBADD = \
	$(LIBSYSTEMD_LIBS)

TESTS = \
	test_sdprocess.t

check_PROGRAMS = \
	$(TESTS)

TEST_EXTENSIONS = .t
T_LOG_DRIVER = env AM_TAP_AWK='$(AWK)' $(SHELL) \
        $(top_srcdir)/config/tap-driver.sh

test_ldadd = \
        $(top_builddir)/src/common/libtap/libtap.la \
        $(top_builddir)/src/common/libsdprocess/libsdprocess.la \
        $(top_builddir)/src/common/libflux-internal.la \
        $(top_builddir)/src/common/libflux-core.la

test_ldflags = \
	-no-install

test_cppflags = \
        $(AM_CPPFLAGS) \
	-I$(top_srcdir)/src/common/libtap

test_sdprocess_t_SOURCES = test/sdprocess.c
test_sdprocess_t_CPPFLAGS = \
	-DTEST_SDPROCESS_DIR=\"$(top_builddir)/src/common/libsdprocess/\" \
	$(test_cppflags)
test_sdprocess_t_LDADD = $(test_ldadd)
test_sdprocess_t_LDFLAGS = $(test_ldflags)
