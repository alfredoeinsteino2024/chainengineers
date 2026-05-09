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
    STATE_COUNT
} TerminalState;

typedef struct
{
    char digits[12];
    int  len;
} AmountInput;

const char* getStateName(TerminalState state);
void clearAmount(AmountInput *amount);
void appendDigit(AmountInput *amount, char digit);
void backspaceAmount(AmountInput *amount);
void formatAmount(const AmountInput *amount, char *out, int outLen);

#endif
