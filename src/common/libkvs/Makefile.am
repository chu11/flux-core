AM_CFLAGS = \
	$(WARNING_CFLAGS) \
	$(CODE_COVERAGE_CFLAGS)

AM_LDFLAGS = \
	$(CODE_COVERAGE_LIBS)

AM_CPPFLAGS = \
	$(CODE_COVERAGE_CPPFLAGS) \
	-I$(top_srcdir) \
	-I$(top_srcdir)/src/include \
	-I$(top_srcdir)/src/common/libccan \
	-I$(top_builddir)/src/common/libflux \
	$(JANSSON_CFLAGS)

noinst_LTLIBRARIES = libkvs.la

libkvs_la_SOURCES = \
	kvs.c \
	kvs_lookup.c \
	kvs_getroot.c \
	kvs_checkpoint.c \
	kvs_checkpoint.h \
	kvs_dir.c \
	kvs_dir_private.h \
	kvs_commit.c \
	kvs_txn.c \
	kvs_txn_compact.c \
	kvs_txn_private.h \
	treeobj.h \
	treeobj.c \
	kvs_copy.c \
	kvs_util.c \
	kvs_util_private.h

fluxcoreinclude_HEADERS = \
	kvs.h \
	kvs_lookup.h \
	kvs_getroot.h \
	kvs_dir.h \
	kvs_txn.h \
	kvs_commit.h \
	kvs_copy.h

TESTS = \
	test_kvs.t \
	test_kvs_txn.t \
	test_kvs_txn_compact.t \
	test_kvs_lookup.t \
	test_kvs_dir.t \
	test_kvs_commit.t \
	test_kvs_getroot.t \
	test_treeobj.t \
	test_kvs_checkpoint.t \
	test_kvs_copy.t \
	test_kvs_util.t

check_PROGRAMS = \
	$(TESTS)

TEST_EXTENSIONS = .t
T_LOG_DRIVER = env AM_TAP_AWK='$(AWK)' $(SHELL) \
	$(top_srcdir)/config/tap-driver.sh

test_ldadd = \
	$(top_builddir)/src/common/libkvs/libkvs.la \
	$(top_builddir)/src/common/libflux/libflux.la \
	$(top_builddir)/src/common/libflux-internal.la \
	$(top_builddir)/src/common/libtap/libtap.la \
	$(JANSSON_LIBS) \
	$(LIBPTHREAD)

test_cppflags = \
	$(AM_CPPFLAGS) \
	-I$(top_srcdir)/src/common/libtap

test_kvs_t_SOURCES = test/kvs.c
test_kvs_t_CPPFLAGS = $(test_cppflags)
test_kvs_t_LDADD = $(test_ldadd)

test_kvs_txn_t_SOURCES = test/kvs_txn.c
test_kvs_txn_t_CPPFLAGS = $(test_cppflags)
test_kvs_txn_t_LDADD = $(test_ldadd)

test_kvs_txn_compact_t_SOURCES = test/kvs_txn_compact.c
test_kvs_txn_compact_t_CPPFLAGS = $(test_cppflags)
test_kvs_txn_compact_t_LDADD = $(test_ldadd)

test_kvs_lookup_t_SOURCES = test/kvs_lookup.c
test_kvs_lookup_t_CPPFLAGS = $(test_cppflags)
test_kvs_lookup_t_LDADD = \
	$(top_builddir)/src/common/libkvs/kvs_txn.o \
	$(test_ldadd)

test_kvs_dir_t_SOURCES = test/kvs_dir.c
test_kvs_dir_t_CPPFLAGS = $(test_cppflags)
test_kvs_dir_t_LDADD = $(test_ldadd)

test_kvs_commit_t_SOURCES = test/kvs_commit.c
test_kvs_commit_t_CPPFLAGS = $(test_cppflags)
test_kvs_commit_t_LDADD = $(test_ldadd)

test_kvs_getroot_t_SOURCES = test/kvs_getroot.c
test_kvs_getroot_t_CPPFLAGS = $(test_cppflags)
test_kvs_getroot_t_LDADD = $(test_ldadd)

test_treeobj_t_SOURCES = test/treeobj.c
test_treeobj_t_CPPFLAGS = $(test_cppflags)
test_treeobj_t_LDADD = $(test_ldadd)

test_kvs_checkpoint_t_SOURCES = test/kvs_checkpoint.c
test_kvs_checkpoint_t_CPPFLAGS = $(test_cppflags)
test_kvs_checkpoint_t_LDADD = $(test_ldadd)

test_kvs_copy_t_SOURCES = test/kvs_copy.c
test_kvs_copy_t_CPPFLAGS = $(test_cppflags)
test_kvs_copy_t_LDADD = $(test_ldadd)

test_kvs_util_t_SOURCES = test/kvs_util.c
test_kvs_util_t_CPPFLAGS = $(test_cppflags)
test_kvs_util_t_LDADD = $(test_ldadd)
