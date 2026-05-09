/*
 * network.c — ChainEngineers Payment Terminal
 * Raw socket HTTP client. No libcurl. No extra DLLs.
 * Works on Windows (Winsock2) and Linux/Mac (POSIX).
 */

#include "network.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* ─────────────────────────────────────────────
   PLATFORM SOCKET SETUP
───────────────────────────────────────────── */
#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")
    typedef SOCKET sock_t;
    #define SOCK_INVALID  INVALID_SOCKET
    #define SOCK_ERR      SOCKET_ERROR
    #define sock_close(s) closesocket(s)
    #define sock_errno    WSAGetLastError()
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #include <errno.h>
    typedef int sock_t;
    #define SOCK_INVALID  (-1)
    #define SOCK_ERR      (-1)
    #define sock_close(s) close(s)
    #define sock_errno    errno
#endif

/* ─────────────────────────────────────────────
   INTERNAL CONSTANTS
───────────────────────────────────────────── */
#define HTTP_BUF_SIZE    4096
#define POLL_INTERVAL_MS 2000
#define POLL_MAX_TRIES   60

/* ─────────────────────────────────────────────
   PUBLIC: network_init
───────────────────────────────────────────── */
int network_init(void)
{
#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2,2), &wsa) != 0) {
        fprintf(stderr, "[NET] WSAStartup failed: %d\n", WSAGetLastError());
        return -1;
    }
#endif
    printf("[NET] Network layer initialized (%s:%d)\n", NET_HOST, NET_PORT);
    return 0;
}

/* ─────────────────────────────────────────────
   PUBLIC: network_cleanup
───────────────────────────────────────────── */
void network_cleanup(void)
{
#ifdef _WIN32
    WSACleanup();
#endif
    printf("[NET] Network layer shut down\n");
}

/* ─────────────────────────────────────────────
   INTERNAL: open a TCP socket to backend
───────────────────────────────────────────── */
static sock_t open_connection(void)
{
    sock_t s = socket(AF_INET, SOCK_STREAM, 0);
    if (s == SOCK_INVALID) return SOCK_INVALID;

#ifdef _WIN32
    DWORD tv = NET_TIMEOUT_SEC * 1000;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, (const char*)&tv, sizeof(tv));
#else
    struct timeval tv = { NET_TIMEOUT_SEC, 0 };
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
#endif

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(NET_PORT);
    addr.sin_addr.s_addr = inet_addr(NET_HOST);

    if (connect(s, (struct sockaddr*)&addr, sizeof(addr)) == SOCK_ERR) {
        fprintf(stderr, "[NET] Connection refused — is the backend running?\n");
        sock_close(s);
        return SOCK_INVALID;
    }
    return s;
}

/* ─────────────────────────────────────────────
   INTERNAL: send HTTP request and read response
───────────────────────────────────────────── */
static int http_request(const char *method, const char *path,
                         const char *body,
                         char *response_out, int response_max)
{
    sock_t s = open_connection();
    if (s == SOCK_INVALID) return -1;

    char request[HTTP_BUF_SIZE];
    if (body && strlen(body) > 0) {
        snprintf(request, sizeof(request),
            "%s %s HTTP/1.0\r\n"
            "Host: %s:%d\r\n"
            "Content-Type: application/json\r\n"
            "Content-Length: %d\r\n"
            "Connection: close\r\n"
            "\r\n"
            "%s",
            method, path, NET_HOST, NET_PORT, (int)strlen(body), body);
    } else {
        snprintf(request, sizeof(request),
            "%s %s HTTP/1.0\r\n"
            "Host: %s:%d\r\n"
            "Connection: close\r\n"
            "\r\n",
            method, path, NET_HOST, NET_PORT);
    }

    if (send(s, request, (int)strlen(request), 0) == SOCK_ERR) {
        fprintf(stderr, "[NET] Send failed\n");
        sock_close(s);
        return -1;
    }

    char raw[HTTP_BUF_SIZE * 2];
    memset(raw, 0, sizeof(raw));
    int total = 0, bytes;
    while ((bytes = recv(s, raw + total, sizeof(raw) - total - 1, 0)) > 0) {
        total += bytes;
    }
    sock_close(s);

    if (total == 0) return -1;

    int status_code = 0;
    sscanf(raw, "HTTP/%*s %d", &status_code);

    char *body_start = strstr(raw, "\r\n\r\n");
    if (!body_start) return status_code;
    body_start += 4;

    strncpy(response_out, body_start, response_max - 1);
    response_out[response_max - 1] = '\0';

    return status_code;
}

/* ─────────────────────────────────────────────
   INTERNAL: minimal JSON string extractor
───────────────────────────────────────────── */
static int json_extract_string(const char *json, const char *key,
                                char *out, int out_len)
{
    char pattern[64];
    snprintf(pattern, sizeof(pattern), "\"%s\":", key);

    const char *pos = strstr(json, pattern);
    if (!pos) return 0;

    pos += strlen(pattern);
    while (*pos == ' ') pos++;

    if (*pos == '"') {
        pos++;
        int i = 0;
        while (*pos && *pos != '"' && i < out_len - 1) {
            out[i++] = *pos++;
        }
        out[i] = '\0';
        return i > 0;
    } else {
        int i = 0;
        while (*pos && *pos != ',' && *pos != '}' && *pos != '\n'
               && i < out_len - 1) {
            out[i++] = *pos++;
        }
        while (i > 0 && (out[i-1] == ' ' || out[i-1] == '\r')) i--;
        out[i] = '\0';
        return i > 0;
    }
}

/* ─────────────────────────────────────────────
   INTERNAL: parse status string to NetStatus
───────────────────────────────────────────── */
static NetStatus parse_status(const char *s)
{
    if (strcmp(s, "pending")   == 0) return NET_STATUS_PENDING;
    if (strcmp(s, "confirmed") == 0) return NET_STATUS_CONFIRMED;
    if (strcmp(s, "failed")    == 0) return NET_STATUS_FAILED;
    if (strcmp(s, "expired")   == 0) return NET_STATUS_EXPIRED;
    return NET_STATUS_ERROR;
}

/* ─────────────────────────────────────────────
   PUBLIC: network_health_check
───────────────────────────────────────────── */
int network_health_check(void)
{
    char body[256];
    int code = http_request("GET", "/health", NULL, body, sizeof(body));
    return (code == 200);
}

/* ─────────────────────────────────────────────
   PUBLIC: network_create_payment
───────────────────────────────────────────── */
int network_create_payment(int amount_naira, PaymentResponse *out)
{
    memset(out, 0, sizeof(PaymentResponse));
    out->status = NET_STATUS_ERROR;

    char post_body[64];
    snprintf(post_body, sizeof(post_body), "{\"amount\":%d}", amount_naira);

    char response[HTTP_BUF_SIZE];
    int code = http_request("POST", "/payment/create",
                             post_body, response, sizeof(response));

    if (code != 200) {
        snprintf(out->error_msg, sizeof(out->error_msg),
                 "HTTP %d from /payment/create", code);
        fprintf(stderr, "[NET] create_payment failed: %s\n", out->error_msg);
        return -1;
    }

    printf("[NET] /payment/create response:\n%s\n", response);

    char status_str[32] = {0};
    json_extract_string(response, "payment_id",  out->payment_id,  NET_ID_LEN);
    json_extract_string(response, "qr_data",     out->qr_data,     NET_QR_LEN);
    json_extract_string(response, "amount_sol",  out->amount_sol,  NET_SOL_LEN);
    json_extract_string(response, "status",      status_str,       sizeof(status_str));

    out->status = parse_status(status_str);

    printf("[NET] Payment created: ID=%s SOL=%s\n",
           out->payment_id, out->amount_sol);
    return 0;
}

/* ─────────────────────────────────────────────
   PUBLIC: network_poll_status
───────────────────────────────────────────── */
int network_poll_status(const char *payment_id, PaymentResponse *out)
{
    memset(out, 0, sizeof(PaymentResponse));
    out->status = NET_STATUS_ERROR;
    strncpy(out->payment_id, payment_id, NET_ID_LEN - 1);

    char path[128];
    snprintf(path, sizeof(path), "/payment/%s/status", payment_id);

    char response[HTTP_BUF_SIZE];
    int code = http_request("GET", path, NULL, response, sizeof(response));

    if (code != 200) {
        snprintf(out->error_msg, sizeof(out->error_msg),
                 "HTTP %d from %s", code, path);
        fprintf(stderr, "[NET] poll_status failed: %s\n", out->error_msg);
        return -1;
    }

    char status_str[32] = {0};
    json_extract_string(response, "status",       status_str,        sizeof(status_str));
    json_extract_string(response, "tx_signature", out->tx_signature, NET_SIG_LEN);
    json_extract_string(response, "amount_sol",   out->amount_sol,   NET_SOL_LEN);

    out->status = parse_status(status_str);

    printf("[NET] Poll: ID=%s status=%s\n", payment_id, status_str);
    return 0;
}

/* ─────────────────────────────────────────────
   PUBLIC: network_thread_create
───────────────────────────────────────────── */
int network_thread_create(void *data)
{
    CreateThreadData *d = (CreateThreadData*)data;
    PaymentResponse result;

    int ok = network_create_payment(d->amount_naira, &result);

    SDL_LockMutex(d->result->mutex);
    d->result->response = result;
    d->result->ready    = 1;
    if (ok != 0) {
        d->result->response.status = NET_STATUS_ERROR;
    }
    SDL_UnlockMutex(d->result->mutex);

    free(d);
    return 0;
}

/* ─────────────────────────────────────────────
   PUBLIC: network_thread_poll
───────────────────────────────────────────── */
int network_thread_poll(void *data)
{
    PollThreadData *d = (PollThreadData*)data;
    int tries = 0;

    while (tries < POLL_MAX_TRIES)
    {
        SDL_LockMutex(d->result->mutex);
        int should_stop = d->result->stop;
        SDL_UnlockMutex(d->result->mutex);
        if (should_stop) break;

        SDL_Delay(POLL_INTERVAL_MS);
        tries++;

        PaymentResponse poll_result;
        int ok = network_poll_status(d->payment_id, &poll_result);

        SDL_LockMutex(d->result->mutex);

        if (ok != 0) {
            SDL_UnlockMutex(d->result->mutex);
            continue;
        }

        d->result->response.status = poll_result.status;
        if (poll_result.tx_signature[0]) {
            strncpy(d->result->response.tx_signature,
                    poll_result.tx_signature, NET_SIG_LEN - 1);
        }

        int done = (poll_result.status == NET_STATUS_CONFIRMED ||
                    poll_result.status == NET_STATUS_FAILED    ||
                    poll_result.status == NET_STATUS_EXPIRED);

        if (done) d->result->ready = 1;

        SDL_UnlockMutex(d->result->mutex);

        if (done) break;
    }

    /* Signal expiry if never confirmed */
    SDL_LockMutex(d->result->mutex);
    if (!d->result->ready)
    {
        d->result->response.status = NET_STATUS_EXPIRED;
        d->result->ready = 1;
        printf("[NET] Poll timeout — payment expired after %d tries\n", tries);
    }
    SDL_UnlockMutex(d->result->mutex);

    d->result->polling = 0;
    free(d);
    return 0;
}

/* ─────────────────────────────────────────────
   PUBLIC: network_status_name
───────────────────────────────────────────── */
const char *network_status_name(NetStatus status)
{
    switch (status) {
        case NET_STATUS_NONE:      return "NONE";
        case NET_STATUS_PENDING:   return "PENDING";
        case NET_STATUS_CONFIRMED: return "CONFIRMED";
        case NET_STATUS_FAILED:    return "FAILED";
        case NET_STATUS_EXPIRED:   return "EXPIRED";
        case NET_STATUS_ERROR:     return "ERROR";
        default:                   return "UNKNOWN";
    }
}
