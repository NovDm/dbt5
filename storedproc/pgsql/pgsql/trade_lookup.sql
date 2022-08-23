/*
 * This file is released under the terms of the Artistic License.  Please see
 * the file LICENSE, included in this package, for details.
 *
 * Copyright The DBT-5 Authors
 *
 * Based on TPC-E Standard Specification Revision 1.14.0
 */

-- Clause 3.3.6.3
CREATE OR REPLACE FUNCTION TradeLookupFrame1 (
						IN max_trades integer,
    IN trade_id TRADE_T[20]
  , OUT bid_price S_PRICE_T[20]
  , OUT cash_transaction_amount VALUE_T[20]
  , OUT cash_transaction_dts TIMESTAMP[20]
  , OUT cash_transaction_name VARCHAR(100)[20]
  , OUT exec_name VARCHAR(64)[20]
  , OUT is_cash SMALLINT[20]
  , OUT is_market SMALLINT[20]
  , OUT num_found INTEGER
  , OUT settlement_amount VALUE_T[20]
  , OUT settlement_cash_due_date TIMESTAMP[20]
  , OUT settlement_cash_type VARCHAR(40)[20]
  , OUT trade_history_dts TIMESTAMP[20][3]
  , OUT trade_history_status_id VARCHAR(4)[20][3]
  , OUT trade_price S_PRICE_T[20]
) RETURNS RECORD AS $$
DECLARE
	-- variables
	i		integer;
	j		integer;
    k INTEGER;
	rs		RECORD;
    irow_count INTEGER;

    tmp_bid_price S_PRICE_T;
    tmp_exec_name VARCHAR(64);
    tmp_is_cash BOOLEAN;
    tmp_is_market BOOLEAN;
    tmp_trade_price S_PRICE_T;

    tmp_settlement_amount VALUE_T;
    tmp_settlement_cash_due_date TIMESTAMP;
    tmp_settlement_cash_type VARCHAR(40);

    tmp_cash_transaction_amount VALUE_T;
    tmp_cash_transaction_dts TIMESTAMP;
    tmp_cash_transaction_name VARCHAR(100);
BEGIN
    num_found = 0;

    cash_transaction_amount := '{}';
    cash_transaction_dts := '{}';
    cash_transaction_name := '{}';

    trade_history_dts := '{}';
    trade_history_status_id := '{}';

    i = 0;
    WHILE i < max_trades LOOP
        i = i + 1;

		-- Get trade information
		-- Should only return one row for each trade

		SELECT	T_BID_PRICE,
			T_EXEC_NAME,
			T_IS_CASH,
			TT_IS_MRKT,
			T_TRADE_PRICE
        into tmp_bid_price
           , tmp_exec_name
           , tmp_is_cash
           , tmp_is_market
           , tmp_trade_price
		FROM	TRADE,
			TRADE_TYPE
		WHERE	T_ID = trade_id[i] AND
			T_TT_ID = TT_ID;

        GET DIAGNOSTICS irow_count = ROW_COUNT;
        num_found = num_found + irow_count;

        IF irow_count > 0 THEN
            bid_price[i] := tmp_bid_price;
            exec_name[i] := tmp_exec_name;
            IF tmp_is_cash THEN
                is_cash[i] := 1;
            ELSE
                is_cash[i] := 0;
            END IF;
            IF tmp_is_market THEN
                is_market[i] := 1;
            ELSE
                is_market[i] := 0;
            END IF;
            trade_price[i] := tmp_trade_price;
        END IF;
		
		-- Get settlement information
		-- Should only return one row for each trade

		SELECT	SE_AMT,
			SE_CASH_DUE_DATE,
			SE_CASH_TYPE
        INTO tmp_settlement_amount
           , tmp_settlement_cash_due_date
           , tmp_settlement_cash_type
		FROM	SETTLEMENT
		WHERE	SE_T_ID = trade_id[i];
			
        GET DIAGNOSTICS irow_count = ROW_COUNT;

        IF irow_count > 0 THEN
            settlement_amount[i] := tmp_settlement_amount;
            settlement_cash_due_date[i] := tmp_settlement_cash_due_date;
            settlement_cash_type[i] := tmp_settlement_cash_type;
        END IF;

		-- get cash information if this is a cash transaction
		-- Should only return one row for each trade that was a cash transaction

        IF tmp_is_cash THEN
			SELECT	CT_AMT,
				CT_DTS,
				CT_NAME
            INTO tmp_cash_transaction_amount
               , tmp_cash_transaction_dts
               , tmp_cash_transaction_name
			FROM	CASH_TRANSACTION
			WHERE	CT_T_ID = trade_id[i];
		END IF;

        IF irow_count > 0 THEN
            cash_transaction_amount[i] := tmp_cash_transaction_amount;
            cash_transaction_dts[i] := tmp_cash_transaction_dts;
            cash_transaction_name[i] := tmp_cash_transaction_name;
        END IF;

		-- read trade_history for the trades
		-- Should return 2 to 3 rows per trade

        j = 0;
        k := (i - 1) * 3 + 1;
        trade_history_dts[k + j] = NULL;
        trade_history_dts[k + j + 1] = NULL;
        trade_history_dts[k + j + 2] = NULL;
        trade_history_status_id[k + j] = NULL;
        trade_history_status_id[k + j + 1] = NULL;
        trade_history_status_id[k + j + 1] = NULL;
		FOR rs IN SELECT TH_DTS, TH_ST_ID 
			FROM TRADE_HISTORY
            WHERE th_t_id = trade_id[i]
            ORDER BY th_dts
            LIMIT 3
        LOOP
            trade_history_dts[k + j] = rs.TH_DTS;
            trade_history_status_id[k + j] = rs.TH_ST_ID;
			j = j + 1;
		END LOOP;
	END LOOP;
END;
$$ LANGUAGE 'plpgsql';

-- Clause 3.3.6.4
CREATE OR REPLACE FUNCTION TradeLookupFrame2(
    IN acct_id IDENT_T
  , IN end_trade_dts TIMESTAMP
  , IN max_trades integer
  , IN start_trade_dts TIMESTAMP
  , OUT bid_price VALUE_T[20]
  , OUT cash_transaction_amount VALUE_T[20]
  , OUT cash_transaction_dts TIMESTAMP[20]
  , OUT cash_transaction_name VARCHAR[20]
  , OUT exec_name VARCHAR[20]
  , OUT is_cash BOOL[20]
  , OUT num_found INTEGER
  , OUT settlement_amount VALUE_T[20]
  , OUT settlement_cash_due_date TIMESTAMP[20]
  , OUT settlement_cash_type VARCHAR[20]
  , OUT trade_history_dts TIMESTAMP[20][3]
  , OUT trade_history_status_id	CHAR(4)[20][3]
  , OUT trade_list IDENT_T[20]
  , OUT trade_price VALUE_T[20]
) RETURNS record AS $$
DECLARE
	-- variables
	i		integer;
	j		integer;
    k INTEGER;
    irow_count INTEGER;
	rs		RECORD;
	aux		RECORD;

    tmp_settlement_amount VALUE_T;
    tmp_settlement_cash_due_date TIMESTAMP;
    tmp_settlement_cash_type VARCHAR(40);

    tmp_cash_transaction_amount VALUE_T;
    tmp_cash_transaction_dts TIMESTAMP;
    tmp_cash_transaction_name VARCHAR(100);
BEGIN
	-- Get trade information
	-- Should return between 0 and max_trades rows

	i = 0;
	FOR rs IN SELECT T_BID_PRICE,
			T_EXEC_NAME,
			T_IS_CASH,
			T_ID,
			T_TRADE_PRICE
		FROM	TRADE
		WHERE	T_CA_ID = acct_id AND
              T_DTS >= start_trade_dts
          AND T_DTS <= end_trade_dts
		ORDER BY T_DTS asc
		LIMIT max_trades LOOP
        i = i + 1;

        bid_price[i] := rs.t_bid_price;
        exec_name[i] := rs.t_exec_name;
        IF rs.t_is_cash THEN
            is_cash[i] := 1;
        ELSE
            is_cash[i] := 0;
        END IF;
        trade_list[i] := rs.t_id;
        trade_price[i] := rs.t_trade_price;

		-- Get settlement information
		-- Should return only one row for each trade

		SELECT	SE_AMT,
			SE_CASH_DUE_DATE,
			SE_CASH_TYPE
        INTO tmp_settlement_amount
          , tmp_settlement_cash_due_date
          , tmp_settlement_cash_type
		FROM	SETTLEMENT
		WHERE	SE_T_ID = rs.T_ID;

        GET DIAGNOSTICS irow_count = ROW_COUNT;
        IF irow_count > 0 THEN
            settlement_amount[i] := tmp_settlement_amount;
            settlement_cash_due_date[i] := tmp_settlement_cash_due_date;
            settlement_cash_type[i] := tmp_settlement_cash_type;
        END IF;

		-- get cash information if this is a cash transaction
		-- Should return only one row for each trade that was a cash transaction

		IF rs.T_IS_CASH THEN
			SELECT	CT_AMT,
 				CT_DTS,
				CT_NAME
            INTO tmp_cash_transaction_amount
              , tmp_cash_transaction_dts
              , tmp_cash_transaction_name
			FROM	CASH_TRANSACTION
			WHERE	CT_T_ID = rs.T_ID;
		END IF;

        GET DIAGNOSTICS irow_count = ROW_COUNT;
        IF irow_count > 0 THEN
            cash_transaction_amount[i] := tmp_cash_transaction_amount;
            cash_transaction_dts[i] := tmp_cash_transaction_dts;
            cash_transaction_name[i] := tmp_cash_transaction_name;
        END IF;

		-- read trade_history for the trades
		-- Should return 2 to 3 rows per trade

        j = 0;
        k := (i - 1) * 3 + 1;
        trade_history_dts[k + j] = NULL;
        trade_history_dts[k + j + 1] = NULL;
        trade_history_dts[k + j + 2] = NULL;
        trade_history_status_id[k + j] = NULL;
        trade_history_status_id[k + j + 1] = NULL;
        trade_history_status_id[k + j + 1] = NULL;
		FOR aux IN SELECT TH_DTS, TH_ST_ID 
			FROM TRADE_HISTORY
            WHERE th_t_id = rs.t_id
            ORDER BY th_dts
            LIMIT 3
        LOOP
            trade_history_dts[k + j] = aux.th_dts;
            trade_history_status_id[k + j] = aux.th_st_id;
            j = j + 1;
		END LOOP;
	END LOOP;
	num_found := i;
END;
$$ LANGUAGE 'plpgsql';

-- Clause 3.3.6.5
CREATE OR REPLACE FUNCTION TradeLookupFrame3(
    IN end_trade_dts TIMESTAMP,
						IN max_acct_id	IDENT_T,
						IN max_trades	integer,
    IN start_trade_dts TIMESTAMP,
						IN symbol	char(15)
  , OUT acct_id IDENT_T[20]
  , OUT cash_transaction_amount VALUE_T[20]
  , OUT cash_transaction_dts TIMESTAMP[20]
  , OUT cash_transaction_name VARCHAR[20]
  , OUT exec_name VARCHAR(64)[20]
  , OUT is_cash INTEGER[20]
  , OUT num_found INTEGER
  , OUT price VALUE_T[20]
  , OUT quantity INTEGER[20]
  , OUT settlement_amount VALUE_T[20]
  , OUT settlement_cash_due_date TIMESTAMP[20]
  , OUT settlement_cash_type VARCHAR[20]
  , OUT trade_dts TIMESTAMP[20]
  , OUT trade_history_dts TIMESTAMP[20][3]
  , OUT trade_history_status_id CHAR(4)[20][3]
  , OUT trade_list IDENT_T[20]
  , OUT trade_type VARCHAR(3)[20]
) RETURNS RECORD AS $$
DECLARE
	-- Local Frame variables
	i	integer;
	j	integer;
    k INTEGER;
    irow_count INTEGER;
	rs	RECORD;
	aux	RECORD;

    tmp_settlement_amount VALUE_T;
    tmp_settlement_cash_due_date TIMESTAMP;
    tmp_settlement_cash_type VARCHAR(40);

    tmp_cash_transaction_amount VALUE_T;
    tmp_cash_transaction_dts TIMESTAMP;
    tmp_cash_transaction_name VARCHAR(100);
BEGIN
	-- Should return between 0 and max_trades rows.

	i = 0;
	FOR rs IN SELECT T_CA_ID,
			T_EXEC_NAME,
			T_IS_CASH,
			T_TRADE_PRICE,
			T_QTY,
			T_DTS,
            T_ID,
			T_TT_ID
		FROM	TRADE
		WHERE	T_S_SYMB = symbol AND
              T_DTS >= start_trade_dts
          AND T_DTS <= end_trade_dts
		ORDER BY T_DTS asc
		LIMIT max_trades LOOP
        i = i + 1;

        acct_id[i] := rs.t_ca_id;
        exec_name[i] := rs.t_exec_name;
        IF rs.t_is_cash THEN
            is_cash[i] := 1;
        ELSE
            is_cash[i] := 0;
        END IF;
        price[i] := rs.t_trade_price;
        quantity[i] := rs.t_qty;
        trade_dts[i] := rs.t_dts;
        trade_list[i] := rs.t_id;
        trade_type[i] := rs.t_tt_id;

		-- Get extra information for each trade in the trade list.
		-- Get settlement information
		-- Should return only one row for each trade

		SELECT	SE_AMT,
			SE_CASH_DUE_DATE,
			SE_CASH_TYPE
        INTO tmp_settlement_amount
          , tmp_settlement_cash_due_date
          , tmp_settlement_cash_type
		FROM	SETTLEMENT
		WHERE	SE_T_ID = rs.T_ID;

        GET DIAGNOSTICS irow_count = ROW_COUNT;
        IF irow_count > 0 THEN
            settlement_amount[i] := tmp_settlement_amount;
            settlement_cash_due_date[i] := tmp_settlement_cash_due_date;
            settlement_cash_type[i] := tmp_settlement_cash_type;
        END IF;

		-- get cash information if this is a cash transaction
		-- Should return only one row for each trade that was a cash transaction

		IF rs.T_IS_CASH THEN
			SELECT	CT_AMT,
 				CT_DTS,
				CT_NAME
            INTO tmp_cash_transaction_amount
               , tmp_cash_transaction_dts
               , tmp_cash_transaction_name
			FROM	CASH_TRANSACTION
			WHERE	CT_T_ID = rs.T_ID;
		END IF;

        IF irow_count > 0 THEN
            cash_transaction_amount[i] := tmp_cash_transaction_amount;
            cash_transaction_dts[i] := tmp_cash_transaction_dts;
            cash_transaction_name[i] := tmp_cash_transaction_name;
        END IF;

		-- read trade_history for the trades
		-- Should return 2 to 3 rows per trade

        j = 0;
        k := (i - 1) * 3 + 1;
        trade_history_dts[k + j] = NULL;
        trade_history_dts[k + j + 1] = NULL;
        trade_history_dts[k + j + 2] = NULL;
        trade_history_status_id[k + j] = NULL;
        trade_history_status_id[k + j + 1] = NULL;
        trade_history_status_id[k + j + 1] = NULL;
		FOR aux IN SELECT TH_DTS, TH_ST_ID 
			FROM TRADE_HISTORY
            WHERE th_t_id = rs.t_id
            ORDER BY th_dts
            LIMIT 3
        LOOP
            trade_history_dts[k + j] = aux.th_dts;
            trade_history_status_id[k + j] = aux.th_st_id;
            j = j + 1;
		END LOOP;
	END LOOP;
    num_found := i;
END;
$$ LANGUAGE 'plpgsql';

-- Clause 3.3.6.6
CREATE OR REPLACE FUNCTION TradeLookupFrame4(
		IN acct_id	IDENT_T,
    IN start_trade_dts TIMESTAMP
  , OUT holding_history_id IDENT_T[20]
  , OUT holding_history_trade_id IDENT_T[20]
  , OUT num_found INTEGER
  , OUT num_trades_found INTEGER
  , OUT quantity_after INTEGER[20]
  , OUT quantity_before INTEGER[20]
  , OUT trade_id TRADE_T
) RETURNS RECORD AS $$
DECLARE
    i INTEGER;
	-- Local Frame variables
	rs		RECORD;
BEGIN
    FOR rs IN
        SELECT T_ID
        FROM TRADE
        WHERE t_ca_id = acct_id
          AND t_dts >= start_trade_dts
        ORDER BY t_dts ASC
    LOOP
        trade_id := rs.t_id;
        EXIT;
    END LOOP;

    GET DIAGNOSTICS i = ROW_COUNT;
    num_trades_found = i;

	-- The trade_id is used in the subquery to find the original trade_id
	-- (HH_H_T_ID), which then is used to list all the entries.
	-- Should return 0 to 20 rows.
	
    i := 0;
	FOR rs IN SELECT HH_H_T_ID,
			HH_T_ID,
			HH_BEFORE_QTY,
            hh_after_qty
			FROM	HOLDING_HISTORY
			WHERE	HH_H_T_ID in
				(SELECT	HH_H_T_ID
				FROM	HOLDING_HISTORY
				WHERE	HH_T_ID = trade_id)
			LIMIT 20 
	LOOP
        i := i + 1;
        holding_history_id[i] := rs.hh_h_t_id;
        holding_history_trade_id[i] := rs.hh_t_id;
        quantity_before[i] := rs.hh_before_qty;
        quantity_after[i] := rs.hh_after_qty;
	END LOOP;
    num_found := i;
END;
$$ LANGUAGE 'plpgsql';
