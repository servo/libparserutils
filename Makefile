# Component settings
COMPONENT := parserutils
# Default to a static library
COMPONENT_TYPE ?= lib-static

# Setup the tooling
include build/makefiles/Makefile.tools

TESTRUNNER := $(PERL) build/testtools/testrunner.pl

# Toolchain flags
WARNFLAGS := -Wall -Wextra -Wundef -Wpointer-arith -Wcast-align \
	-Wwrite-strings -Wstrict-prototypes -Wmissing-prototypes \
	-Wmissing-declarations -Wnested-externs -Werror -pedantic
CFLAGS := $(CFLAGS) -std=c99 -D_BSD_SOURCE -I$(CURDIR)/include/ \
	-I$(CURDIR)/src $(WARNFLAGS) 

include build/makefiles/Makefile.top

# Extra installation rules
I := include/parserutils
INSTALL_ITEMS := $(INSTALL_ITEMS) /$(I):$(I)/errors.h;$(I)/functypes.h;$(I)/parserutils.h;$(I)/types.h

I := include/parserutils/charset
INSTALL_ITEMS := $(INSTALL_ITEMS) /$(I):$(I)/codec.h;$(I)/mibenum.h;$(I)utf16.h;$(I)/utf8.h

I := include/parserutils/inputstream
INSTALL_ITEMS := $(INSTALL_ITEMS) /$(I):$(I)/inputstream.h

I := include/parserutils/utils
INSTALL_ITEMS := $(INSTALL_ITEMS) /$(I):$(I)/buffer.h;$(I)/stack.h;$(I)/vector.h

INSTALL_ITEMS := $(INSTALL_ITEMS) /lib/pkgconfig:lib$(COMPONENT).pc.in
INSTALL_ITEMS := $(INSTALL_ITEMS) /lib:$(BUILDDIR)/lib$(COMPONENT)$(LIBEXT)
