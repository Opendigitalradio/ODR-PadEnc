ACLOCAL_AMFLAGS = -I m4
AUTOMAKE_OPTIONS = subdir-objects

if IS_GIT_REPO
GITVERSION_FLAGS = -DGITVERSION="\"`git describe --dirty`\""
else
GITVERSION_FLAGS =
endif

odr_padenc_CXXFLAGS = $(GITVERSION_FLAGS) @MAGICKWAND_CFLAGS@ -Icontrib -Wall -Wextra
odr_padenc_LDADD    = @MAGICKWAND_LDADD@
odr_padenc_SOURCES  = src/odr-padenc.cpp \
					  src/crc.cpp

bin_PROGRAMS = odr-padenc$(EXEEXT)


EXTRA_DIST = \
    $(top_srcdir)/bootstrap \
    $(top_srcdir)/README.md \
    $(top_srcdir)/Doxyfile

doc: export PROJECT_NUMBER:=$(shell git describe --dirty)

.PHONY: doc
doc:
	doxygen

