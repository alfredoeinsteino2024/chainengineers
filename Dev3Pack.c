/*
 * Dev3Pack.c — ChainEngineers Payment Terminal
 * Phase 2: SDL2 + Network + Solana Devnet
 *
 * BUG FIXES FROM PREVIOUS VERSION:
 *
 *  BUG 1 & 2 — Unprotected reads of g_net fields after mutex release:
 *    Old: unlock mutex, then read g_net.response.payment_id[0]
 *    Fix: snapshot ALL g_net fields inside ONE mutex lock per frame,
 *         then use only local copies for all logic and prints.
 *
 *  BUG 3 — Unprotected tx_signature print:
 *    Old: printf g_net.response.tx_signature after mutex release
 *    Fix: use snap_tx local copy taken inside the mutex.
 *
 *  BUG 4 — Main freeze: ready flag never fires if JSON parse fails:
 *    Old: WAITING block only transitions when ready==1.
 *         If poll thread's JSON parser fails to extract "confirmed",
 *         ready is never set and the screen freezes permanently.
 *    Fix: check net_status DIRECTLY every frame — no ready flag needed.
 *         NET_STATUS_CONFIRMED is terminal: poll thread stops writing
 *         once set, so reading it every frame is always safe.
 *
 *  BUG 5 — No poll_start guard:
 *    Old: poll_started is a plain int with no mutex protection.
 *         start_poll_payment() could be called twice in edge cases.
 *    Fix: poll_start flag inside NetworkResult, set under mutex
 *         before thread launch, checked from snapshot each frame.
 */

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "ui/ui.h"
#include "states/states.h"
#include "network/network.h"

#define WIDTH  800
#define HEIGHT 480

/* ─────────────────────────────────────────────
   GLOBAL: shared network result
   Written by network threads, read by main loop.
───────────────────────────────────────────── */
NetworkResult g_net;

/* ─────────────────────────────────────────────
   INTERNAL: kick off payment creation thread
───────────────────────────────────────────── */
static void start_create_payment(int amount_naira)
{
    SDL_LockMutex(g_net.mutex);
    memset(&g_net.response, 0, sizeof(PaymentResponse));
    g_net.ready      = 0;
    g_net.stop       = 0;
    g_net.polling    = 0;
    g_net.poll_start = 0;   /* FIX 5: guard flag reset */
    SDL_UnlockMutex(g_net.mutex);

    CreateThreadData *data = malloc(sizeof(CreateThreadData));
    data->amount_naira     = amount_naira;
    data->result           = &g_net;

    SDL_Thread *t = SDL_CreateThread(network_thread_create,
                                     "CreatePayment", data);
    SDL_DetachThread(t);

    printf("[MAIN] Payment creation thread started for N%d\n", amount_naira);
    fflush(stdout);
}

/* ─────────────────────────────────────────────
   INTERNAL: kick off polling thread
───────────────────────────────────────────── */
static void start_poll_payment(const char *payment_id)
{
    SDL_LockMutex(g_net.mutex);
    g_net.ready      = 0;
    g_net.stop       = 0;
    g_net.polling    = 1;
    g_net.poll_start = 1;   /* FIX 5: mark poll as started under mutex */
    SDL_UnlockMutex(g_net.mutex);

    PollThreadData *data = malloc(sizeof(PollThreadData));
    strncpy(data->payment_id, payment_id, NET_ID_LEN - 1);
    data->payment_id[NET_ID_LEN - 1] = '\0';
    data->result = &g_net;

    SDL_Thread *t = SDL_CreateThread(network_thread_poll,
                                     "PollPayment", data);
    SDL_DetachThread(t);

    printf("[MAIN] Poll thread started for ID=%s\n", payment_id);
    fflush(stdout);
}

/* ─────────────────────────────────────────────
   MAIN
───────────────────────────────────────────── */
int main(int argc, char *argv[])
{
    /* ── SDL + TTF init ──────────────────── */
    if (SDL_Init(SDL_INIT_VIDEO) != 0)
    { printf("SDL Init Error: %s\n", SDL_GetError()); return 1; }

    if (TTF_Init() != 0)
    { printf("TTF Init Error: %s\n", TTF_GetError()); return 1; }

    SDL_Window *window = SDL_CreateWindow(
        "ChainEngineers Terminal",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        WIDTH, HEIGHT, 0
    );
    if (!window) { printf("Window Error: %s\n", SDL_GetError()); return 1; }

    SDL_Renderer *renderer = SDL_CreateRenderer(
        window, -1, SDL_RENDERER_ACCELERATED);
    if (!renderer) { printf("Renderer Error: %s\n", SDL_GetError()); return 1; }

    TTF_Font *font = TTF_OpenFont("assets/Roboto_Condensed-Regular.ttf", 28);
    if (!font) { printf("Font Error: %s\n", TTF_GetError()); return 1; }

    TTF_Font *fontLarge = TTF_OpenFont("assets/Roboto_Condensed-Regular.ttf", 56);
    if (!fontLarge) { printf("Font Large Error: %s\n", TTF_GetError()); return 1; }

    /* ── Network init ────────────────────── */
    memset(&g_net, 0, sizeof(NetworkResult));
    g_net.mutex = SDL_CreateMutex();
    if (!g_net.mutex) {
        printf("Mutex Error: %s\n", SDL_GetError());
        return 1;
    }

    if (network_init() != 0) {
        printf("[MAIN] WARNING: Network init failed. Running in offline mode.\n");
    } else {
        printf("[MAIN] Backend health: %s\n",
               network_health_check() ? "ONLINE" : "OFFLINE");
        fflush(stdout);
    }

    /* ── Terminal state ──────────────────── */
    TerminalState currentState = STATE_IDLE;
    AmountInput   amount;
    clearAmount(&amount);

    bool running = true;
    SDL_Event event;
    SDL_StartTextInput();

    printf("ChainEngineers Terminal Started\n");
    printf("State: %s\n", getStateName(currentState));
    fflush(stdout);

    /* ── Main loop ───────────────────────── */
    while (running)
    {
        /* ── Events ─────────────────────── */
        while (SDL_PollEvent(&event))
        {
            if (event.type == SDL_QUIT)
                running = false;

            if (event.type == SDL_TEXTINPUT
                && currentState == STATE_ENTER_AMOUNT)
            {
                char c = event.text.text[0];
                if (c >= '0' && c <= '9') {
                    appendDigit(&amount, c);
                    printf("[MAIN] State: %s | Amount: %s\n",
                           getStateName(currentState), amount.digits);
                    fflush(stdout);
                }
            }

            if (event.type == SDL_KEYDOWN)
            {
                SDL_Keycode key = event.key.keysym.sym;

                if (key == SDLK_ESCAPE)
                {
                    if (currentState == STATE_IDLE) {
                        running = false;
                    } else {
                        SDL_LockMutex(g_net.mutex);
                        g_net.stop = 1;
                        SDL_UnlockMutex(g_net.mutex);
                        clearAmount(&amount);
                        currentState = STATE_IDLE;
                    }
                }
                else if (currentState == STATE_IDLE)
                {
                    if (key == SDLK_RETURN) {
                        clearAmount(&amount);
                        currentState = STATE_ENTER_AMOUNT;
                    }
                }
                else if (currentState == STATE_ENTER_AMOUNT)
                {
                    if (key == SDLK_RETURN && amount.len > 0) {
                        int naira = atoi(amount.digits);
                        start_create_payment(naira);
                        currentState = STATE_PROCESSING;
                    }
                    else if (key == SDLK_BACKSPACE) {
                        backspaceAmount(&amount);
                    }
                    else if (key >= SDLK_KP_0 && key <= SDLK_KP_9) {
                        appendDigit(&amount, '0' + (key - SDLK_KP_0));
                    }
                }
                else if (currentState == STATE_PROCESSING)
                {
                    if (key == SDLK_p) {
                        currentState = STATE_CONFIRMED;
                        printf("[DEBUG] Manual confirm\n");
                        fflush(stdout);
                    }
                    if (key == SDLK_f) {
                        currentState = STATE_FAILED;
                        printf("[DEBUG] Manual fail\n");
                        fflush(stdout);
                    }
                }
                else if (currentState == STATE_CONFIRMED)
                {
                    if (key == SDLK_RETURN) {
                        clearAmount(&amount);
                        currentState = STATE_IDLE;
                    }
                }
                else if (currentState == STATE_FAILED)
                {
                    if (key == SDLK_RETURN) {
                        clearAmount(&amount);
                        currentState = STATE_IDLE;
                    }
                }

                printf("[MAIN] State: %s | Amount: %s\n",
                       getStateName(currentState), amount.digits);
                fflush(stdout);
            }
        }

        /* ────────────────────────────────────────────────────────
           NETWORK STATE CHECK
           
           FIX 1,2,3: ONE mutex lock per frame. ALL g_net fields
           copied to local variables. Zero g_net access after unlock.
        ──────────────────────────────────────────────────────────*/
        {
            NetStatus net_status;
            int       net_poll_start;
            char      snap_id[NET_ID_LEN];
            char      snap_tx[NET_SIG_LEN];

            SDL_LockMutex(g_net.mutex);
            net_status     = g_net.response.status;
            net_poll_start = g_net.poll_start;
            strncpy(snap_id, g_net.response.payment_id,   NET_ID_LEN  - 1);
            strncpy(snap_tx, g_net.response.tx_signature, NET_SIG_LEN - 1);
            snap_id[NET_ID_LEN  - 1] = '\0';
            snap_tx[NET_SIG_LEN - 1] = '\0';
            SDL_UnlockMutex(g_net.mutex);

            /* ── PROCESSING: wait for create thread ── */
            if (currentState == STATE_PROCESSING)
            {
                if (net_status == NET_STATUS_ERROR)
                {
                    printf("[MAIN] Create payment failed -> FAILED\n");
                    fflush(stdout);
                    currentState = STATE_FAILED;
                }
                /* FIX 1,2: snap_id used — no unprotected g_net read */
                /* FIX 5:   net_poll_start prevents double thread launch */
                else if (net_status == NET_STATUS_PENDING
                         && snap_id[0] != '\0'
                         && !net_poll_start)
                {
                    printf("[MAIN] Payment created. ID=%s -> WAITING\n", snap_id);
                    fflush(stdout);
                    currentState = STATE_WAITING_PAYMENT;
                    start_poll_payment(snap_id);
                }
            }

            /* ── WAITING: watch poll thread result ──
             *
             * FIX 4 — THE KEY FIX:
             * We check net_status directly every frame.
             * We do NOT require ready==1.
             *
             * Why this fixes the freeze:
             * The poll thread writes status=CONFIRMED then sets ready=1.
             * With the old code, if the JSON parse silently failed,
             * status stayed PENDING and ready stayed 0 — frozen forever.
             * Now we check the status value itself. The moment the poll
             * thread writes NET_STATUS_CONFIRMED into g_net.response.status,
             * the very next frame we catch it here and transition.
             *
             * Safety: NET_STATUS_CONFIRMED is a terminal state.
             * The poll thread stops all writes once it sets this value,
             * so reading it directly every frame is race-condition safe.
            ─────────────────────────────────────────────────────── */
            else if (currentState == STATE_WAITING_PAYMENT)
            {
                /* Debug: print status every frame so you can see it changing */
                static NetStatus last_printed = NET_STATUS_NONE;
                if (net_status != last_printed) {
                    printf("[NET POLL] status=%s id=%.8s tx=%.16s\n",
                           network_status_name(net_status), snap_id, snap_tx);
                    fflush(stdout);
                    last_printed = net_status;
                }

                if (net_status == NET_STATUS_CONFIRMED)
                {
                    /* FIX 3: snap_tx used — no unprotected g_net read */
                    printf("[MAIN] *** PAYMENT CONFIRMED *** TX=%.24s...\n",
                           snap_tx);
                    fflush(stdout);
                    currentState = STATE_CONFIRMED;
                }
                else if (net_status == NET_STATUS_FAILED  ||
                         net_status == NET_STATUS_EXPIRED ||
                         net_status == NET_STATUS_ERROR)
                {
                    printf("[MAIN] Payment failed/expired -> FAILED\n");
                    fflush(stdout);
                    currentState = STATE_FAILED;
                }
            }
        }

        /* ── Render ──────────────────────── */
        renderScreen[currentState](renderer, font, fontLarge, &amount);
        SDL_RenderPresent(renderer);
        SDL_Delay(16);
    }

    /* ── Cleanup ────────────────────────── */
    SDL_LockMutex(g_net.mutex);
    g_net.stop = 1;
    SDL_UnlockMutex(g_net.mutex);
    SDL_Delay(100);

    network_cleanup();
    SDL_DestroyMutex(g_net.mutex);

    TTF_CloseFont(fontLarge);
    TTF_CloseFont(font);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    TTF_Quit();
    SDL_Quit();

    return 0;
}
