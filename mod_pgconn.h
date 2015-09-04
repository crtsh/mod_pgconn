/* mod_pgconn - An httpd module for PostgreSQL connection pooling
 * Written by Rob Stradling
 * Copyright (C) 2003-2015 COMODO CA Limited
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef MOD_PGCONN_H
#define MOD_PGCONN_H

#include <stdio.h>

/* Apache 2.0 include files */
#include "apr_hash.h"
#include "apr_lib.h"
#include "apr_optional.h"
#include "apr_reslist.h"
#include "apr_strings.h"
#include "httpd.h"
#include "http_config.h"
#include "http_log.h"
#include "http_protocol.h"

/* PostgreSQL include files */
#include "libpq-fe.h"


/* Forward reference for module record */
module AP_MODULE_DECLARE_DATA pgconn_module;


/* Enumerate the API return codes */
typedef enum {
	PGCONN_ALREADYACQUIRED	= 0,
	PGCONN_ACQUIRED		= 1,
	PGCONN_RELEASED		= 2,
	PGCONN_UNAVAILABLE	= 3,
	PGCONN_BAD		= 4
} ePGconnStatus;

/* Enumerate the Catalog Cache modes of operation */
typedef enum {
	DISABLED	= 0,
	ENABLED		= 1,
	REQUIRED	= 2
} eCatalogCache;


/* Typedef for <PGconn> container structure */
typedef struct tPGconnContainer {
	struct tPGconnContainer* m_next;
	apr_reslist_t* m_PGconnPool;
	char* m_name;
	char* m_connInfo;
	int m_poolMin;
	int m_poolMaxSoft;
	int m_poolMaxHard;
	apr_int64_t m_poolTTL;	/* Microseconds */
	char* m_traceDir;
	/* Used by mod_pgproc */
	eCatalogCache m_catalogCache;
	apr_hash_t* m_catalog;	/* "schema.name" -> tFunctionDetails */
} tPGconnContainer;


/* Typedef for per-server configuration information */
typedef struct tPGconnServerConfig {
	/* Linked list of <PGconn> containers */
	tPGconnContainer* m_first_PGconnContainer;
} tPGconnServerConfig;


/* Typedef for per-directory configuration information */
typedef struct tPGconnDirConfig {
	tPGconnContainer* m_defaultPGconnContainer;
} tPGconnDirConfig;


/* Functions exported by this module */
APR_DECLARE_OPTIONAL_FN(
	tPGconnContainer*, getPGconnContainerByName,
	(const tPGconnServerConfig*, const char*)
);
APR_DECLARE_OPTIONAL_FN(
	ePGconnStatus, acquirePGconn,
	(const tPGconnContainer*, PGconn** v_PGconn)
);
APR_DECLARE_OPTIONAL_FN(
	ePGconnStatus, releasePGconn,
	(const tPGconnContainer*, PGconn** v_PGconn)
);
APR_DECLARE_OPTIONAL_FN(
	int, measurePGconnAvailability, (const tPGconnContainer*)
);

/* Functions imported by this module */
APR_DECLARE_OPTIONAL_FN(
	char*, getAllFunctionDetails,
	(apr_pool_t*, apr_pool_t*, tPGconnContainer*)
);


#endif
