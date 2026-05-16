#ifndef NETWORK_H
#define NETWORK_H

/*
 * network.h — ChainEngineers Payment Terminal
 * HTTP client layer: SDL2 Terminal <-> Node.js Backend <-> Solana Devnet
 *
 * Uses raw Winsock2 (Windows) or POSIX sockets (Linux/Mac).
 * Zero external dependencies beyond what you already have.
 *
 * THREAD SAFETY:
 *   network_create_payment() and network_poll_status() are blocking.
 *   Call them from a background SDL_Thread — never from the render loop.
 *   Use the NetworkResult shared struct + mutex pattern shown below.
 */

#include <SDL2/SDL.h>

/* ─────────────────────────────────────────────
   BACKEND CONFIG
───────────────────────────────────────────── */
#define NET_HOST        "127.0.0.1"
#define NET_PORT        3000
#define NET_TIMEOUT_SEC 8

/* ─────────────────────────────────────────────
   PAYMENT STATUS — mirrors backend response
───────────────────────────────────────────── */
typedef enum
{
    NET_STATUS_NONE = 0,
    NET_STATUS_PENDING,
    NET_STATUS_CONFIRMED,
    NET_STATUS_FAILED,
    NET_STATUS_EXPIRED,
    NET_STATUS_ERROR
} NetStatus;

/* ─────────────────────────────────────────────
   PAYMENT RESPONSE
───────────────────────────────────────────── */
#define NET_ID_LEN    32
#define NET_QR_LEN    256
#define NET_SIG_LEN   96
#define NET_SOL_LEN   16
#define NET_TIME_LEN  16

typedef struct
{
    char      payment_id[NET_ID_LEN];
    char      qr_data[NET_QR_LEN];
    char      tx_signature[NET_SIG_LEN];
    char      amount_sol[NET_SOL_LEN];
    NetStatus status;
    char      error_msg[64];
} PaymentResponse;

/* ─────────────────────────────────────────────
   HISTORY ENTRY — one confirmed transaction
───────────────────────────────────────────── */
typedef struct
{
    char payment_id[NET_ID_LEN];
    int  amount_naira;
    char amount_sol[NET_SOL_LEN];
    char tx_signature[NET_SIG_LEN];
    char timestamp[NET_TIME_LEN];
} HistoryEntry;

/* ─────────────────────────────────────────────
   HISTORY RESPONSE — up to 10 transactions
───────────────────────────────────────────── */
#define NET_MAX_HISTORY 10

typedef struct
{
    HistoryEntry entries[NET_MAX_HISTORY];
    int          count;
    int          ready;     /* 1 = data available */
    int          error;     /* 1 = fetch failed   */
} HistoryResponse;

/* ─────────────────────────────────────────────
   BALANCE RESPONSE
───────────────────────────────────────────── */
#define NET_ADDR_LEN  48
#define NET_BAL_LEN   20

typedef struct
{
    char address[NET_ADDR_LEN];
    char balance_sol[NET_BAL_LEN];
    int  ready;     /* 1 = data available */
    int  error;     /* 1 = fetch failed   */
} BalanceResponse;

/* ─────────────────────────────────────────────
   SHARED RESULT — thread-safe bridge between
   network thread and render loop.
───────────────────────────────────────────── */
typedef struct
{
    PaymentResponse response;
    SDL_mutex      *mutex;
    int             ready;
    int             polling;
    int             stop;
    int             poll_start;
} NetworkResult;

/* ─────────────────────────────────────────────
   THREAD DATA
───────────────────────────────────────────── */
typedef struct
{
    int            amount_naira;
    NetworkResult *result;
} CreateThreadData;

typedef struct
{
    char           payment_id[NET_ID_LEN];
    NetworkResult *result;
} PollThreadData;

typedef struct
{
    HistoryResponse *result;
    SDL_mutex       *mutex;
} HistoryThreadData;

typedef struct
{
    BalanceResponse *result;
    SDL_mutex       *mutex;
} BalanceThreadData;

/* ─────────────────────────────────────────────
   PUBLIC API
───────────────────────────────────────────── */

/* Call once at startup */
int  network_init(void);

/* Call once at shutdown */
void network_cleanup(void);

/* Check if backend is reachable — returns 1 if ok */
int  network_health_check(void);

/* Blocking: POST /payment/create */
int  network_create_payment(int amount_naira, PaymentResponse *out);

/* Blocking: GET /payment/:id/status */
int  network_poll_status(const char *payment_id, PaymentResponse *out);

/* Blocking: GET /history */
int  network_fetch_history(HistoryResponse *out);

/* Blocking: GET /balance */
int  network_fetch_balance(BalanceResponse *out);

/* SDL_Thread entry points */
int  network_thread_create(void *data);   /* CreateThreadData*  */
int  network_thread_poll(void *data);     /* PollThreadData*    */
int  network_thread_history(void *data);  /* HistoryThreadData* */
int  network_thread_balance(void *data);  /* BalanceThreadData* */

/* Convert NetStatus to display string */
const char *network_status_name(NetStatus status);

int  network_get_balance(char *response_out, int response_max);
void network_parse_field(const char *json, const char *key,
                          char *out, int out_len);

#endif /* NETWORK_H */
