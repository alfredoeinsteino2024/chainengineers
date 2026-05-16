#ifndef STATES_H
#define STATES_H

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

typedef enum
{
    STATE_IDLE,
    STATE_ENTER_AMOUNT,
    STATE_PROCESSING,
    STATE_WAITING_PAYMENT,
    STATE_CONFIRMED,
    STATE_FAILED,
    STATE_HISTORY,
    STATE_BALANCE,
    STATE_COUNT
} TerminalState;

typedef struct
{
    char digits[12];
    int  len;
} AmountInput;

/* ─────────────────────────────────────────────
   TRANSACTION HISTORY
───────────────────────────────────────────── */
#define MAX_HISTORY  10
#define TX_SOL_LEN   16
#define TX_SIG_LEN   96
#define TX_ID_LEN    32

typedef struct
{
    char naira[16];
    char amount_sol[TX_SOL_LEN];
    char tx_signature[TX_SIG_LEN];
    char payment_id[TX_ID_LEN];
    char timestamp[32];
} TxRecord;

typedef struct
{
    TxRecord records[MAX_HISTORY];
    int      count;
    int      selected;
} TxHistory;

const char* getStateName(TerminalState state);
void clearAmount(AmountInput *amount);
void appendDigit(AmountInput *amount, char digit);
void backspaceAmount(AmountInput *amount);
void formatAmount(const AmountInput *amount, char *out, int outLen);

#endif
