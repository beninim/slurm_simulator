# Makefile for acct_gather_profile/none plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

AM_CPPFLAGS = -I$(top_srcdir) -I$(top_srcdir)/src/common

pkglib_LTLIBRARIES = acct_gather_profile_none.la

# Null job completion logging plugin.
acct_gather_profile_none_la_SOURCES = acct_gather_profile_none.c

acct_gather_profile_none_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS)

