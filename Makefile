builddir=.
top_srcdir=/usr/lib/apache2
top_builddir=${top_srcdir}
include ${top_srcdir}/build/special.mk
builddir=.

#   the used tools
APXS=apxs
APACHECTL=apachectl

#   additional defines, includes and libraries
LDFLAGS=-lpq

#   the default target
all: local-shared-build
