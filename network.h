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
   Change HOST/PORT if your Express server moves.
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
    NET_STATUS_ERROR       /* network/parse error */
} NetStatus;

/* ─────────────────────────────────────────────
   PAYMENT RESPONSE
   Filled by network_create_payment() and
   network_poll_status().
───────────────────────────────────────────── */
#define NET_ID_LEN    32
#define NET_QR_LEN    256
#define NET_SIG_LEN   96
#define NET_SOL_LEN   16

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
   SHARED RESULT — thread-safe bridge between
   network thread and render loop.
   
   Usage in main loop:
     SDL_LockMutex(g_net.mutex);
     if (g_net.ready) { read g_net.response; }
     SDL_UnlockMutex(g_net.mutex);
───────────────────────────────────────────── */
typedef struct
{
    PaymentResponse response;
    SDL_mutex      *mutex;
    int             ready;       /* 1 = new result available */
    int             polling;     /* 1 = poll thread active   */
    int             stop;        /* 1 = signal thread to stop */
    int             poll_start;  /* 1 = poll thread launched  */
} NetworkResult;

/* ─────────────────────────────────────────────
   THREAD DATA — passed to SDL_Thread functions
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

/* ─────────────────────────────────────────────
   PUBLIC API
───────────────────────────────────────────── */

/* Call once at startup */
int  network_init(void);

/* Call once at shutdown */
void network_cleanup(void);

/* Check if backend is reachable — returns 1 if ok */
int  network_health_check(void);

/*
 * Blocking: POST /payment/create
 * Fills `out` with payment_id, qr_data, amount_sol.
 * Returns 0 on success, -1 on error.
 * Call from a background thread.
 */
int  network_create_payment(int amount_naira, PaymentResponse *out);
/*
 * Blocking: GET /payment/:id/status
 * Fills `out` with status and tx_signature if confirmed.
 * Returns 0 on success, -1 on error.
 * Call from a background thread.
 */
int  network_poll_status(const char *payment_id, PaymentResponse *out);

/*
 * SDL_Thread entry points — pass these to SDL_CreateThread()
 * Data must be heap-allocated; thread frees it.
 */
int  network_thread_create(void *data);   /* CreateThreadData* */
int  network_thread_poll(void *data);     /* PollThreadData*   */

/* Convert NetStatus to display string */
const char *network_status_name(NetStatus status);

#endif /* NETWORK_H */
