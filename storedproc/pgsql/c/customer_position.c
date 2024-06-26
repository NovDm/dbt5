/*
 * This file is released under the terms of the Artistic License.  Please see
 * the file LICENSE, included in this package, for details.
 *
 * Copyright The DBT-5 Authors
 *
 * Based on TPC-E Standard Specification Revision 1.14.0.
 */

#include <sys/types.h>
#include <unistd.h>
#include <inttypes.h>
#include <postgres.h>
#include <fmgr.h>
#include <executor/spi.h> /* this should include most necessary APIs */
#include <executor/executor.h> /* for GetAttributeByName() */
#include <funcapi.h> /* for returning set of rows */
#include <utils/datetime.h>
#include <utils/builtins.h>
#include <catalog/pg_type.h>

#include "frame.h"
#include "dbt5common.h"

#ifdef PG_MODULE_MAGIC
PG_MODULE_MAGIC;
#endif

#define SQLCPF1_1                                                             \
	"SELECT c_id\n"                                                           \
	"FROM   customer\n"                                                       \
	"WHERE  c_tax_id = $1"

#define SQLCPF1_2                                                             \
	"SELECT c_st_id\n"                                                        \
	"     , c_l_name\n"                                                       \
	"     , c_f_name\n"                                                       \
	"     , c_m_name\n"                                                       \
	"     , c_gndr\n"                                                         \
	"     , c_tier\n"                                                         \
	"     , c_dob\n"                                                          \
	"     , c_ad_id\n"                                                        \
	"     , c_ctry_1\n"                                                       \
	"     , c_area_1\n"                                                       \
	"     , c_local_1\n"                                                      \
	"     , c_ext_1\n"                                                        \
	"     , c_ctry_2\n"                                                       \
	"     , c_area_2\n"                                                       \
	"     , c_local_2\n"                                                      \
	"     , c_ext_2\n"                                                        \
	"     , c_ctry_3\n"                                                       \
	"     , c_area_3\n"                                                       \
	"     , c_local_3\n"                                                      \
	"     , c_ext_3\n"                                                        \
	"     , c_email_1\n"                                                      \
	"     , c_email_2\n"                                                      \
	"FROM customer\n"                                                         \
	"WHERE c_id = $1"

#define SQLCPF1_3                                                             \
	"SELECT ca_id\n"                                                          \
	"    ,  ca_bal\n"                                                         \
	"    ,  coalesce(sum(hs_qty * lt_price), 0) AS soma\n"                    \
	"FROM customer_account\n"                                                 \
	"     LEFT OUTER JOIN holding_summary\n"                                  \
	"                  ON hs_ca_id = ca_id,\n"                                \
	"     last_trade\n"                                                       \
	"WHERE ca_c_id = $1\n"                                                    \
	" AND lt_s_symb = hs_s_symb\n"                                            \
	"GROUP BY ca_id, ca_bal\n"                                                \
	"ORDER BY 3 ASC\n"                                                        \
	"LIMIT 10"

#define SQLCPF2_1                                                             \
	"SELECT t_id\n"                                                           \
	"     , t_s_symb\n"                                                       \
	"     , t_qty\n"                                                          \
	"     , st_name\n"                                                        \
	"     , th_dts\n"                                                         \
	"FROM (\n"                                                                \
	"         SELECT t_id AS id\n"                                            \
	"         FROM trade\n"                                                   \
	"         WHERE t_ca_id = $1\n"                                           \
	"         ORDER BY t_dts DESC\n"                                          \
	"         LIMIT 10\n"                                                     \
	"     ) AS t\n"                                                           \
	"   , trade\n"                                                            \
	"   , trade_history\n"                                                    \
	"   , status_type\n"                                                      \
	"WHERE t_id = id\n"                                                       \
	"  AND th_t_id = t_id\n"                                                  \
	"  AND st_id = th_st_id\n"                                                \
	"ORDER BY th_dts DESC"

#define CPF1_1 CPF1_statements[0].plan
#define CPF1_2 CPF1_statements[1].plan
#define CPF1_3 CPF1_statements[2].plan

#define CPF2_1 CPF2_statements[0].plan

static cached_statement CPF1_statements[] = {

	{ SQLCPF1_1, 1, { TEXTOID } },

	{ SQLCPF1_2, 1, { INT8OID } },

	{ SQLCPF1_3, 1, { INT8OID } },

	{ NULL }
};

static cached_statement CPF2_statements[] = {

	{ SQLCPF2_1, 1, { INT8OID } },

	{ NULL }
};

/* Prototypes to prevent potential gcc warnings. */
Datum CustomerPositionFrame1(PG_FUNCTION_ARGS);
Datum CustomerPositionFrame2(PG_FUNCTION_ARGS);

PG_FUNCTION_INFO_V1(CustomerPositionFrame1);
PG_FUNCTION_INFO_V1(CustomerPositionFrame2);

#ifdef DEBUG
void dump_cpf1_inputs(long, text *);
void dump_cpf2_inputs(long);

void
dump_cpf1_inputs(long cust_id, text *tax_id_p)
{
	elog(DEBUG1, "CPF1: INPUTS START");
	elog(DEBUG1, "CPF1: cust_id %ld", cust_id);
	elog(DEBUG1, "CPF1: tax_id %s",
			DatumGetCString(
					DirectFunctionCall1(textout, PointerGetDatum(tax_id_p))));
	elog(DEBUG1, "CPF1: INPUTS END");
}

void
dump_cpf2_inputs(long acct_id)
{
	elog(DEBUG1, "CPF2: INPUTS START");
	elog(DEBUG1, "CPF2: acct_id %ld", acct_id);
	elog(DEBUG1, "CPF2: INPUTS END");
}
#endif /* DEBUG */

/* Clause 3.3.2.3 */
Datum
CustomerPositionFrame1(PG_FUNCTION_ARGS)
{
	FuncCallContext *funcctx;
	int i;
	char **values = NULL;

	int64 cust_id;

	/* stuff done only on the first call of the function */
	if (SRF_IS_FIRSTCALL()) {
		MemoryContext oldcontext;

		text *tax_id_p;

		enum cpf1
		{
			i_cust_id = 0,
			i_acct_id,
			i_acct_len,
			i_asset_total,
			i_c_ad_id,
			i_c_area_1,
			i_c_area_2,
			i_c_area_3,
			i_c_ctry_1,
			i_c_ctry_2,
			i_c_ctry_3,
			i_c_dob,
			i_c_email_1,
			i_c_email_2,
			i_c_ext_1,
			i_c_ext_2,
			i_c_ext_3,
			i_c_f_name,
			i_c_gndr,
			i_c_l_name,
			i_c_local_1,
			i_c_local_2,
			i_c_local_3,
			i_c_m_name,
			i_c_st_id,
			i_c_tier,
			i_cash_bal
		};

		int ret;
		Datum args[1];
		char nulls[1] = { ' ' };
		TupleDesc tupdesc;
		SPITupleTable *tuptable = NULL;
		HeapTuple tuple;

		/*
		 * Prepare a values array for building the returned tuple.
		 * This should be an array of C strings, which will
		 * be processed later by the type input functions.
		 */
		values = (char **) palloc(sizeof(char *) * 27);
		values[i_cust_id] = (char *) palloc((IDENT_T_LEN + 1) * sizeof(char));
		values[i_acct_len] = (char *) palloc((INTEGER_LEN + 1) * sizeof(char));

		/* Create a function context for cross-call persistence. */
		funcctx = SRF_FIRSTCALL_INIT();

		/* Switch to memory context appropriate for multiple function calls. */
		oldcontext = MemoryContextSwitchTo(funcctx->multi_call_memory_ctx);

		cust_id = PG_GETARG_INT64(0);
		tax_id_p = PG_GETARG_TEXT_P(1);
#ifdef DEBUG
		dump_cpf1_inputs(cust_id, tax_id_p);
#endif /* DEBUG */

		SPI_connect();
		plan_queries(CPF1_statements);
		if (cust_id == 0) {
#ifdef DEBUG
			elog(DEBUG1, "%s", SQLCPF1_1);
#endif /* DEBUG */
			args[0] = PointerGetDatum(tax_id_p);
			ret = SPI_execute_plan(CPF1_1, args, nulls, true, 0);
			if (ret == SPI_OK_SELECT && SPI_processed > 0) {
				tupdesc = SPI_tuptable->tupdesc;
				tuptable = SPI_tuptable;
				tuple = tuptable->vals[0];
				cust_id = atoll(SPI_getvalue(tuple, tupdesc, 1));
#ifdef DEBUG
				elog(DEBUG1, "Got cust_id ok: %ld", cust_id);
#endif /* DEBUG */
			} else {
				FAIL_FRAME_SET(&funcctx->max_calls, CPF1_statements[0].sql);
			}
		}

#ifdef DEBUG
		elog(DEBUG1, "%s", SQLCPF1_2);
#endif /* DEBUG */
		args[0] = Int64GetDatum(cust_id);
		ret = SPI_execute_plan(CPF1_2, args, nulls, true, 0);
#ifdef DEBUG
		elog(DEBUG1, "%ld row(s) returned from CPF1_2.", SPI_processed);
#endif /* DEBUG */
		if (ret == SPI_OK_SELECT && SPI_processed > 0) {
			tupdesc = SPI_tuptable->tupdesc;
			tuptable = SPI_tuptable;
			tuple = tuptable->vals[0];

			values[i_c_st_id] = SPI_getvalue(tuple, tupdesc, 1);
			values[i_c_l_name] = SPI_getvalue(tuple, tupdesc, 2);
			values[i_c_f_name] = SPI_getvalue(tuple, tupdesc, 3);
			values[i_c_m_name] = SPI_getvalue(tuple, tupdesc, 4);
			values[i_c_gndr] = SPI_getvalue(tuple, tupdesc, 5);
			values[i_c_tier] = SPI_getvalue(tuple, tupdesc, 6);
			values[i_c_dob] = SPI_getvalue(tuple, tupdesc, 7);
			values[i_c_ad_id] = SPI_getvalue(tuple, tupdesc, 8);
			values[i_c_ctry_1] = SPI_getvalue(tuple, tupdesc, 9);
			values[i_c_area_1] = SPI_getvalue(tuple, tupdesc, 10);
			values[i_c_local_1] = SPI_getvalue(tuple, tupdesc, 11);
			values[i_c_ext_1] = SPI_getvalue(tuple, tupdesc, 12);
			values[i_c_ctry_2] = SPI_getvalue(tuple, tupdesc, 13);
			values[i_c_area_2] = SPI_getvalue(tuple, tupdesc, 14);
			values[i_c_local_2] = SPI_getvalue(tuple, tupdesc, 15);
			values[i_c_ext_2] = SPI_getvalue(tuple, tupdesc, 16);
			values[i_c_ctry_3] = SPI_getvalue(tuple, tupdesc, 17);
			values[i_c_area_3] = SPI_getvalue(tuple, tupdesc, 18);
			values[i_c_local_3] = SPI_getvalue(tuple, tupdesc, 19);
			values[i_c_ext_3] = SPI_getvalue(tuple, tupdesc, 20);
			values[i_c_email_1] = SPI_getvalue(tuple, tupdesc, 21);
			values[i_c_email_2] = SPI_getvalue(tuple, tupdesc, 22);
		} else {
			FAIL_FRAME_SET(&funcctx->max_calls, CPF1_statements[1].sql);
		}
#ifdef DEBUG
		elog(DEBUG1, "%s", SQLCPF1_3);
#endif /* DEBUG */
		args[0] = Int64GetDatum(cust_id);
		ret = SPI_execute_plan(CPF1_3, args, nulls, true, 0);
		snprintf(values[i_acct_len], BIGINT_LEN, "%" PRId64, SPI_processed);
#ifdef DEBUG
		elog(DEBUG1, "%ld row(s) returned from CPF1_3.", SPI_processed);
#endif /* DEBUG */
		if (ret == SPI_OK_SELECT && SPI_processed > 0) {
			char *tmp;
			int length_ai, length_at, length_cb;

			/* Total number of tuples to be returned. */
			funcctx->max_calls = 1;

			tupdesc = SPI_tuptable->tupdesc;
			tuptable = SPI_tuptable;
			tuple = tuptable->vals[0];

			length_ai = (BIGINT_LEN + 1) * (SPI_processed + 1) + 2;
			values[i_acct_id] = (char *) palloc(length_ai-- * sizeof(char));

			length_cb = (BALANCE_T_LEN + 1) * (SPI_processed + 1) + 2;
			values[i_cash_bal] = (char *) palloc(length_cb-- * sizeof(char));

			length_at = (S_PRICE_T_LEN + 1) * (SPI_processed + 1) + 2;
			values[i_asset_total]
					= (char *) palloc(length_at-- * sizeof(char));

			values[i_acct_id][0] = '{';
			values[i_acct_id][1] = '\0';

			values[i_cash_bal][0] = '{';
			values[i_cash_bal][1] = '\0';

			values[i_asset_total][0] = '{';
			values[i_asset_total][1] = '\0';

			if (SPI_processed > 0) {
				tmp = SPI_getvalue(tuple, tupdesc, 1);
				strncat(values[i_acct_id], tmp, length_ai);
				length_ai -= strlen(tmp);
				if (length_ai < 0) {
					FAIL_FRAME("acct_id values needs to be increased");
				}

				tmp = SPI_getvalue(tuple, tupdesc, 2);
				strncat(values[i_cash_bal], tmp, length_cb);
				length_cb -= strlen(tmp);
				if (length_cb < 0) {
					FAIL_FRAME("cash_bal values needs to be increased");
				}

				tmp = SPI_getvalue(tuple, tupdesc, 3);
				strncat(values[i_asset_total], tmp, length_at);
				length_at -= strlen(tmp);
				if (length_at < 0) {
					FAIL_FRAME("asset_total values needs to be increased");
				}
			}
			for (i = 1; i < SPI_processed; i++) {
				tuple = tuptable->vals[i];

				strncat(values[i_acct_id], ",", length_ai--);
				if (length_ai < 0) {
					FAIL_FRAME("acct_id values needs to be increased");
				}

				tmp = SPI_getvalue(tuple, tupdesc, 1);
				strncat(values[i_acct_id], tmp, length_ai);
				length_ai -= strlen(tmp);
				if (length_ai < 0) {
					FAIL_FRAME("account_id values needs to be increased");
				}

				strncat(values[i_cash_bal], ",", length_cb--);
				if (length_cb < 0) {
					FAIL_FRAME("cash_bal values needs to be increased");
				}

				tmp = SPI_getvalue(tuple, tupdesc, 2);
				strncat(values[i_cash_bal], tmp, length_cb);
				length_cb -= strlen(tmp);
				if (length_cb < 0) {
					FAIL_FRAME("cash_bal values needs to be increased");
				}

				strncat(values[i_asset_total], ",", length_at--);
				if (length_at < 0) {
					FAIL_FRAME("asset_total values needs to be increased");
				}

				tmp = SPI_getvalue(tuple, tupdesc, 3);
				if (tmp != NULL) {
					strncat(values[i_asset_total], tmp, length_at);
					length_at -= strlen(tmp);
					if (length_at < 0) {
						FAIL_FRAME("asset_total values needs to be increased");
					}
				} else {
					strncat(values[i_asset_total], "0.00", length_at);
					length_at -= 4;
					if (length_at < 0) {
						FAIL_FRAME("asset_total values needs to be increased");
					}
				}
			}
			strncat(values[i_acct_id], "}", length_ai--);
			if (length_ai < 0) {
				FAIL_FRAME("account_id values needs to be increased");
			}

			strncat(values[i_cash_bal], "}", length_cb--);
			if (length_cb < 0) {
				FAIL_FRAME("cash_bal values needs to be increased");
			}

			strncat(values[i_asset_total], "}", length_at--);
			if (length_at < 0) {
				FAIL_FRAME("asset_total values needs to be increased");
			}
		} else {
			FAIL_FRAME_SET(&funcctx->max_calls, CPF1_statements[2].sql);
		}
		snprintf(values[i_cust_id], 12, "%" PRId64, cust_id);

		/* Build a tuple descriptor for our result type. */
		if (get_call_result_type(fcinfo, NULL, &tupdesc)
				!= TYPEFUNC_COMPOSITE) {
			ereport(ERROR, (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
								   errmsg("function returning record called "
										  "in context "
										  "that cannot accept type record")));
		}

		/*
		 * Generate attribute metadata needed later to produce tuples from raw
		 * C strings.
		 */
		funcctx->attinmeta = TupleDescGetAttInMetadata(tupdesc);

		/* save SPI data for use across calls */
		funcctx->user_fctx = tuptable;

		MemoryContextSwitchTo(oldcontext);
	}

	/* Stuff done on every call of the function. */
	funcctx = SRF_PERCALL_SETUP();

	if (funcctx->call_cntr < funcctx->max_calls) {
		Datum result;
		HeapTuple tuple;

#ifdef DEBUG
		for (i = 0; i < 27; i++) {
			elog(DEBUG1, "CPF1 OUT: %d '%s'", i, values[i]);
		}
#endif /* DEBUG */

		/* Build a tuple. */
		tuple = BuildTupleFromCStrings(funcctx->attinmeta, values);

		/* Make the tuple into a datum. */
		result = HeapTupleGetDatum(tuple);

		SRF_RETURN_NEXT(funcctx, result);
	} else {
		SPI_finish();
		SRF_RETURN_DONE(funcctx);
	}
}

/* Clause 3.3.2.4 */
Datum
CustomerPositionFrame2(PG_FUNCTION_ARGS)
{
	FuncCallContext *funcctx;
	char **values = NULL;
	int i;

	if (SRF_IS_FIRSTCALL()) {
		MemoryContext oldcontext;

		TupleDesc tupdesc;
		SPITupleTable *tuptable = NULL;
		HeapTuple tuple;

		int64 acct_id = PG_GETARG_INT64(0);

		enum cpf2
		{
			i_hist_dts = 0,
			i_hist_len,
			i_qty,
			i_symbol,
			i_trade_id,
			i_trade_status
		};

		int ret;
		Datum args[1];
		char nulls[] = { ' ' };
#ifdef DEBUG
		dump_cpf2_inputs(acct_id);
#endif /* DEBUG */

		/*
		 * Prepare a values array for building the returned tuple.
		 * This should be an array of C strings, which will
		 * be processed later by the type input functions.
		 */
		values = (char **) palloc(sizeof(char *) * 6);
		values[i_hist_len] = (char *) palloc((INTEGER_LEN + 1) * sizeof(char));

		/* Create a function context for cross-call persistence. */
		funcctx = SRF_FIRSTCALL_INIT();
		funcctx->max_calls = 1;

		/* Switch to memory context appropriate for multiple function calls. */
		oldcontext = MemoryContextSwitchTo(funcctx->multi_call_memory_ctx);

		SPI_connect();
		plan_queries(CPF2_statements);
#ifdef DEBUG
		elog(DEBUG1, "%s", SQLCPF2_1);
#endif /* DEBUG */
		args[0] = Int64GetDatum(acct_id);
		ret = SPI_execute_plan(CPF2_1, args, nulls, true, 0);
		snprintf(values[i_hist_len], BIGINT_LEN, "%" PRId64, SPI_processed);
#ifdef DEBUG
		elog(DEBUG1, "%ld row(s) returned.", SPI_processed);
#endif /* DEBUG */
		/* Should return 1 to rows. */
		if (ret == SPI_OK_SELECT && SPI_processed > 0) {
			char *tmp;
			int length_hd, length_q, length_s, length_ti, length_ts;

			/* Total number of tuples to be returned. */
			funcctx->max_calls = 1;

			tupdesc = SPI_tuptable->tupdesc;
			tuptable = SPI_tuptable;
			tuple = tuptable->vals[0];

			length_hd = (MAXDATELEN + 1) * SPI_processed + 3;
			values[i_hist_dts] = (char *) palloc(length_hd-- * sizeof(char));

			length_q = (INTEGER_LEN + 1) * SPI_processed + 3;
			values[i_qty] = (char *) palloc(length_q-- * sizeof(char));

			length_s = (S_SYMB_LEN + 3) * SPI_processed + 3;
			values[i_symbol] = (char *) palloc(length_s-- * sizeof(char));

			length_ti = (IDENT_T_LEN + 1) * SPI_processed + 3;
			values[i_trade_id] = (char *) palloc(length_ti-- * sizeof(char));

			length_ts = (ST_NAME_LEN + 3) * SPI_processed + 3;
			values[i_trade_status]
					= (char *) palloc(length_ts-- * sizeof(char));

			values[i_hist_dts][0] = '{';
			values[i_hist_dts][1] = '\0';

			values[i_qty][0] = '{';
			values[i_qty][1] = '\0';

			values[i_symbol][0] = '{';
			values[i_symbol][1] = '\0';

			values[i_trade_id][0] = '{';
			values[i_trade_id][1] = '\0';

			values[i_trade_status][0] = '{';
			values[i_trade_status][1] = '\0';

			tmp = SPI_getvalue(tuple, tupdesc, 5);
			strncat(values[i_hist_dts], tmp, length_hd);
			length_hd -= strlen(tmp);
			if (length_hd < 0) {
				FAIL_FRAME("hist_dts values needs to be increased");
			}

			tmp = SPI_getvalue(tuple, tupdesc, 3);
			strncat(values[i_qty], tmp, length_q);
			length_q -= strlen(tmp);
			if (length_q < 0) {
				FAIL_FRAME("qty values needs to be increased");
			}

			strncat(values[i_symbol], "\"", length_s--);
			if (length_s < 0) {
				FAIL_FRAME("symbol values needs to be increased");
			}

			tmp = SPI_getvalue(tuple, tupdesc, 2);
			strncat(values[i_symbol], tmp, length_s);
			length_s -= strlen(tmp);
			if (length_s < 0) {
				FAIL_FRAME("symbol values needs to be increased");
			}

			strncat(values[i_symbol], "\"", length_s--);
			if (length_s < 0) {
				FAIL_FRAME("symbol values needs to be increased");
			}

			tmp = SPI_getvalue(tuple, tupdesc, 1);
			strncat(values[i_trade_id], tmp, length_ti);
			length_ti -= strlen(tmp);
			if (length_ti < 0) {
				FAIL_FRAME("trade_id values needs to be increased");
			}

			strncat(values[i_trade_status], "\"", length_ts--);
			if (length_ts < 0) {
				FAIL_FRAME("trade_status values needs to be increased");
			}

			tmp = SPI_getvalue(tuple, tupdesc, 4);
			strncat(values[i_trade_status], tmp, length_ts);
			length_ts -= strlen(tmp);
			if (length_ts < 0) {
				FAIL_FRAME("trade_status values needs to be increased");
			}

			strncat(values[i_trade_status], "\"", length_ts--);
			if (length_ts < 0) {
				FAIL_FRAME("trade_status values needs to be increased");
			}

			for (i = 1; i < SPI_processed; i++) {
				tuple = tuptable->vals[i];

				strncat(values[i_hist_dts], ",", length_hd--);
				if (length_hd < 0) {
					FAIL_FRAME("hist_dts values needs to be increased");
				}

				tmp = SPI_getvalue(tuple, tupdesc, 5);
				strncat(values[i_hist_dts], tmp, length_hd);
				length_hd -= strlen(tmp);
				if (length_hd < 0) {
					FAIL_FRAME("hist_dts values needs to be increased");
				}

				strncat(values[i_qty], ",", length_q--);
				if (length_q < 0) {
					FAIL_FRAME("qty values needs to be increased");
				}

				tmp = SPI_getvalue(tuple, tupdesc, 3);
				strncat(values[i_qty], tmp, length_q);
				length_q -= strlen(tmp);
				if (length_q < 0) {
					FAIL_FRAME("qty values needs to be increased");
				}

				strncat(values[i_symbol], ",\"", length_s);
				length_s -= 2;
				if (length_s < 0) {
					FAIL_FRAME("symbol values needs to be increased");
				}

				tmp = SPI_getvalue(tuple, tupdesc, 2);
				strncat(values[i_symbol], tmp, length_s);
				length_s -= strlen(tmp);
				if (length_s < 0) {
					FAIL_FRAME("symbol values needs to be increased");
				}

				strncat(values[i_symbol], "\"", length_s--);
				if (length_s < 0) {
					FAIL_FRAME("symbol values needs to be increased");
				}

				strncat(values[i_trade_id], ",", length_ti--);
				if (length_ti < 0) {
					FAIL_FRAME("trade_id values needs to be increased");
				}

				tmp = SPI_getvalue(tuple, tupdesc, 1);
				strncat(values[i_trade_id], tmp, length_ti);
				length_ti -= strlen(tmp);
				if (length_ti < 0) {
					FAIL_FRAME("trade_id values needs to be increased");
				}

				strncat(values[i_trade_status], ",\"", length_ts);
				length_ts -= 2;
				if (length_ts < 0) {
					FAIL_FRAME("trade_status values needs to be increased");
				}

				tmp = SPI_getvalue(tuple, tupdesc, 4);
				strncat(values[i_trade_status], tmp, length_ts);
				length_ts -= strlen(tmp);
				if (length_ts < 0) {
					FAIL_FRAME("trade_status values needs to be increased");
				}

				strncat(values[i_trade_status], "\"", length_ts--);
				if (length_ts < 0) {
					FAIL_FRAME("trade_status values needs to be increased");
				}
			}

			strncat(values[i_hist_dts], "}", length_hd--);
			if (length_hd < 0) {
				FAIL_FRAME("hist_dts values needs to be increased");
			}

			strncat(values[i_qty], "}", length_q--);
			if (length_q < 0) {
				FAIL_FRAME("qty values needs to be increased");
			}

			strncat(values[i_symbol], "}", length_s--);
			if (length_s < 0) {
				FAIL_FRAME("symbol values needs to be increased");
			}

			strncat(values[i_trade_id], "}", length_ti--);
			if (length_ti < 0) {
				FAIL_FRAME("trade_id values needs to be increased");
			}

			strncat(values[i_trade_status], "}", length_ts--);
			if (length_ts < 0) {
				FAIL_FRAME("trade_status values needs to be increased");
			}
		} else {
			if (ret == SPI_OK_SELECT && SPI_processed == 0) {
				elog(WARNING, "Query CPF2_1 should return 10-30 rows.");
			}
#ifdef DEBUG
			dump_cpf2_inputs(acct_id);
#endif /* DEBUG */
			FAIL_FRAME_SET(&funcctx->max_calls, CPF2_statements[1].sql);

			/*
			 * FIXME: Probably don't need to do this if we're not going to
			 * return any rows, but if we do then we don't need to figure
			 * out what pieces of memory we need to free later in the function.
			 */
			values[i_hist_dts] = (char *) palloc(3 * sizeof(char));
			values[i_qty] = (char *) palloc(3 * sizeof(char));
			values[i_symbol] = (char *) palloc(3 * sizeof(char));
			values[i_trade_id] = (char *) palloc(3 * sizeof(char));
			values[i_trade_status] = (char *) palloc(3 * sizeof(char));

			strncpy(values[i_hist_dts], "{}", 3);
			strncpy(values[i_qty], "{}", 3);
			strncpy(values[i_symbol], "{}", 3);
			strncpy(values[i_trade_id], "{}", 3);
			strncpy(values[i_trade_status], "{}", 3);
		}

		/* Build a tuple descriptor for our result type. */
		if (get_call_result_type(fcinfo, NULL, &tupdesc)
				!= TYPEFUNC_COMPOSITE) {
			ereport(ERROR, (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
								   errmsg("function returning record called "
										  "in context "
										  "that cannot accept type record")));
		}

		/*
		 * Generate attribute metadata needed later to produce tuples from raw
		 * C strings.
		 */
		funcctx->attinmeta = TupleDescGetAttInMetadata(tupdesc);

		/* save SPI data for use across calls */
		funcctx->user_fctx = tuptable;

		MemoryContextSwitchTo(oldcontext);
	}

	/* Stuff done on every call of the function. */
	funcctx = SRF_PERCALL_SETUP();

	if (funcctx->call_cntr < funcctx->max_calls) {
		Datum result;
		HeapTuple tuple;

#ifdef DEBUG
		for (i = 0; i < 6; i++) {
			elog(DEBUG1, "CPF2 OUT: %d '%s'", i, values[i]);
		}
#endif /* DEBUG */

		/* Build a tuple. */
		tuple = BuildTupleFromCStrings(funcctx->attinmeta, values);

		/* Make the tuple into a datum. */
		result = HeapTupleGetDatum(tuple);

		SRF_RETURN_NEXT(funcctx, result);
	} else {
		SPI_finish();
		SRF_RETURN_DONE(funcctx);
	}
}
