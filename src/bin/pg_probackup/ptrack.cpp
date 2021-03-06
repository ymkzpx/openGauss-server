/*-------------------------------------------------------------------------
 *
 * ptrack.c: support functions for ptrack backups
 *
 * Portions Copyright (c) 2020 Huawei Technologies Co.,Ltd.
 * Copyright (c) 2019 Postgres Professional
 *
 *-------------------------------------------------------------------------
 */

#include "pg_probackup.h"

#if PG_VERSION_NUM < 110000
#include "catalog/catalog.h"
#endif
#include "catalog/pg_tablespace.h"
#include "common/fe_memutils.h"
/*
 * Macro needed to parse ptrack.
 * NOTE Keep those values synchronized with definitions in ptrack.h
 */
#define PTRACK_BITS_PER_HEAPBLOCK 1
#define HEAPBLOCKS_PER_BYTE (BITS_PER_BYTE / PTRACK_BITS_PER_HEAPBLOCK)

/*
 * Get lsn of the moment when ptrack was enabled the last time.
 */
XLogRecPtr
get_last_ptrack_lsn(PGconn *backup_conn, PGNodeInfo *nodeInfo)
{
	PGresult   *res;
	uint32		lsn_hi;
	uint32		lsn_lo;
	int			ret;
	XLogRecPtr	lsn;

	res = pgut_execute(backup_conn, "SELECT pg_cbm_tracked_location()", 0, NULL);

	ret = sscanf_s(pg_strdup(PQgetvalue(res, 0, 0)), "%X/%X", &lsn_hi, &lsn_lo);
	securec_check_for_sscanf_s(ret, 2, "\0", "\0");

	/* Calculate LSN */
	lsn = ((uint64) lsn_hi) << 32 | lsn_lo;

	PQclear(res);
	return lsn;
}

/*
 * Fetch a list of changed files with their ptrack maps.
 */
parray *
pg_ptrack_get_pagemapset(PGconn *backup_conn, XLogRecPtr lsn)
{
	PGresult   *res;
	char		start_lsn[17 + 1];
	char	   *params[2];
	char       *saved        = NULL;
	char       *blocknum_str = NULL;
	parray	   *pagemapset   = NULL;
	int			i;
	int			ret		= 0;
	int			blkcnt	= 0;
	BlockNumber	blknum	= 0;
	datapagemap_t pagemap;

	snprintf(start_lsn, sizeof(start_lsn), "%X/%X", (uint32) (lsn >> 32), (uint32) lsn);
	params[0] = gs_pstrdup(start_lsn);

	res = pgut_execute(backup_conn, "CHECKPOINT;", 0, NULL);
	PQclear(res);

	res = pgut_execute(backup_conn, "SELECT pg_cbm_tracked_location()", 0, NULL);
	if (PQnfields(res) != 1) {
		elog(ERROR, "cannot get cbm tracked lsn location");
	}
	params[1] = gs_pstrdup(PQgetvalue(res, 0, 0));
	PQclear(res);

	res = pgut_execute(backup_conn,
					   "SELECT path,changed_block_number,changed_block_list "
					   "FROM pg_cbm_get_changed_block($1, $2)",
					   2, (const char **) params);
	pfree(params[0]);
	pfree(params[1]);

	if (PQnfields(res) == 0)
		elog(ERROR, "cannot get ptrack pagemapset");

	/* Initialize bitmap */
	pagemap.bitmap = NULL;
	pagemap.bitmapsize = 0;

	/* Construct database map */
	for (i = 0; i < PQntuples(res); i++)
	{
		page_map_entry *pm_entry = (page_map_entry *) pgut_malloc(sizeof(page_map_entry));

		/* get path */
		pm_entry->path = pgut_strdup(PQgetvalue(res, i, 0));

		ret = sscanf_s(PQgetvalue(res, i, 1), "%u", &blkcnt);
		securec_check_for_sscanf_s(ret, 1, "\0", "\0");

		if (blkcnt == 1) {
			ret = sscanf_s(PQgetvalue(res, i, 2), "%u", &blknum);
			securec_check_for_sscanf_s(ret, 1, "\0", "\0");
			datapagemap_add(&pagemap, blknum % ((BlockNumber) RELSEG_SIZE));
		} else {
			blocknum_str = strtok_r(PQgetvalue(res, i, 2), ", ", &saved);
			while (blocknum_str != NULL) {
				ret = sscanf_s(blocknum_str, "%u", &blknum);
				securec_check_for_sscanf_s(ret, 1, "\0", "\0");
				datapagemap_add(&pagemap, blknum % ((BlockNumber) RELSEG_SIZE));
				blocknum_str = strtok_r(NULL, ", ", &saved);
			}
		}

		pm_entry->pagemap = (char *)pagemap.bitmap;
		pm_entry->pagemapsize = pagemap.bitmapsize;

		pagemap.bitmap = NULL;
		pagemap.bitmapsize = 0;

		if (pagemapset == NULL)
			pagemapset = parray_new();

		parray_append(pagemapset, pm_entry);
	}

	PQclear(res);

	return pagemapset;
}

/*
 * Given a list of files in the instance to backup, build a pagemap for each
 * data file that has ptrack. Result is saved in the pagemap field of pgFile.
 *
 * We fetch a list of changed files with their ptrack maps.  After that files
 * are merged with their bitmaps.  File without bitmap is treated as unchanged.
 */
void
make_pagemap_from_ptrack(parray *files,
						 PGconn *backup_conn,
						 XLogRecPtr lsn)
{
	parray *filemaps;
	int		file_i = 0;
	page_map_entry *dummy_map = NULL;

	/* Receive all available ptrack bitmaps at once */
	filemaps = pg_ptrack_get_pagemapset(backup_conn, lsn);

	if (filemaps != NULL)
		parray_qsort(filemaps, pgFileMapComparePath);
	else
		return;

	dummy_map = (page_map_entry *) pgut_malloc(sizeof(page_map_entry));

	/* Iterate over files and look for corresponding pagemap if any */
	for (file_i = 0; file_i < parray_num(files); file_i++)
	{
		pgFile *file = (pgFile *) parray_get(files, file_i);
		page_map_entry **res_map = NULL;
		page_map_entry *map = NULL;

		/*
		 * For now nondata files are not entitled to have pagemap
		 * TODO It's possible to use ptrack for incremental backup of
		 * relation forks. Not implemented yet.
		 */
		if (!file->is_datafile || file->is_cfs)
			continue;

		/* Consider only files from PGDATA (this check is probably redundant) */
		if (file->external_dir_num != 0)
			continue;

		if (filemaps)
		{
			dummy_map->path = file->rel_path;
			res_map = (page_map_entry **)parray_bsearch(filemaps, dummy_map, pgFileMapComparePath);
			map = (res_map) ? *res_map : NULL;
		}

		/* Found map */
		if (map)
		{
			elog(VERBOSE, "Using ptrack pagemap for file \"%s\"", file->rel_path);
			file->pagemap.bitmapsize = map->pagemapsize;
			file->pagemap.bitmap = (unsigned char *)map->pagemap;
		}
	}

	free(dummy_map);
}
