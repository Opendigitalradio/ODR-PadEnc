ACLOCAL_AMFLAGS = -I m4
AUTOMAKE_OPTIONS = subdir-objects

if IS_GIT_REPO
GITVERSION_FLAGS = -DGITVERSION="\"`git describe --dirty`\""
else
GITVERSION_FLAGS =
endif

odr_padenc_CXXFLAGS = $(GITVERSION_FLAGS) @MAGICKWAND_CFLAGS@ -Wall -Wextra -fPIE
odr_padenc_LDADD    = @MAGICKWAND_LDADD@
odr_padenc_LDFLAGS  = -pie -z now
odr_padenc_SOURCES  = \
					  src/odr-padenc.cpp \
					  src/odr-padenc.h \
					  src/pad_interface.cpp \
					  src/pad_interface.h \
					  src/common.cpp \
					  src/common.h \
					  src/pad_common.cpp \
					  src/pad_common.h \
					  src/dls.cpp \
					  src/dls.h \
					  src/sls.cpp \
					  src/sls.h \
					  src/charset.cpp \
					  src/charset.h \
					  src/crc.cpp \
					  src/crc.h

bin_PROGRAMS = odr-padenc$(EXEEXT)


EXTRA_DIST = \
    $(top_srcdir)/ChangeLog \
    $(top_srcdir)/bootstrap \
    $(top_srcdir)/README.md \
    $(top_srcdir)/Doxyfile \
    $(top_srcdir)/COPYING \
    $(top_srcdir)/LICENCE

doc: export PROJECT_NUMBER:=$(shell git describe --dirty)

.PHONY: doc
doc:
	doxygen

