mod_pgconn.la: mod_pgconn.slo
	$(SH_LINK) -rpath $(libexecdir) -module -avoid-version mod_pgconn.lo
DISTCLEAN_TARGETS = modules.mk
shared =  mod_pgconn.la
