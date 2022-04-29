AM_CFLAGS = \
	$(WARNING_CFLAGS) \
	$(CODE_COVERAGE_CFLAGS)

AM_LDFLAGS = \
	$(CODE_COVERAGE_LIBS) \
	$(JANSSON_CFLAGS)

AM_CPPFLAGS = \
	-I$(top_srcdir) \
	-I$(top_srcdir)/src/include \
	-I$(top_builddir)/src/common/libflux \
	$(SQLITE_CFLAGS) \
	$(FLUX_SECURITY_CFLAGS)

fluxmod_LTLIBRARIES = job-archive2.la

job_archive2_la_SOURCES = \
	job-archive2.c

job_archive2_la_LDFLAGS = $(fluxmod_ldflags) -module
job_archive2_la_LIBADD = \
	$(top_builddir)/src/common/libjob/libjob.la \
	$(top_builddir)/src/common/libflux-internal.la \
	$(top_builddir)/src/common/libflux-core.la \
	$(top_builddir)/src/common/libflux-optparse.la \
	$(JANSSON_LIBS) \
	$(SQLITE_LIBS)
