/*-
 * See the file LICENSE for redistribution information.
 *
 * Copyright (c) 2008 WiredTiger Software.
 *	All rights reserved.
 *
 * $Id$
 */

#include "wt_internal.h"

#ifdef HAVE_DIAGNOSTIC
static void __wt_bt_debug_col_fix_indx(WT_TOC *, WT_COL_INDX *, FILE *);
static int  __wt_bt_debug_col_var_indx(WT_TOC *, WT_COL_INDX *, FILE *);
static void __wt_bt_debug_desc(WT_PAGE *, FILE *);
static int  __wt_bt_debug_item(WT_TOC *, WT_ITEM *, FILE *);
static int  __wt_bt_debug_item_data(WT_TOC *, WT_ITEM *, FILE *fp);
static void __wt_bt_debug_page_col_fix(DB *, WT_PAGE *, FILE *);
static void __wt_bt_debug_page_col_int(WT_PAGE *, FILE *);
static int  __wt_bt_debug_page_item(WT_TOC *, WT_PAGE *, FILE *);
static void __wt_bt_debug_repl(WT_SDBT *, FILE *);
static int  __wt_bt_debug_row_indx(WT_TOC *, WT_ROW_INDX *, FILE *);
static int  __wt_bt_debug_set_fp(const char *, FILE **, int *);

static int
__wt_bt_debug_set_fp(const char *ofile, FILE **fpp, int *close_varp)
{
	FILE *fp;

	*close_varp = 0;

	/* If we were giving a stream, use it. */
	if ((fp = *fpp) != NULL)
		return (0);

	/* If we were given a file, use it. */
	if (ofile != NULL) {
		if ((fp = fopen(ofile, "w")) == NULL)
			return (WT_ERROR);
		*fpp = fp;
		*close_varp = 1;
		return (0);
	}

	/* Default to stdout. */
	*fpp = stdout;
	return (0);
}

/*
 * __wt_bt_debug_dump --
 *	Dump a database in debugging mode.
 */
int
__wt_bt_debug_dump(WT_TOC *toc, char *ofile, FILE *fp)
{
	int do_close, ret;

	WT_RET(__wt_bt_debug_set_fp(ofile, &fp, &do_close));

	/*
	 * We use the verification code to do debugging dumps because if we're
	 * dumping in debugging mode, we want to confirm the page is OK before
	 * walking it.
	 */
	ret = __wt_bt_verify(toc, NULL, fp);

	if (do_close)
		(void)fclose(fp);

	return (ret);
}

/*
 * __wt_bt_debug_addr --
 *	Dump a page in debugging mode based on its addr/size.
 */
int
__wt_bt_debug_addr(
    WT_TOC *toc, u_int32_t addr, u_int32_t size, char *ofile, FILE *fp)
{
	WT_PAGE *page;
	int ret;

	/*
	 * Addr/size were set by our caller (probably via some debugger).   If
	 * WT_RESTART is returned, retry, presumably addr/size are valid and
	 * the page was only discarded, not re-written.
	 */
	WT_RET_RESTART(__wt_bt_page_in(toc, addr, size, 1, &page));
	ret = __wt_bt_debug_page(toc, page, ofile, fp);
	__wt_bt_page_out(toc, &page, 0);
	return (ret);
}

/*
 * __wt_bt_debug_page --
 *	Dump a page in debugging mode.
 */
int
__wt_bt_debug_page(WT_TOC *toc, WT_PAGE *page, char *ofile, FILE *fp)
{
	DB *db;
	WT_PAGE_HDR *hdr;
	int do_close, ret;

	db = toc->db;
	hdr = page->hdr;
	ret = 0;

	WT_RET(__wt_bt_debug_set_fp(ofile, &fp, &do_close));

	fprintf(fp, "addr: %lu-%lu {\n", (u_long)page->addr,
	    (u_long)page->addr + (WT_OFF_TO_ADDR(db, page->size) - 1));

	fprintf(fp, "\taddr %lu, size %lu, lsn %lu/%lu\n",
	    (u_long)page->addr, (u_long)page->size,
	    (u_long)hdr->lsn[0], (u_long)hdr->lsn[1]);

	fprintf(fp, "\t%s: ", __wt_bt_hdr_type(hdr));
	if (hdr->type == WT_PAGE_OVFL)
		fprintf(fp, "bytes %lu\n", (u_long)hdr->u.datalen);
	else {
		fprintf(fp, "level %lu, entries %lu, records %llu\n",
		    (u_long)hdr->level, (u_long)hdr->u.entries, page->records);
		fprintf(fp, "\tfirst-free %#lx, space avail %lu\n",
		    (u_long)page->first_free, (u_long)page->space_avail);
	}
	fprintf(fp, "\n");

	switch (hdr->type) {
	case WT_PAGE_DESCRIPT:
		__wt_bt_debug_desc(page, fp);
		break;
	case WT_PAGE_COL_VAR:
	case WT_PAGE_DUP_INT:
	case WT_PAGE_DUP_LEAF:
	case WT_PAGE_ROW_INT:
	case WT_PAGE_ROW_LEAF:
		ret = __wt_bt_debug_page_item(toc, page, fp);
		break;
	case WT_PAGE_COL_FIX:
		__wt_bt_debug_page_col_fix(db, page, fp);
		break;
	case WT_PAGE_COL_INT:
		__wt_bt_debug_page_col_int(page, fp);
		break;
	case WT_PAGE_OVFL:
		break;
	WT_ILLEGAL_FORMAT(db);
	}

	fprintf(fp, "}\n");

	if (do_close)
		(void)fclose(fp);

	return (ret);
}

/*
 * __wt_bt_debug_desc --
 *	Dump the database description on page 0.
 */
static void
__wt_bt_debug_desc(WT_PAGE *page, FILE *fp)
{
	WT_PAGE_DESC *desc;

	if (fp == NULL)				/* Default to stderr */
		fp = stderr;

	desc = (WT_PAGE_DESC *)WT_PAGE_BYTE(page);
	fprintf(fp, "\tdescription record: {\n"),
	fprintf(fp, "\t\tmagic: %lu, major: %lu, minor: %lu\n",
	    (u_long)desc->magic, (u_long)desc->majorv, (u_long)desc->minorv);
	fprintf(fp, "\t\tinternal page min/max size: %lu/%lu\n",
	    (u_long)desc->intlmin, (u_long)desc->intlmax);
	fprintf(fp, "\t\tleaf page min/max size: %lu/%lu\n",
	    (u_long)desc->leafmin, (u_long)desc->leafmax);
	fprintf(fp, "\t\tbase record: %llu, fixed_len: %lu\n",
	    (u_quad)desc->base_recno, (u_long)desc->fixed_len);
	if (desc->root_addr == WT_ADDR_INVALID)
		fprintf(fp, "\t\troot addr (none)\n");
	else
		fprintf(fp, "\t\troot addr %lu, size %lu\n",
		    (u_long)desc->root_addr, (u_long)desc->root_size);
	if (desc->free_addr == WT_ADDR_INVALID)
		fprintf(fp, "\t\tfree addr (none)\n");
	else
		fprintf(fp, "\t\tfree addr %lu, size %lu\n",
		    (u_long)desc->free_addr, (u_long)desc->free_size);
	fprintf(fp, "\t}\n");
}

/*
 * __wt_bt_debug_inmem --
 *	Dump the in-memory information for a page.
 */
int
__wt_bt_debug_inmem(WT_TOC *toc, WT_PAGE *page, char *ofile, FILE *fp)
{
	DB *db;
	WT_COL_INDX *cip;
	WT_ROW_INDX *rip;
	u_int32_t i;
	int do_close;

	db = toc->db;

	WT_RET(__wt_bt_debug_set_fp(ofile, &fp, &do_close));

	fprintf(fp, "addr: %lu-%lu {\n", (u_long)page->addr,
	    (u_long)page->addr + (WT_OFF_TO_ADDR(db, page->size) - 1));

	/* Dump the WT_{ROW,COL}_INDX array. */
	switch (page->hdr->type) {
	case WT_PAGE_DUP_INT:
	case WT_PAGE_DUP_LEAF:
	case WT_PAGE_ROW_INT:
	case WT_PAGE_ROW_LEAF:
		WT_INDX_FOREACH(page, rip, i)
			WT_RET(__wt_bt_debug_row_indx(toc, rip, fp));
		break;
	case WT_PAGE_COL_FIX:
		WT_INDX_FOREACH(page, cip, i)
			__wt_bt_debug_col_fix_indx(toc, cip, fp);
		break;
	case WT_PAGE_COL_INT:
	case WT_PAGE_COL_VAR:
		WT_INDX_FOREACH(page, cip, i)
			WT_RET(__wt_bt_debug_col_var_indx(toc, cip, fp));
		break;
	case WT_PAGE_OVFL:
		break;
	WT_ILLEGAL_FORMAT(db);
	}

	fprintf(fp, "}\n");

	if (do_close)
		(void)fclose(fp);

	return (0);
}

/*
 * __wt_bt_debug_col_var_indx --
 *	Dump a single WT_COL_INDX structure for a WT_PAGE_COL_{INT,VAR} page.
 */
static int
__wt_bt_debug_col_var_indx(WT_TOC *toc, WT_COL_INDX *cip, FILE *fp)
{
	if (fp == NULL)				/* Default to stderr */
		fp = stderr;

	if (cip->repl != NULL)
		__wt_bt_debug_repl(cip->repl, fp);
	else {
		fprintf(fp, "\tdata {");
		WT_RET(__wt_bt_debug_item_data(toc, cip->data, fp));
		fprintf(fp, "}\n");
	}
	return (0);
}

/*
 * __wt_bt_debug_col_fix_indx --
 *	Dump a single WT_COL_INDX structure for a WT_PAGE_COL_FIX page.
 */
static void
__wt_bt_debug_col_fix_indx(WT_TOC *toc, WT_COL_INDX *cip, FILE *fp)
{
	DB *db;
	IDB *idb;

	db = toc->db;
	idb = db->idb;

	if (fp == NULL)				/* Default to stderr */
		fp = stderr;

	if (cip->repl != NULL)
		__wt_bt_debug_repl(cip->repl, fp);
	else if (F_ISSET(idb, WT_REPEAT_COMP)) {
		fprintf(fp,
		    "\trepeat %lu {", (u_long)WT_FIX_REPEAT_COUNT(cip->data));
		__wt_bt_print(WT_FIX_REPEAT_DATA(cip->data), db->fixed_len, fp);
		fprintf(fp, "}\n");
	} else {
		fprintf(fp, "\tdata {");
		__wt_bt_print(cip->data, db->fixed_len, fp);
		fprintf(fp, "}\n");
	}
}

/*
 * __wt_bt_debug_row_indx --
 *	Dump a single WT_ROW_INDX structure.
 */
static int
__wt_bt_debug_row_indx(WT_TOC *toc, WT_ROW_INDX *rip, FILE *fp)
{
	if (fp == NULL)				/* Default to stderr */
		fp = stderr;

	if (WT_KEY_PROCESS(rip))
		fprintf(fp, "\tkey {requires processing}\n");
	else
		__wt_bt_debug_dbt("\tkey", rip, fp);
	if (rip->repl != NULL)
		__wt_bt_debug_repl(rip->repl, fp);
	else {
		fprintf(fp, "\tdata {");
		WT_RET(__wt_bt_debug_item_data(toc, rip->data, fp));
		fprintf(fp, "}\n");
	}
	return (0);
}

/*
 * __wt_bt_debug_repl --
 *	Dump a replacement array.
 */
static void
__wt_bt_debug_repl(WT_SDBT *repl, FILE *fp)
{
	u_int i;

	if (fp == NULL)				/* Default to stderr */
		fp = stderr;

	while (repl != NULL) {
		for (i = 0; i < WT_SDBT_CHUNK; ++i, ++repl) {
			if (repl->data == NULL)
				continue;
			if (WT_SDBT_DELETED_ISSET(repl->data)) {
				fprintf(fp, "\trepl: {deleted}\n");
				continue;
			}
			__wt_bt_debug_dbt("\trepl", repl, fp);
		}
		/*
		 * The last slot in the array is fake -- if it's non-NULL,
		 * it points to a previous array which we also walk.
		 */
		repl = repl->data == NULL ? NULL : (WT_SDBT *)repl->data;
	}
}

/*
 * __wt_bt_debug_page_item --
 *	Dump a page of WT_ITEM's.
 */
static int
__wt_bt_debug_page_item(WT_TOC *toc, WT_PAGE *page, FILE *fp)
{
	WT_ITEM *item;
	u_int32_t i;

	if (fp == NULL)				/* Default to stderr */
		fp = stderr;

	WT_ITEM_FOREACH(page, item, i)
		WT_RET(__wt_bt_debug_item(toc, item, fp));
	return (0);
}

/*
 * __wt_bt_debug_item --
 *	Dump a single WT_ITEM.
 */
static int
__wt_bt_debug_item(WT_TOC *toc, WT_ITEM *item, FILE *fp)
{
	DB *db;
	WT_OFF *off;
	WT_OVFL *ovfl;

	if (fp == NULL)				/* Default to stderr */
		fp = stderr;

	db = toc->db;

	fprintf(fp, "\ttype %s, len %lu",
	    __wt_bt_item_type(item), (u_long)WT_ITEM_LEN(item));

	switch (WT_ITEM_TYPE(item)) {
	case WT_ITEM_KEY:
	case WT_ITEM_DATA:
	case WT_ITEM_DEL:
	case WT_ITEM_DUP:
		break;
	case WT_ITEM_KEY_OVFL:
	case WT_ITEM_DATA_OVFL:
	case WT_ITEM_DUP_OVFL:
		ovfl = WT_ITEM_BYTE_OVFL(item);
		fprintf(fp, ", addr %lu, size %lu",
		    (u_long)ovfl->addr, (u_long)ovfl->size);
		break;
	case WT_ITEM_OFF:
		off = WT_ITEM_BYTE_OFF(item);
		fprintf(fp, ", addr %lu, size %lu, records %llu\n",
		    (u_long)off->addr, (u_long)off->size, WT_RECORDS(off));
		return (0);
	WT_ILLEGAL_FORMAT(db);
	}

	fprintf(fp, "\n\t{");
	WT_RET(__wt_bt_debug_item_data(toc, item, fp));
	fprintf(fp, "}\n");
	return (0);
}

/*
 * __wt_bt_debug_page_col_int --
 *	Dump a WT_PAGE_COL_INT page.
 */
static void
__wt_bt_debug_page_col_int(WT_PAGE *page, FILE *fp)
{
	WT_OFF *off;
	u_int32_t i;

	if (fp == NULL)				/* Default to stderr */
		fp = stderr;

	WT_OFF_FOREACH(page, off, i)
		fprintf(fp, "\toffpage,  addr %lu, size %lu, records %llu\n",
		    (u_long)off->addr,
		    (u_long)off->size, (u_quad)WT_RECORDS(off));
}

/*
 * __wt_bt_debug_page_col_fix --
 *	Dump a WT_PAGE_COL_FIX page.
 */
static void
__wt_bt_debug_page_col_fix(DB *db, WT_PAGE *page, FILE *fp)
{
	IDB *idb;
	u_int32_t i;
	u_int8_t *p;

	if (fp == NULL)				/* Default to stderr */
		fp = stderr;

	idb = db->idb;

	if (F_ISSET(idb, WT_REPEAT_COMP))
		WT_FIX_REPEAT_FOREACH(db, page, p, i) {
			fprintf(fp, "\trepeat %lu {",
			    (u_long)WT_FIX_REPEAT_COUNT(p));
			if (WT_FIX_DELETE_ISSET(WT_FIX_REPEAT_DATA(p)))
				fprintf(fp, "deleted");
			else
				__wt_bt_print(
				    WT_FIX_REPEAT_DATA(p), db->fixed_len, fp);
			fprintf(fp, "}\n");
		}
	else
		WT_FIX_FOREACH(db, page, p, i) {
			fprintf(fp, "\t{");
			if (WT_FIX_DELETE_ISSET(p))
				fprintf(fp, "deleted");
			else
				__wt_bt_print(p, db->fixed_len, fp);
			fprintf(fp, "}\n");
		}
}

/*
 * __wt_bt_debug_item_data --
 *	Dump a single item's data in debugging mode.
 */
static int
__wt_bt_debug_item_data(WT_TOC *toc, WT_ITEM *item, FILE *fp)
{
	DB *db;
	IDB *idb;
	WT_OVFL *ovfl;
	WT_PAGE *page;
	u_int32_t size;
	u_int8_t *p;
	void *hp;
	int ret;

	if (fp == NULL)				/* Default to stderr */
		fp = stderr;

	db = toc->db;
	idb = db->idb;
	page = NULL;
	hp = NULL;
	ret = 0;

	switch (WT_ITEM_TYPE(item)) {
	case WT_ITEM_KEY:
		hp = idb->huffman_key;
		p = WT_ITEM_BYTE(item);
		size = WT_ITEM_LEN(item);
		break;
	case WT_ITEM_DATA:
	case WT_ITEM_DUP:
		hp = idb->huffman_data;
		p = WT_ITEM_BYTE(item);
		size = WT_ITEM_LEN(item);
		break;
	case WT_ITEM_DEL:
		hp = NULL;
		p = (u_int8_t *)"deleted";
		size = sizeof("deleted" - 1);
		break;
	case WT_ITEM_KEY_OVFL:
	case WT_ITEM_DATA_OVFL:
	case WT_ITEM_DUP_OVFL:
		ovfl = WT_ITEM_BYTE_OVFL(item);
		WT_ERR(__wt_bt_ovfl_in(toc, ovfl, &page));
		hp = idb->huffman_data;
		p = WT_PAGE_BYTE(page);
		size = ovfl->size;
		break;
	case WT_ITEM_OFF:
		return (0);
	WT_ILLEGAL_FORMAT(db);
	}

	/* Uncompress the item as necessary. */
	if (hp != NULL) {
		WT_ERR(__wt_huffman_decode(hp, p, size,
		    &toc->tmp1.data, &toc->tmp1.mem_size, &toc->tmp1.size));
		p = toc->tmp1.data;
		size = toc->tmp1.size;
	}

	__wt_bt_print(p, size, fp);

err:	if (page != NULL)
		__wt_bt_page_out(toc, &page, 0);
	return (ret);
}

/*
 * __wt_bt_debug_dbt --
 *	Dump a single DBT in debugging mode, with an optional tag.
 */
void
__wt_bt_debug_dbt(const char *tag, void *arg_dbt, FILE *fp)
{
	DBT *dbt;

	if (fp == NULL)				/* Default to stderr */
		fp = stderr;

	/*
	 * The argument isn't necessarily a DBT structure, but the first two
	 * fields of the argument are always a void *data/u_int32_t size pair.
	 */
	dbt = arg_dbt;

	if (tag != NULL)
		fprintf(fp, "%s: ", tag);
	fprintf(fp, "%lu {",  (u_long)dbt->size);
	__wt_bt_print(dbt->data, dbt->size, fp);
	fprintf(fp, "}\n");
}
#endif
