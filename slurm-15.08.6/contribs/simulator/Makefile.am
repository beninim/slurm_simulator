#
# Makefile for simdate

AUTOMAKE_OPTIONS = foreign

AM_CPPFLAGS = -I$(top_srcdir)

bin_PROGRAMS = sim_mgr simdate edit_trace simqsnap  list_trace trace_builder update_trace mysql_trace_builder

sim_mgr_LDADD = 	$(top_builddir)/src/api/libslurm.o $(DL_LIBS)
simdate_LDADD = 	$(top_builddir)/src/api/libslurm.o $(DL_LIBS)
list_trace_LDADD = 	$(top_builddir)/src/api/libslurm.o $(DL_LIBS)
trace_builder_LDADD = 	$(top_builddir)/src/api/libslurm.o $(DL_LIBS)
update_trace_LDADD = 	$(top_builddir)/src/api/libslurm.o $(DL_LIBS)
mysql_trace_builder_LDADD = 	$(top_builddir)/src/api/libslurm.o $(DL_LIBS)

sim_mgr_SOURCES = sim_mgr.c
simdate_SOURCES = simdate.c
edit_trace_SOURCES = edit_trace.c
simqsnap_SOURCES = simqsnap.c
list_trace_SOURCES = list_trace.c
trace_builder_SOURCES = trace_builder.c
update_trace_SOURCES = update_trace.c
mysql_trace_builder_SOURCES = mysql_trace_builder.c

sim_mgr_LDFLAGS = -export-dynamic $(CMD_LDFLAGS) \
	$(HWLOC_LDFLAGS) $(HWLOC_LIBS)

simdate_LDFLAGS = -export-dynamic $(CMD_LDFLAGS) \
	$(HWLOC_LDFLAGS) $(HWLOC_LIBS)

edit_trace_LDFLAGS = -export-dynamic $(CMD_LDFLAGS) \
	$(HWLOC_LDFLAGS) $(HWLOC_LIBS)

simqsnap_LDFLAGS = -export-dynamic $(CMD_LDFLAGS) \
	$(HWLOC_LDFLAGS) $(HWLOC_LIBS)

list_trace_LDFLAGS = -export-dynamic $(CMD_LDFLAGS) \
	$(HWLOC_LDFLAGS) $(HWLOC_LIBS)

trace_builder_LDFLAGS = -export-dynamic $(CMD_LDFLAGS) \
	$(HWLOC_LDFLAGS) $(HWLOC_LIBS)

update_trace_LDFLAGS = -export-dynamic $(CMD_LDFLAGS) \
	$(HWLOC_LDFLAGS) $(HWLOC_LIBS)

mysql_trace_builder_LDFLAGS = -export-dynamic $(CMD_LDFLAGS) $(MYSQL_CFLAGS) \
	$(HWLOC_LDFLAGS) $(HWLOC_LIBS) $(MYSQL_LIBS)
