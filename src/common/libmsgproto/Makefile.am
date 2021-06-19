AM_CFLAGS = \
        $(WARNING_CFLAGS) \
        $(CODE_COVERAGE_CFLAGS)

AM_LDFLAGS = \
        $(CODE_COVERAGE_LDFLAGS)

AM_CPPFLAGS = \
	-I$(top_srcdir) \
	-I$(top_srcdir)/src/include \
	-I$(top_builddir)/src/common/libflux \
	-I$(top_srcdir)/src/common/libccan

noinst_LTLIBRARIES = \
	libmsgproto.la

libmsgproto_la_SOURCES = \
	msgproto.h \
	msgproto.c

TESTS = \
	test_msgproto.t

check_PROGRAMS = \
	$(TESTS)

TEST_EXTENSIONS = .t
T_LOG_DRIVER = env AM_TAP_AWK='$(AWK)' $(SHELL) \
        $(top_srcdir)/config/tap-driver.sh

test_ldadd = \
        $(top_builddir)/src/common/libmsgproto/libmsgproto.la \
        $(top_builddir)/src/common/libflux-internal.la \
        $(top_builddir)/src/common/libflux-core.la \
        $(top_builddir)/src/common/libtap/libtap.la

test_ldflags = \
	-no-install

test_cppflags = \
        $(AM_CPPFLAGS) \
	-I$(top_srcdir)/src/common/libtap

test_msgproto_t_SOURCES = test/msgproto.c
test_msgproto_t_CPPFLAGS = $(test_cppflags)
test_msgproto_t_LDADD = $(test_ldadd)
test_msgproto_t_LDFLAGS = $(test_ldflags)
