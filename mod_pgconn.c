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

#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#include "mod_pgconn.h"


/******************************************************************************
 * getPGconnContainerByName()                                                 *
 *   Finds the <PGconn> container with the given name.                        *
 *                                                                            *
 * IN:	v_PGconnServerConfig - the server config that points to the start of  *
 * 				the <PGconn> container list to search.        *
 * 	v_connectionName - the connection name to search for.                 *
 *                                                                            *
 * Returns:	pointer to the <PGconn> container, or...                      *
 * 		NULL, if the connection name was not found.                   *
 ******************************************************************************/
static tPGconnContainer* getPGconnContainerByName(
	const tPGconnServerConfig* v_PGconnServerConfig,
	const char* v_connectionName
)
{
	if ((!v_PGconnServerConfig) || (!v_connectionName))
		return NULL;

	tPGconnContainer* t_PGconnContainer;
	for (t_PGconnContainer = v_PGconnServerConfig->m_first_PGconnContainer;
			t_PGconnContainer;
			t_PGconnContainer = t_PGconnContainer->m_next)
		if (!strcasecmp(t_PGconnContainer->m_name, v_connectionName))
			return t_PGconnContainer;

	return NULL;
}


/******************************************************************************
 * openPGconn()                                                               *
 *   Opens a new PostgreSQL connection.  This function should only be called  *
 * as the PGconn* resource list constructor.                                  *
 *                                                                            *
 * IN:	v_PGconnContainer - connection container details.                     *
 * 	v_pool - pool to use for memory allocation.                           *
 *                                                                            *
 * OUT:	v_PGconn - connection record pointer.                                 *
 *                                                                            *
 * Returns:	APR_SUCCESS - if the connection was opened successfully.      *
 * 		APR_EGENERAL - if the connection could not be opened.         *
 ******************************************************************************/
static apr_status_t openPGconn(
	void** v_PGconn,
	void* v_PGconnContainer,
	apr_pool_t* v_pool_unused
)
{
	/* Check and initialize the connection pointer */
	if ((!v_PGconn) || (!v_PGconnContainer))
		return APR_EGENERAL;
	*v_PGconn = NULL;

	/* Open a PostgreSQL connection */
	PGconn* t_PGconn = PQconnectdb(
		((tPGconnContainer*)v_PGconnContainer)->m_connInfo
	);
	if (!t_PGconn)
		return APR_EGENERAL;	/* Out of memory! */

	/* Check that the connection was opened successfully */
	if (PQstatus(t_PGconn) != CONNECTION_OK) {
		ap_log_error(
			APLOG_MARK, APLOG_ERR, 0, NULL,
			"PQconnectdb() error: %s", PQerrorMessage(t_PGconn)
		);
		PQfinish(t_PGconn);
		return APR_EGENERAL;
	}
	else {
		(*(PGconn**)v_PGconn) = t_PGconn;
		return APR_SUCCESS;
	}
}


/******************************************************************************
 * openPGconn_tracing()                                                       *
 *   Opens a new PostgreSQL connection, outputting connection tracing         *
 * information to a file.  This function should only be called as the PGconn* *
 * resource list constructor.                                                 *
 *                                                                            *
 * IN:	v_PGconnContainer - connection container details.                     *
 * 	v_pool - pool to use for memory allocation.                           *
 *                                                                            *
 * OUT:	v_PGconn - connection record pointer.                                 *
 *                                                                            *
 * Returns:	APR_SUCCESS - if the connection was opened successfully.      *
 * 		APR_EGENERAL - if the connection could not be opened.         *
 ******************************************************************************/
static apr_status_t openPGconn_tracing(
	void** v_PGconn,
	void* v_PGconnContainer,
	apr_pool_t* v_pool
)
{
	/* Check and initialize the connection pointer */
	if ((!v_PGconn) || (!v_PGconnContainer) || (!v_pool))
		return APR_EGENERAL;
	*v_PGconn = NULL;

	/* Open a PostgreSQL connection */
	#define d_PGconnContainer	((tPGconnContainer*)v_PGconnContainer)
	PGconn* t_PGconn = PQconnectdb(d_PGconnContainer->m_connInfo);
	if (!t_PGconn)
		return APR_EGENERAL;	/* Out of memory! */

	/* Check that the connection was opened successfully */
	if (PQstatus(t_PGconn) != CONNECTION_OK) {
		ap_log_error(
			APLOG_MARK, APLOG_ERR, 0, NULL,
			"PQconnectdb() error: %s", PQerrorMessage(t_PGconn)
		);
		PQfinish(t_PGconn);
		return APR_EGENERAL;
	}

	/* Open a new trace file */
	FILE* t_traceFile = fopen(
		apr_psprintf(
			v_pool, "%s/%d_%d.trc", d_PGconnContainer->m_traceDir,
			getpid(), PQbackendPID(t_PGconn)
		),
		"w"
	);
	if (!t_traceFile) {
		/* Failed to open trace file */
		/* Close PostgreSQL connection */
		PQfinish(t_PGconn);
		return APR_EGENERAL;
	}

	/* Register a cleanup handler to close the trace file when the memory
	   pool is destroyed */
	apr_pool_cleanup_register(
		v_pool, t_traceFile, (void*)fclose, apr_pool_cleanup_null
	);

	/* Start tracing */
	PQtrace(t_PGconn, t_traceFile);

	(*(PGconn**)v_PGconn) = t_PGconn;
	return APR_SUCCESS;

	#undef d_PGconnContainer
}


/******************************************************************************
 * closePGconn()                                                              *
 *   Closes a PostgreSQL connection.  This function should only be called as  *
 * the PGconn* resource list destructor.                                      *
 *                                                                            *
 * IN:	v_PGconn - connection record pointer.                                 *
 *                                                                            *
 * Returns:	APR_SUCCESS - if the connection was closed successfully.      *
 * 		APR_EGENERAL - if there was no connection to close.           *
 ******************************************************************************/
static apr_status_t closePGconn(
	void* v_PGconn,
	void* v_PGconnContainer_unused,
	apr_pool_t* v_pool_unused
)
{
	if (!v_PGconn)
		return APR_EGENERAL;
	else {
		/* Close the PostgreSQL connection */
		PQfinish((PGconn*)v_PGconn);
		return APR_SUCCESS;
	}
}


/******************************************************************************
 * closePGconn_tracing()                                                      *
 *   Disabled connection tracing and closes a PostgreSQL connection.  This    *
 * function should only be called as the PGconn* resource list destructor.    *
 *                                                                            *
 * IN:	v_PGconn - connection record pointer.                                 *
 *                                                                            *
 * Returns:	APR_SUCCESS - if the connection was closed successfully.      *
 * 		APR_EGENERAL - if there was no connection to close.           *
 ******************************************************************************/
static apr_status_t closePGconn_tracing(
	void* v_PGconn,
	void* v_PGconnContainer_unused,
	apr_pool_t* v_pool_unused
)
{
	if (!v_PGconn)
		return APR_EGENERAL;
	else {
		/* Disable connection tracing and close the PostgreSQL
		  connection */
		PQuntrace((PGconn*)v_PGconn);
		PQfinish((PGconn*)v_PGconn);
		return APR_SUCCESS;
	}
}


/******************************************************************************
 * acquirePGconn()                                                            *
 *   Acquires a PostgreSQL connection from the PGconn* resource list.         *
 * The resource list takes care of closing/reusing/timing-out connections as  *
 * required.                                                                  *
 *                                                                            *
 * IN:	v_PGconnContainer - connection container details.                     *
 * 	v_PGconn - should be NULL.                                            *
 *                                                                            *
 * OUT:	v_PGconn - connection record pointer (if successful).                 *
 *                                                                            *
 * Returns:	PGCONN_ACQUIRED - if everything was OK.                       *
 * 		PGCONN_ALREADYACQUIRED - if a connection was already          *
 * 					acquired.                             *
 * 		PGCONN_UNAVAILABLE - if all the connections in the pool are   *
 * 					already in use.                       *
 * 		PGCONN_BAD - if the connection could not be opened/reset.     *
 ******************************************************************************/
static ePGconnStatus acquirePGconn(
	const tPGconnContainer* v_PGconnContainer,
	PGconn** v_PGconn
)
{
	if ((!v_PGconnContainer) || (!v_PGconn))
		return PGCONN_BAD;
	/* Don't allow acquirePGconn() to be called twice without a call to
	   releasePGconn() inbetween */
	else if (*v_PGconn)
		return PGCONN_ALREADYACQUIRED;
	/* Check that the PGconn* resource list was created successfully */
	else if (!(v_PGconnContainer->m_PGconnPool))
		return PGCONN_UNAVAILABLE;

	/* Acquire a connection from the PGconn* resource list */
	apr_status_t t_status = apr_reslist_acquire(
		v_PGconnContainer->m_PGconnPool, (void**)v_PGconn
	);
	if (t_status != APR_SUCCESS)
		return PGCONN_UNAVAILABLE;

	/* Check the connection status */
	if (PQstatus(*v_PGconn) != CONNECTION_OK) {
		/* Problem with connection. Try resetting it */
		PQreset(*v_PGconn);
		/* Check the connection status again */
		if (PQstatus(*v_PGconn) != CONNECTION_OK) {
			/* Connection still doesn't work, so release the
			   resource straight away */
			apr_reslist_release(
				v_PGconnContainer->m_PGconnPool, *v_PGconn
			);
			*v_PGconn = NULL;
			return PGCONN_BAD;
		}
	}

	/* Connection acquired successfully */
	return PGCONN_ACQUIRED;
}


/******************************************************************************
 * releasePGconn()                                                            *
 *   Releases a PostgreSQL connection back to the PGconn* resource list.      *
 * The resource list takes care of closing/reusing/timing-out connections as  *
 * required.                                                                  *
 *                                                                            *
 * IN:	v_PGconnContainer - connection container details.                     *
 * 	v_PGconn - connection record pointer (should be non-NULL).            *
 *                                                                            *
 * OUT:	v_PGconn - NULL (if the connection has been released successfully).   *
 *                                                                            *
 * Returns:	PGCONN_RELEASED - if everything was OK.                       *
 * 		PGCONN_BAD - if there was no acquired connection to release.  *
 ******************************************************************************/
static ePGconnStatus releasePGconn(
	const tPGconnContainer* v_PGconnContainer,
	PGconn** v_PGconn
)
{
	/* If there is a currently acquired connection, release the resource */
	if ((!v_PGconnContainer) || (!v_PGconn))
		return PGCONN_BAD;
	else if (*v_PGconn)
		if (apr_reslist_release(v_PGconnContainer->m_PGconnPool,
					*v_PGconn) == APR_SUCCESS) {
			*v_PGconn = NULL;
			return PGCONN_RELEASED;
		}

	return PGCONN_BAD;	/* No acquired connection to release! */
}


/******************************************************************************
 * measurePGconnAvailability()                                                *
 *   Check how many PGconn resources aren't currently in use.                 *
 *                                                                            *
 * IN:	v_PGconnContainer - connection container details.                     *
 *                                                                            *
 * Returns:	0..100 - % of the total PGconn resources not currently in use.*
 ******************************************************************************/
static int measurePGconnAvailability(
	const tPGconnContainer* v_PGconnContainer
)
{
	if (!v_PGconnContainer)
		return 0;

	return ((v_PGconnContainer->m_poolMaxHard
			- apr_reslist_acquired_count(
				v_PGconnContainer->m_PGconnPool
			)
		) * 100) / v_PGconnContainer->m_poolMaxHard;
}


/******************************************************************************
 * PGconn_serverConfig_create()                                               *
 *   Creates the per-server configuration structure.                          *
 *                                                                            *
 * IN:	v_pool - pool to use for memory allocation.                           *
 *                                                                            *
 * Returns:	pointer to per-server config structure.                       *
 ******************************************************************************/
static void* PGconn_serverConfig_create(
	apr_pool_t* v_pool,
	server_rec* v_server_unused
)
{
	tPGconnServerConfig* t_PGconnServerConfig;

	/* Allocate memory for per-server config structure */
	t_PGconnServerConfig = (tPGconnServerConfig*)apr_pcalloc(
		v_pool, sizeof(*t_PGconnServerConfig)
	);

	/* No connections setup to start with. 'm_first_PGconnContainer' will
	   have already been NULLified by memset() */

	return (void*)t_PGconnServerConfig;
}


/******************************************************************************
 * PGconn_dirConfig_create()                                                  *
 *   Creates the per-directory configuration structure.                       *
 *                                                                            *
 * IN:	v_pool - pool to use for memory allocation.                           *
 *                                                                            *
 * Returns:	pointer to per-directory config structure.                    *
 ******************************************************************************/
static void* PGconn_dirConfig_create(
	apr_pool_t* v_pool,
	char* v_directory_unused
)
{
	tPGconnDirConfig* t_PGconnDirConfig;

	/* Allocate memory for per-directory config structure, zeroising it with
	   memset() */
	t_PGconnDirConfig = (tPGconnDirConfig*)apr_pcalloc(
		v_pool, sizeof(*t_PGconnDirConfig)
	);

	/* There is no default default <PGconn> container.
	   'm_defaultPGconnContainer' will have already been NULLified by
	   memset() */

	return (void*)t_PGconnDirConfig;
}


/******************************************************************************
 * PGconn_containerCommand()                                                  *
 *   Process the "<PGconn>" container directive.                              *
 *                                                                            *
 * IN:	v_cmdParms - various server configuration details.                    *
 * 	v_args - connection name plus trailing ">".                           *
 *                                                                            *
 * Returns:	NULL or an error message.                                     *
 ******************************************************************************/
static const char* PGconn_containerCommand(
	cmd_parms* v_cmdParms,
	void* v_dirConfig_unused,
	const char* v_args
)
{
	tPGconnServerConfig* t_PGconnServerConfig;
	tPGconnContainer** t_PGconnContainer;
	ap_directive_t* t_directive;
	const char* t_args;
	char* t_endPtr = "";
	static APR_OPTIONAL_FN_TYPE(getAllFunctionDetails)*
							getAllFunctionDetails;
	char* t_errorMessage = NULL;

	/* Check that the Connection Name has been specified */
	if (v_args[0] == '>')
		return "Missing Connection Name";

	/* Check that there is a closing ">" for the container start
	   directive */
	if (!strrchr(v_args, '>'))
		return "Missing \">\"";

	/* Get the server configuration structure */
	t_PGconnServerConfig = (tPGconnServerConfig*)ap_get_module_config(
		v_cmdParms->server->module_config, &pgconn_module
	);
	if (!t_PGconnServerConfig)
		return "ap_get_module_config() failed";

	/* Find the end of the <PGconn> container linked list */
	for (t_PGconnContainer = &(t_PGconnServerConfig->
						m_first_PGconnContainer);
			*t_PGconnContainer;
			t_PGconnContainer = &((*t_PGconnContainer)->m_next))
		if (!strcmp((*t_PGconnContainer)->m_name, v_args))
			return "Duplicate Connection Name";

	/* Allocate memory for the <PGconn> container structure, zeroising with
	   memset() */
	*t_PGconnContainer = (tPGconnContainer*)apr_pcalloc(
		v_cmdParms->pool, sizeof(**t_PGconnContainer)
	);
	if (!(*t_PGconnContainer))
		return "Not enough memory";

	/* This is added to the end of the list. 'm_next' will already be
	   NULL, because apr_pcalloc() was used to allocate memory */
	/* No PGconn* resource list yet. 'm_PGconnPool' will already be NULL,
	   because apr_pcalloc() was used to allocate memory */
	/* Copy the connection name, removing the container start directive's
	   closing ">" */
	(*t_PGconnContainer)->m_name = apr_pstrndup(
		v_cmdParms->pool, v_args, strlen(v_args) - 1
	);
	/* Default 'connInfo' is "" */
	(*t_PGconnContainer)->m_connInfo = "";
	/* Minimum pool size will already be '0', because apr_pcalloc() was used
	   to allocate memory */
	/* Soft Maximum pool size will already be '0', because apr_pcalloc() was
	   used to allocate memory */
	/* Set Hard Maximum pool size to 1 */
	(*t_PGconnContainer)->m_poolMaxHard = 1;
	/* Default 'poolTTL' will already be '0', because apr_pcalloc() was used
	   to allocate memory */
	/* Default 'traceDir' will already be NULL, because apr_pcalloc() was
	   used to allocate memory */
	/* Catalog cache is disabled by default. 'm_catalogCache' will already
	   be DISABLED and 'm_catalog' will already be NULL, because
	   apr_pcalloc() was used to allocate memory */

	/* Parse the contents of the container */
	for (t_directive = v_cmdParms->directive->first_child; t_directive;
			t_directive = t_directive->next) {
		t_args = t_directive->args;
		if (!strcasecmp(t_directive->directive, "ConnInfo")) {
			(*t_PGconnContainer)->m_connInfo = ap_getword_conf(
				v_cmdParms->pool, &t_args
			);
			if (*t_args)
				return "ConnInfo: Too many arguments";
			else if (strlen((*t_PGconnContainer)->m_connInfo))
				continue;
			else
				return "ConnInfo: Too few arguments";
		}
		else if (!strcasecmp(t_directive->directive, "PoolMin"))
			(*t_PGconnContainer)->m_poolMin = strtol(
				t_directive->args, &t_endPtr, 10
			);
		else if (!strcasecmp(t_directive->directive, "PoolMaxSoft"))
			(*t_PGconnContainer)->m_poolMaxSoft = strtol(
				t_directive->args, &t_endPtr, 10
			);
		else if (!strcasecmp(t_directive->directive, "PoolMaxHard"))
			(*t_PGconnContainer)->m_poolMaxHard = strtol(
				t_directive->args, &t_endPtr, 10
			);
		else if (!strcasecmp(t_directive->directive, "PoolTTL"))
			(*t_PGconnContainer)->m_poolTTL = apr_strtoi64(
				t_directive->args, &t_endPtr, 10
			);
		else if (!strcasecmp(t_directive->directive, "TraceDir")) {
			(*t_PGconnContainer)->m_traceDir = ap_getword_conf(
				v_cmdParms->pool, &t_args
			);
			if (*t_args)
				return "TraceDir: Too many arguments";
			else if (strlen((*t_PGconnContainer)->m_traceDir)) {
				(*t_PGconnContainer)->m_traceDir
						= ap_server_root_relative(
					v_cmdParms->pool,
					(*t_PGconnContainer)->m_traceDir
				);
				continue;
			}
			else
				return "TraceDir: Too few arguments";
		}
		else if (!strcasecmp(t_directive->directive, "CatalogCache")) {
			if (!strcasecmp(t_args, "disabled"))
				(*t_PGconnContainer)->m_catalogCache = DISABLED;
			else if (!strcasecmp(t_args, "enabled"))
				(*t_PGconnContainer)->m_catalogCache = ENABLED;
			else if (!strcasecmp(t_args, "required"))
				(*t_PGconnContainer)->m_catalogCache = REQUIRED;
		}
		else
			return apr_psprintf(
				v_cmdParms->temp_pool,
				"'%s' not recognized", t_directive->directive
			);

		/* Check if an error occurred */
		if (*t_endPtr)
			return apr_psprintf(
				v_cmdParms->temp_pool,
				"Invalid value specified for '%s'",
				t_directive->directive
			);
	}

	/* If required, call the mod_pgproc function to cache the "function
	   catalog" */
	if ((*t_PGconnContainer)->m_catalogCache != DISABLED) {
		getAllFunctionDetails = APR_RETRIEVE_OPTIONAL_FN(
			getAllFunctionDetails
		);
		if (!getAllFunctionDetails)
			t_errorMessage = "\"CatalogCache enabled/required\""
					" requires mod_pgproc!";
		else
			t_errorMessage = getAllFunctionDetails(
				v_cmdParms->pool, v_cmdParms->temp_pool,
				(*t_PGconnContainer)
			);
	}

	return t_errorMessage;
}


/******************************************************************************
 * PGconn_command()                                                           *
 *   Process the "PGconn" command.                                            *
 *                                                                            *
 * IN:	v_cmdParms - various server configuration details.                    *
 * 	v_PGconnDirConfig - the per-directory config structure.               *
 * 	v_moduleName - if specified, the name of the module that should use   *
 * 			the specified connection name by default (otherwise,  *
 * 			the specified connection name will be used by default *
 * 			for all modules).                                     *
 * 	v_PGconnName - the name of the <PGconn> container to use, by default, *
 * 			for this directory.                                   *
 *                                                                            *
 * Returns:	NULL or an error message.                                     *
 ******************************************************************************/
static const char* PGconn_command(
	cmd_parms* v_cmdParms,
	void* v_PGconnDirConfig,
	const char* v_PGconnName,		/* these are reversed... */
	const char* v_moduleName		/* ...on purpose */
)
{
	tPGconnServerConfig* t_PGconnServerConfig;
	tPGconnContainer* t_PGconnContainer;

	/* Check if this directive should be handled by another module */
	if (v_moduleName)
		return DECLINE_CMD;

	/* Get the server configuration structure */
	t_PGconnServerConfig = (tPGconnServerConfig*)ap_get_module_config(
		v_cmdParms->server->module_config, &pgconn_module
	);

	/* Find the required <PGconn> container record */
	t_PGconnContainer = getPGconnContainerByName(
		t_PGconnServerConfig, v_PGconnName
	);
	#define t_PGconnDirConfig	((tPGconnDirConfig*)v_PGconnDirConfig)
	if (t_PGconnContainer)
		t_PGconnDirConfig->m_defaultPGconnContainer = t_PGconnContainer;
	else
		return "Invalid Connection Name";
	#undef t_PGconnDirConfig

	return NULL;
}


/******************************************************************************
 * PGconn_childInit()                                                         *
 *   This function is executed once when each new "child" process starts.     *
 * It creates a new PGconn* resource list for each <PGconn>, which is shared  *
 * between all threads (if any) created by the MPM (e.g. worker) for this     *
 * process.  To find all <PGconn>s, we have to navigate through the entire    *
 * list of Virtual Hosts, starting from "v_server", which is the "base"       *
 * Virtual Host.                                                              *
 *                                                                            *
 * IN:	v_pool - pool to use for memory allocation.                           *
 * 	v_server - the server record.                                         *
 ******************************************************************************/
static void PGconn_childInit(
	apr_pool_t* v_pool,
	server_rec* v_server
)
{
	tPGconnServerConfig* t_PGconnServerConfig;
	tPGconnContainer* t_PGconnContainer;
	server_rec* t_server;
	apr_status_t t_status;

	/* Navigate through all the Virtual Hosts */
	for (t_server = v_server; t_server; t_server = t_server->next) {
		/* Get the server configuration structure */
		t_PGconnServerConfig =
			(tPGconnServerConfig*)ap_get_module_config(
				t_server->module_config, &pgconn_module
			);

		/* Loop through each of this Virtual Host's <PGconn>
		   containers */
		for (t_PGconnContainer = t_PGconnServerConfig->
							m_first_PGconnContainer;
				t_PGconnContainer;
				t_PGconnContainer = t_PGconnContainer->m_next)
			if (t_PGconnContainer->m_poolMaxHard >= 1) {
				/* Connections are allowed, so create the
				   PGconn* resource list for this process */
				t_status = apr_reslist_create(
					&(t_PGconnContainer->m_PGconnPool),
					t_PGconnContainer->m_poolMin,
					t_PGconnContainer->m_poolMaxSoft,
					t_PGconnContainer->m_poolMaxHard,
					t_PGconnContainer->m_poolTTL,
					t_PGconnContainer->m_traceDir ?
						openPGconn_tracing :
						openPGconn,
					t_PGconnContainer->m_traceDir ?
						closePGconn_tracing :
						closePGconn,
					t_PGconnContainer, v_pool
				);
				if (t_status != APR_SUCCESS)
					ap_log_error(
						APLOG_MARK, APLOG_ERR, 0,
						v_server,
						"Failed to create PGconn*"
							" resource list!"
					);
				else	/* Register a cleanup function to
					   destroy the PGconn* resource list
					   when the server shuts down */
					apr_pool_cleanup_register(
						v_pool,
						t_PGconnContainer->m_PGconnPool,
						(void*)apr_reslist_destroy,
						apr_pool_cleanup_null
					);
			}
	}
}


/*----------------------------------------------------------------------------
  - Command Table                                                            -
  ----------------------------------------------------------------------------*/
static const command_rec PGconn_commandTable[] = {
	AP_INIT_RAW_ARGS(
		"<PGconn", PGconn_containerCommand, NULL, RSRC_CONF,
		"a <PGconn> container"
	),
	AP_INIT_TAKE12(
		"PGconn", PGconn_command, NULL, ACCESS_CONF,
		"a <PGconn> container name"
	),
	NULL
};


/******************************************************************************
 * PGconn_registerHooks()                                                     *
 ******************************************************************************/
static void PGconn_registerHooks(
	apr_pool_t* v_pool_unused
)
{
	APR_REGISTER_OPTIONAL_FN(getPGconnContainerByName);
	APR_REGISTER_OPTIONAL_FN(acquirePGconn);
	APR_REGISTER_OPTIONAL_FN(releasePGconn);
	APR_REGISTER_OPTIONAL_FN(measurePGconnAvailability);

	/* Register "child init" handler */
	ap_hook_child_init(PGconn_childInit, NULL, NULL, APR_HOOK_MIDDLE);
}


/*----------------------------------------------------------------------------
  - Module record: dispatch list for API hooks                               -
  ----------------------------------------------------------------------------*/
module AP_MODULE_DECLARE_DATA pgconn_module = {
	STANDARD20_MODULE_STUFF,
	PGconn_dirConfig_create,	/* create per-dir config */
	NULL,				/* merge per-dir config */
	PGconn_serverConfig_create,	/* create per-server config */
	NULL,				/* merge per-server config */
	PGconn_commandTable,		/* table of config file commands */
	PGconn_registerHooks		/* register hooks */
};
