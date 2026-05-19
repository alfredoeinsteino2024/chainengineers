/*
 * ui.c — ChainEngineers Payment Terminal
 * Phase 2 update: renderWaitingScreen now renders a REAL QR code
 * using Nayuki qrcodegen library, replacing the [ QR CODE ] placeholder.
 * renderConfirmedScreen shows TX signature panel.
 */

#include "ui.h"
#include "../network/network.h"
#include "../qr/qrcodegen.h"
#include <string.h>
#include <stdio.h>

/* g_net is defined in Dev3Pack.c, declared extern here */
extern NetworkResult g_net;

/* ─────────────────────────────────────────────
   RENDER DISPATCH TABLE
───────────────────────────────────────────── */
RenderFn renderScreen[STATE_COUNT] = {
    [STATE_IDLE]            = renderIdleScreen,
    [STATE_ENTER_AMOUNT]    = renderEnterAmountScreen,
    [STATE_PROCESSING]      = renderProcessingScreen,
    [STATE_WAITING_PAYMENT] = renderWaitingScreen,
    [STATE_CONFIRMED]       = renderConfirmedScreen,
    [STATE_FAILED]          = renderFailedScreen,
    [STATE_HISTORY]         = renderHistoryScreen,
    [STATE_BALANCE]         = renderBalanceScreen,
};
/* ─────────────────────────────────────────────
   INTERNAL: draw text centered on X axis
───────────────────────────────────────────── */
static void drawTextCentered(SDL_Renderer *renderer, TTF_Font *font,
                              const char *text, SDL_Color color,
                              int screenW, int y)
{
    if (!text || !text[0]) return;
    SDL_Surface *surface = TTF_RenderUTF8_Blended(font, text, color);
    if (!surface) return;
    SDL_Texture *texture = SDL_CreateTextureFromSurface(renderer, surface);
    if (!texture) { SDL_FreeSurface(surface); return; }
    int w, h;
    SDL_QueryTexture(texture, NULL, NULL, &w, &h);
    SDL_Rect rect = {(screenW - w) / 2, y, w, h};
    SDL_RenderCopy(renderer, texture, NULL, &rect);
    SDL_FreeSurface(surface);
    SDL_DestroyTexture(texture);
}

/* ─────────────────────────────────────────────
   INTERNAL: draw text left-aligned
───────────────────────────────────────────── */
static void drawText(SDL_Renderer *renderer, TTF_Font *font,
                     const char *text, SDL_Color color, int x, int y)
{
    if (!text || !text[0]) return;
    SDL_Surface *surface = TTF_RenderUTF8_Blended(font, text, color);
    if (!surface) return;
    SDL_Texture *texture = SDL_CreateTextureFromSurface(renderer, surface);
    if (!texture) { SDL_FreeSurface(surface); return; }
    int w, h;
    SDL_QueryTexture(texture, NULL, NULL, &w, &h);
    SDL_Rect rect = {x, y, w, h};
    SDL_RenderCopy(renderer, texture, NULL, &rect);
    SDL_FreeSurface(surface);
    SDL_DestroyTexture(texture);
}

/* ─────────────────────────────────────────────
   INTERNAL: draw card background + accent bar
───────────────────────────────────────────── */
static void drawCard(SDL_Renderer *r, Uint8 ar, Uint8 ag, Uint8 ab)
{
    SDL_Rect box = {100, 60, 600, 360};
    SDL_SetRenderDrawColor(r, 30, 30, 30, 255);
    SDL_RenderFillRect(r, &box);
    SDL_Rect accent = {100, 60, 600, 6};
    SDL_SetRenderDrawColor(r, ar, ag, ab, 255);
    SDL_RenderFillRect(r, &accent);
}

/* ─────────────────────────────────────────────
   INTERNAL: render a real QR code using qrcodegen
   Draws QR centered at (cx, cy).
   pixel_size = SDL pixels per QR module.
───────────────────────────────────────────── */
static void renderQRCode(SDL_Renderer *renderer,
                          const char *text,
                          int cx, int cy,
                          int pixel_size)
{
    if (!text || !text[0]) return;

    uint8_t qrcode[qrcodegen_BUFFER_LEN_MAX];
    uint8_t tempBuf[qrcodegen_BUFFER_LEN_MAX];

    bool ok = qrcodegen_encodeText(
        text,
        tempBuf, qrcode,
        qrcodegen_Ecc_MEDIUM,
        qrcodegen_VERSION_MIN,
        qrcodegen_VERSION_MAX,
        qrcodegen_Mask_AUTO,
        true
    );

    if (!ok) {
        /* Fallback: red error box */
        SDL_SetRenderDrawColor(renderer, 255, 80, 80, 255);
        SDL_Rect box = {cx - 40, cy - 40, 80, 80};
        SDL_RenderDrawRect(renderer, &box);
        return;
    }

    int size   = qrcodegen_getSize(qrcode);
    int total  = size * pixel_size;
    int startX = cx - total / 2;
    int startY = cy - total / 2;

    /* White quiet zone background */
    int padding = pixel_size * 2;
    SDL_Rect bg = {
        startX - padding,
        startY - padding,
        total  + padding * 2,
        total  + padding * 2
    };
    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
    SDL_RenderFillRect(renderer, &bg);

    /* Draw each module */
    for (int y = 0; y < size; y++) {
        for (int x = 0; x < size; x++) {
            if (qrcodegen_getModule(qrcode, x, y)) {
                SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
            } else {
                SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
            }
            SDL_Rect module = {
                startX + x * pixel_size,
                startY + y * pixel_size,
                pixel_size,
                pixel_size
            };
            SDL_RenderFillRect(renderer, &module);
        }
    }
}

/* ─────────────────────────────────────────────
   SCREEN: IDLE
───────────────────────────────────────────── */
void renderIdleScreen(SDL_Renderer *renderer, TTF_Font *font,
                      TTF_Font *fontLarge, const AmountInput *amount)
{
    SDL_SetRenderDrawColor(renderer, 15, 15, 15, 255);
    SDL_RenderClear(renderer);
    drawCard(renderer, 138, 43, 226);

    SDL_Color white  = {255, 255, 255, 255};
    SDL_Color gray   = {160, 160, 160, 255};
    SDL_Color purple = {180, 100, 255, 255};

    drawTextCentered(renderer, font, "CHAINENGINEERS",       white,  800, 130);
    drawTextCentered(renderer, font, "PAYMENT TERMINAL",     purple, 800, 175);
    drawTextCentered(renderer, font, "Solana Devnet",        gray,   800, 240);
    drawTextCentered(renderer, font, "Press ENTER to start", gray,   800, 340);
}

/* ─────────────────────────────────────────────
   SCREEN: ENTER AMOUNT
───────────────────────────────────────────── */
void renderEnterAmountScreen(SDL_Renderer *renderer, TTF_Font *font,
                              TTF_Font *fontLarge, const AmountInput *amount)
{
    SDL_SetRenderDrawColor(renderer, 15, 15, 15, 255);
    SDL_RenderClear(renderer);
    drawCard(renderer, 0, 200, 100);

    SDL_Color white = {255, 255, 255, 255};
    SDL_Color gray  = {160, 160, 160, 255};
    SDL_Color green = {0,   220, 120, 255};

    drawTextCentered(renderer, font, "ENTER AMOUNT", white, 800, 80);

    SDL_Rect amtBox = {150, 150, 500, 120};
    SDL_SetRenderDrawColor(renderer, 20, 20, 20, 255);
    SDL_RenderFillRect(renderer, &amtBox);

    char display[32];
    formatAmount(amount, display, sizeof(display));
    char withCursor[36];
    Uint32 ticks = SDL_GetTicks();
    snprintf(withCursor, sizeof(withCursor),
             "%s%s", display, (ticks / 500) % 2 == 0 ? "|" : " ");

    drawTextCentered(renderer, fontLarge, withCursor,                   green, 800, 170);
    drawTextCentered(renderer, font,      "BACKSPACE to delete",        gray,  800, 300);
    drawTextCentered(renderer, font,      "ENTER confirm | ESC cancel", gray,  800, 340);
}

/* ─────────────────────────────────────────────
   SCREEN: PROCESSING
───────────────────────────────────────────── */
void renderProcessingScreen(SDL_Renderer *renderer, TTF_Font *font,
                             TTF_Font *fontLarge, const AmountInput *amount)
{
    SDL_SetRenderDrawColor(renderer, 15, 15, 15, 255);
    SDL_RenderClear(renderer);
    drawCard(renderer, 255, 180, 0);

    SDL_Color white = {255, 255, 255, 255};
    SDL_Color amber = {255, 200,  50, 255};
    SDL_Color gray  = {160, 160, 160, 255};

    Uint32 ticks = SDL_GetTicks();
    int    dots  = (ticks / 400) % 4;
    char   label[32];
    snprintf(label, sizeof(label), "PROCESSING%.*s", dots, "...");

    drawTextCentered(renderer, fontLarge, label,                      amber, 800, 150);
    drawTextCentered(renderer, font,      "Connecting to backend...", white, 800, 250);
    drawTextCentered(renderer, font,      "Creating Solana payment",  gray,  800, 290);
    drawTextCentered(renderer, font,      "[P] confirm  [F] fail  (debug)", gray, 800, 370);
}

/* ─────────────────────────────────────────────
   SCREEN: WAITING FOR PAYMENT
   Renders REAL QR code from qr_data.
   Left: text info   Right: QR code
───────────────────────────────────────────── */
void renderWaitingScreen(SDL_Renderer *renderer, TTF_Font *font,
                          TTF_Font *fontLarge, const AmountInput *amount)
{
    SDL_SetRenderDrawColor(renderer, 15, 15, 15, 255);
    SDL_RenderClear(renderer);
    drawCard(renderer, 0, 150, 255);

    SDL_Color white  = {255, 255, 255, 255};
    SDL_Color blue   = {100, 180, 255, 255};
    SDL_Color gray   = {160, 160, 160, 255};
    SDL_Color purple = {180, 100, 255, 255};
    SDL_Color amber  = {255, 200,  50, 255};

    /* Safely read shared network data */
    char pay_id[NET_ID_LEN]   = "Generating...";
    char sol_amt[NET_SOL_LEN] = "";
    char qr_data[NET_QR_LEN]  = "";

    SDL_LockMutex(g_net.mutex);
    if (g_net.response.payment_id[0])
        strncpy(pay_id,  g_net.response.payment_id, NET_ID_LEN  - 1);
    if (g_net.response.amount_sol[0])
        strncpy(sol_amt, g_net.response.amount_sol, NET_SOL_LEN - 1);
    if (g_net.response.qr_data[0])
        strncpy(qr_data, g_net.response.qr_data,   NET_QR_LEN  - 1);
    SDL_UnlockMutex(g_net.mutex);

    char display[32];
    formatAmount(amount, display, sizeof(display));

    Uint32 ticks = SDL_GetTicks();
    const char *dot_frames[] = {"WAITING.  ", "WAITING.. ", "WAITING..."};
    const char *dots = dot_frames[(ticks / 500) % 3];

    /* Title — full width centered */
    drawTextCentered(renderer, font, "WAITING FOR PAYMENT", white, 800, 75);

    /* FIX 1: Amount centered on left panel (x=100 to x=490, center=295)
     * screenW=590 → text center always lands at x=295
     * Works correctly for any digit length from N1 to N99999999 */
    drawTextCentered(renderer, fontLarge, display, blue, 590, 110);

    if (sol_amt[0]) {
        char sol_line[32];
        snprintf(sol_line, sizeof(sol_line), "%s SOL", sol_amt);
        /* Match SOL line to same center as amount above */
        drawTextCentered(renderer, font, sol_line, amber, 590, 185);
    }

    /* FIX 2: ID panel shifted rightward for balance
     * Old box: x=120, w=440 → center at x=340, text center at x=230 (mismatch)
     * New box: x=140, w=400 → center at x=340, text screenW=680 → center at x=340 (aligned) */
    SDL_Rect idBox = {140, 215, 400, 44};
    SDL_SetRenderDrawColor(renderer, 20, 20, 40, 255);
    SDL_RenderFillRect(renderer, &idBox);
    SDL_SetRenderDrawColor(renderer, 0, 100, 200, 255);
    SDL_RenderDrawRect(renderer, &idBox);

    char id_label[48];
    snprintf(id_label, sizeof(id_label), "ID: %.16s", pay_id);
    /* screenW=680 → text center at x=340, matching box center exactly */
    drawTextCentered(renderer, font, id_label, purple, 740, 228);

    /* Waiting dots and cancel hint — aligned to left panel center */
    drawTextCentered(renderer, font, dots,            blue, 660, 275);
    drawTextCentered(renderer, font, "ESC to cancel", gray, 1220, 355);

    /* Right side: REAL QR code centered at (610, 310) */
    if (qr_data[0]) {
        renderQRCode(renderer, qr_data, 610, 330, 3);

        /* FIX 3: "Scan to pay" moved up and centered under QR
         * QR center x=610 → screenW=1220 centers text at x=610 regardless of text width
         * y=370 puts it just below the QR bottom edge (~365px) */
        drawTextCentered(renderer, font, "Scan to pay", gray, 680, 350);
    } else {
        /* Still generating — placeholder */
        SDL_Rect qrBox = {510, 250, 140, 100};
        SDL_SetRenderDrawColor(renderer, 40, 40, 60, 255);
        SDL_RenderFillRect(renderer, &qrBox);
        SDL_SetRenderDrawColor(renderer, 100, 100, 100, 255);
        SDL_RenderDrawRect(renderer, &qrBox);
        drawTextCentered(renderer, font, "[ QR CODE ]", gray, 800, 295);
    }
}

/* ─────────────────────────────────────────────
   SCREEN: CONFIRMED
───────────────────────────────────────────── */
void renderConfirmedScreen(SDL_Renderer *renderer, TTF_Font *font,
                            TTF_Font *fontLarge, const AmountInput *amount)
{
    SDL_SetRenderDrawColor(renderer, 15, 15, 15, 255);
    SDL_RenderClear(renderer);

    SDL_Rect box = {100, 60, 600, 360};
    SDL_SetRenderDrawColor(renderer, 20, 50, 20, 255);
    SDL_RenderFillRect(renderer, &box);
    SDL_Rect accent = {100, 60, 600, 6};
    SDL_SetRenderDrawColor(renderer, 0, 220, 100, 255);
    SDL_RenderFillRect(renderer, &accent);

    SDL_Color white = {255, 255, 255, 255};
    SDL_Color green = {0,   220, 120, 255};
    SDL_Color gray  = {160, 160, 160, 255};
    SDL_Color amber = {255, 200,  50, 255};

    char tx[NET_SIG_LEN]      = "";
    char sol_amt[NET_SOL_LEN] = "";
    SDL_LockMutex(g_net.mutex);
    if (g_net.response.tx_signature[0])
        strncpy(tx,      g_net.response.tx_signature, NET_SIG_LEN  - 1);
    if (g_net.response.amount_sol[0])
        strncpy(sol_amt, g_net.response.amount_sol,   NET_SOL_LEN  - 1);
    SDL_UnlockMutex(g_net.mutex);

    char display[32];
    formatAmount(amount, display, sizeof(display));

    drawTextCentered(renderer, font,      "PAYMENT CONFIRMED", green, 800, 80);
    drawTextCentered(renderer, fontLarge, display,             green, 800, 130);

    if (sol_amt[0]) {
        char sol_line[32];
        snprintf(sol_line, sizeof(sol_line), "%s SOL", sol_amt);
        drawTextCentered(renderer, font, sol_line, amber, 800, 210);
    }

    drawTextCentered(renderer, font, "RECEIVED", white, 800, 250);

    if (tx[0]) {
        SDL_Rect txBox = {120, 275, 560, 50};
        SDL_SetRenderDrawColor(renderer, 10, 30, 10, 255);
        SDL_RenderFillRect(renderer, &txBox);
        SDL_SetRenderDrawColor(renderer, 0, 120, 60, 255);
        SDL_RenderDrawRect(renderer, &txBox);

        char tx_short[40];
        snprintf(tx_short, sizeof(tx_short), "TX: %.28s...", tx);
        drawTextCentered(renderer, font, tx_short, green, 800, 290);
    } else {
        drawTextCentered(renderer, font, "Transaction verified on-chain", gray, 800, 290);
    }

    drawTextCentered(renderer, font, "Press ENTER for new transaction", white, 800, 360);
}

/* ─────────────────────────────────────────────
   SCREEN: FAILED
───────────────────────────────────────────── */
void renderFailedScreen(SDL_Renderer *renderer, TTF_Font *font,
                         TTF_Font *fontLarge, const AmountInput *amount)
{
    SDL_SetRenderDrawColor(renderer, 15, 15, 15, 255);
    SDL_RenderClear(renderer);

    SDL_Rect box = {100, 60, 600, 360};
    SDL_SetRenderDrawColor(renderer, 50, 15, 15, 255);
    SDL_RenderFillRect(renderer, &box);
    SDL_Rect accent = {100, 60, 600, 6};
    SDL_SetRenderDrawColor(renderer, 220, 50, 50, 255);
    SDL_RenderFillRect(renderer, &accent);

    SDL_Color white = {255, 255, 255, 255};
    SDL_Color red   = {255,  80,  80, 255};
    SDL_Color gray  = {160, 160, 160, 255};

    char err_msg[64] = "";
    SDL_LockMutex(g_net.mutex);
    if (g_net.response.error_msg[0])
        strncpy(err_msg, g_net.response.error_msg, 63);
    SDL_UnlockMutex(g_net.mutex);

    drawTextCentered(renderer, fontLarge, "FAILED",              red,   800, 140);
    if (err_msg[0]) {
        drawTextCentered(renderer, font, err_msg,                gray,  800, 240);
    } else {
        drawTextCentered(renderer, font, "Transaction rejected", white, 800, 240);
    }
    drawTextCentered(renderer, font, "Press ENTER to try again", gray,  800, 330);
    drawTextCentered(renderer, font, "ESC to return to home",    gray,  800, 365);
}

/* ─────────────────────────────────────────────
   SCREEN: HISTORY
───────────────────────────────────────────── */
void renderHistoryScreen(SDL_Renderer *renderer, TTF_Font *font,
                          TTF_Font *fontLarge, const AmountInput *amount)
{
    SDL_SetRenderDrawColor(renderer, 15, 15, 15, 255);
    SDL_RenderClear(renderer);
    drawCard(renderer, 100, 80, 255);

    SDL_Color white  = {255, 255, 255, 255};
    SDL_Color gray   = {160, 160, 160, 255};
    SDL_Color green  = {0,   220, 120, 255};
    SDL_Color purple = {180, 100, 255, 255};
    SDL_Color amber  = {255, 200,  50, 255};

    drawTextCentered(renderer, font, "TRANSACTION HISTORY", white, 800, 75);

    /* Read history from g_net — guarded */
    extern TxHistory g_history;

    if (g_history.count == 0)
    {
        drawTextCentered(renderer, font, "No transactions yet", gray, 800, 220);
        drawTextCentered(renderer, font, "ESC to go back",      gray, 800, 380);
        return;
    }

    /* Show up to 4 most recent transactions */
    int start = g_history.count - 1;
    int shown = 0;
    int y     = 110;

    for (int i = start; i >= 0 && shown < 4; i--, shown++)
    {
        TxRecord *r = &g_history.records[i];

        /* Row background */
        SDL_Rect row = {115, y, 570, 56};
        SDL_SetRenderDrawColor(renderer, 25, 25, 40, 255);
        SDL_RenderFillRect(renderer, &row);
        SDL_SetRenderDrawColor(renderer, 50, 50, 80, 255);
        SDL_RenderDrawRect(renderer, &row);

        /* Amount */
        char naira_str[32];
        snprintf(naira_str, sizeof(naira_str), "N%s", r->naira);
        drawText(renderer, font, naira_str, green, 130, y + 8);

        /* SOL amount */
        char sol_str[32];
        snprintf(sol_str, sizeof(sol_str), "%s SOL", r->amount_sol);
        drawText(renderer, font, sol_str, amber, 130, y + 30);

        /* TX signature truncated */
        char tx_str[32];
        snprintf(tx_str, sizeof(tx_str), "TX: %.20s...", r->tx_signature);
        drawText(renderer, font, tx_str, purple, 310, y + 8);

        /* Timestamp */
        drawText(renderer, font, r->timestamp, gray, 310, y + 30);

        y += 64;
    }

    char count_str[32];
    snprintf(count_str, sizeof(count_str), "Total: %d transaction%s",
             g_history.count, g_history.count == 1 ? "" : "s");
    drawTextCentered(renderer, font, count_str, gray,  800, 378);
    drawTextCentered(renderer, font, "ESC to go back", gray, 800, 408);
}

/* ─────────────────────────────────────────────
   SCREEN: BALANCE
───────────────────────────────────────────── */
void renderBalanceScreen(SDL_Renderer *renderer, TTF_Font *font,
                          TTF_Font *fontLarge, const AmountInput *amount)
{
    SDL_SetRenderDrawColor(renderer, 15, 15, 15, 255);
    SDL_RenderClear(renderer);
    drawCard(renderer, 0, 200, 180);

    SDL_Color white = {255, 255, 255, 255};
    SDL_Color gray  = {160, 160, 160, 255};
    SDL_Color teal  = {0,   220, 180, 255};
    SDL_Color amber = {255, 200,  50, 255};

    extern char g_balance_sol[32];
    extern char g_balance_naira[32];

    drawTextCentered(renderer, font,      "TERMINAL BALANCE",  white, 800, 100);
    drawTextCentered(renderer, fontLarge, g_balance_sol[0] ? g_balance_sol : "...", teal, 800, 160);
    drawTextCentered(renderer, font,      "SOL",               gray,  800, 240);

    if (g_balance_naira[0]) {
        char naira_line[48];
        snprintf(naira_line, sizeof(naira_line), "≈ %s NGN", g_balance_naira);
        drawTextCentered(renderer, font, naira_line, amber, 800, 280);
    }

    drawTextCentered(renderer, font, "Solana Devnet",   gray, 800, 330);
    drawTextCentered(renderer, font, "ESC to go back",  gray, 800, 390);
}
