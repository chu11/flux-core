AM_CFLAGS = \
        $(WARNING_CFLAGS) \
        $(CODE_COVERAGE_CFLAGS)

AM_LDFLAGS = \
        $(CODE_COVERAGE_LDFLAGS)

AM_CPPFLAGS = \
	-I$(top_srcdir) \
	-I$(top_srcdir)/src/include \
	-I$(top_builddir)/src/common/libflux \
	$(ZMQ_CFLAGS)

noinst_LTLIBRARIES = \
	libzsock.la

libzsock_la_SOURCES = \
	zsock_msg.h \
	zsock_msg.c

TESTS = test_zsock_msg.t

check_PROGRAMS = \
        $(TESTS)

TEST_EXTENSIONS = .t
T_LOG_DRIVER = env AM_TAP_AWK='$(AWK)' $(SHELL) \
        $(top_srcdir)/config/tap-driver.sh

test_ldadd = \
        $(top_builddir)/src/common/libtestutil/libtestutil.la \
        $(top_builddir)/src/common/libzsock/libzsock.la \
        $(top_builddir)/src/common/libflux-internal.la \
        $(top_builddir)/src/common/libflux-core.la \
        $(top_builddir)/src/common/libtap/libtap.la \
	$(ZMQ_LIBS)

test_cppflags = \
        $(AM_CPPFLAGS) \
        -I$(top_srcdir)/src/common/libtap

test_ldflags = \
	-no-install

test_zsock_msg_t_SOURCES = test/zsock_msg.c
test_zsock_msg_t_CPPFLAGS = $(test_cppflags)
test_zsock_msg_t_LDADD = $(test_ldadd)
test_zsock_msg_t_LDFLAGS = $(test_ldflags)
