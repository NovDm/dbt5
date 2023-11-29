/*
 * Legal Notice
 *
 * This document and associated source code (the "Work") is a part of a
 * benchmark specification maintained by the TPC.
 *
 * The TPC reserves all right, title, and interest to the Work as provided
 * under U.S. and international laws, including without limitation all patent
 * and trademark rights therein.
 *
 * No Warranty
 *
 * 1.1 TO THE MAXIMUM EXTENT PERMITTED BY APPLICABLE LAW, THE INFORMATION
 *     CONTAINED HEREIN IS PROVIDED "AS IS" AND WITH ALL FAULTS, AND THE
 *     AUTHORS AND DEVELOPERS OF THE WORK HEREBY DISCLAIM ALL OTHER
 *     WARRANTIES AND CONDITIONS, EITHER EXPRESS, IMPLIED OR STATUTORY,
 *     INCLUDING, BUT NOT LIMITED TO, ANY (IF ANY) IMPLIED WARRANTIES,
 *     DUTIES OR CONDITIONS OF MERCHANTABILITY, OF FITNESS FOR A PARTICULAR
 *     PURPOSE, OF ACCURACY OR COMPLETENESS OF RESPONSES, OF RESULTS, OF
 *     WORKMANLIKE EFFORT, OF LACK OF VIRUSES, AND OF LACK OF NEGLIGENCE.
 *     ALSO, THERE IS NO WARRANTY OR CONDITION OF TITLE, QUIET ENJOYMENT,
 *     QUIET POSSESSION, CORRESPONDENCE TO DESCRIPTION OR NON-INFRINGEMENT
 *     WITH REGARD TO THE WORK.
 * 1.2 IN NO EVENT WILL ANY AUTHOR OR DEVELOPER OF THE WORK BE LIABLE TO
 *     ANY OTHER PARTY FOR ANY DAMAGES, INCLUDING BUT NOT LIMITED TO THE
 *     COST OF PROCURING SUBSTITUTE GOODS OR SERVICES, LOST PROFITS, LOSS
 *     OF USE, LOSS OF DATA, OR ANY INCIDENTAL, CONSEQUENTIAL, DIRECT,
 *     INDIRECT, OR SPECIAL DAMAGES WHETHER UNDER CONTRACT, TORT, WARRANTY,
 *     OR OTHERWISE, ARISING IN ANY WAY OUT OF THIS OR ANY OTHER AGREEMENT
 *     RELATING TO THE WORK, WHETHER OR NOT SUCH AUTHOR OR DEVELOPER HAD
 *     ADVANCE NOTICE OF THE POSSIBILITY OF SUCH DAMAGES.
 *
 * Contributors
 * - 2006 Rilson Nascimento
 * - 2010 Mark Wong <markwkm@postgresql.org>
 */

//
// Database loader class for CASH_TRANSACTION table.
//

#ifndef PGSQL_CASH_TRANSACTION_LOAD_H
#define PGSQL_CASH_TRANSACTION_LOAD_H

namespace TPCE
{

class CPGSQLCashTransactionLoad : public CPGSQLLoader<CASH_TRANSACTION_ROW>
{
private:
	CDateTime ct_dts;
public:
	CPGSQLCashTransactionLoad(const char *szConnectStr,
			const char *szTable = "cash_transaction")
			: CPGSQLLoader<CASH_TRANSACTION_ROW>(szConnectStr, szTable) { };

	void WriteNextRecord(const CASH_TRANSACTION_ROW &next_record) {
		ct_dts = next_record.CT_DTS;
		fprintf(p, "%" PRId64 "%c%s%c%.2f%c%s\n",
				next_record.CT_T_ID, delimiter,
				ct_dts.ToStr(iDateTimeFmt), delimiter,
				next_record.CT_AMT, delimiter,
				next_record.CT_NAME);
		// FIXME: Have blind faith that this row of data was built correctly.
		while (fgetc(p) != EOF) ;
	}
};

} // namespace TPCE

#endif //PGSQL_CASH_TRANSACTION_LOAD_H