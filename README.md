# ChainEngineers — Solana Embedded Payment Terminal

> **Dev3Pack Global Hackathon 2025 — Solana Track**

A low-cost embedded payment terminal that brings real Solana transactions into physical commerce. Built as a **Digital Twin simulator** in C with SDL2, communicating with a Node.js backend and confirming real transactions on **Solana Devnet**.

---

## The Problem

Small merchants in emerging economies like Nigeria have no access to affordable decentralized payment infrastructure. Existing crypto payment systems are:
- Software-only and technically intimidating
- Dependent on expensive centralized hardware
- Impractical for local markets, campuses, and transport systems

## The Solution

ChainEngineers is an embedded Solana payment terminal where:
- Merchants enter an amount in **Naira**
- The system converts to **SOL** and generates a real **Solana Pay QR code**
- The customer scans with **Phantom wallet** and sends real SOL
- The terminal detects the **on-chain transaction** and confirms with a real **TX signature**

---

## Demo

| Screen | Description |
|---|---|
| IDLE | Splash screen, waiting for merchant |
| ENTER AMOUNT | Merchant keys in Naira amount |
| PROCESSING | Terminal connects to backend, creates payment session |
| WAITING | Real QR code displayed, polling Solana Devnet |
| CONFIRMED | Payment verified on-chain, TX signature displayed |
| FAILED | Timeout or rejection handling |

**Terminal Wallet Address (Devnet):**
```
AiYTy2PLhJLsDRnFMNV886Y6fHvfb9sRouiLkgkKnGdx
```
Verify on Solana Explorer:
https://explorer.solana.com/address/AiYTy2PLhJLsDRnFMNV886Y6fHvfb9sRouiLkgkKnGdx?cluster=devnet

---

## Tech Stack

| Layer | Technology |
|---|---|
| Terminal UI | C + SDL2 + SDL2_ttf |
| QR Code | qrcodegen (Nayuki, pure C) |
| Networking | Raw Winsock2 sockets + SDL_Thread + SDL_mutex |
| Backend | Node.js + Express |
| Blockchain | Solana Devnet via @solana/web3.js |
| Platform | Windows, compiled with MinGW GCC |

---

## Architecture

```
┌─────────────────────┐        HTTP/TCP        ┌──────────────────────┐
│   SDL2 Terminal      │ ──────────────────────▶ │   Node.js Backend    │
│   (C + Winsock2)    │                         │   (Express API)      │
│                     │ ◀────────────────────── │                      │
│  6-State Machine:   │    payment_id + status  │  Solana Devnet       │
│  IDLE               │                         │  Transaction Monitor │
│  ENTER_AMOUNT       │                         │                      │
│  PROCESSING         │                         └──────────────────────┘
│  WAITING_PAYMENT    │                                    │
│  CONFIRMED          │                                    ▼
│  FAILED             │                         ┌──────────────────────┐
└─────────────────────┘                         │   Solana Devnet      │
                                                │   Real TX Confirmed  │
                                                └──────────────────────┘
```

---

## Payment Flow

```
1. Merchant enters amount in Naira
2. Terminal sends POST /payment/create via raw TCP socket
3. Backend generates payment session + Solana Pay QR data
4. QR code rendered live on SDL2 terminal window
5. Customer scans QR with Phantom wallet
6. Real SOL sent on Solana Devnet
7. Backend detects new transaction on terminal address
8. SDL2 poll thread receives confirmed status
9. Terminal transitions to CONFIRMED screen
10. Real TX signature displayed to merchant
```

---

## Backend API

```
POST /payment/create      — create payment session
GET  /payment/:id/status  — poll for confirmation
GET  /health              — server health + devnet status
```

---

## Project Structure

```
chainengineers/
├── Dev3Pack.c          — main loop, state machine, thread coordination
├── ui/
│   ├── ui.c            — SDL2 render dispatch, 6 screen renderers
│   └── ui.h
├── states/
│   ├── states.c        — state machine, AmountInput struct
│   └── states.h
├── network/
│   ├── network.c       — raw HTTP client, SDL threads, mutex shared state
│   └── network.h
├── qr/
│   ├── qrcodegen.c     — Nayuki QR encoder (pure C)
│   └── qrcodegen.h
├── backend/
│   ├── server.js       — Express + Solana Devnet monitor
│   └── package.json
└── assets/
    └── Roboto_Condensed-Regular.ttf
```

---

## How to Run

### Backend
```bash
cd backend
npm install
node server.js
```

### Terminal (Windows + MinGW)
```bash
gcc Dev3Pack.c ui\ui.c states\states.c network\network.c qr\qrcodegen.c -o chainengineers.exe -I. -L. -lmingw32 -lSDL2main -lSDL2 -lSDL2_ttf -lws2_32 -lgdi32 -lole32 -lusp10 -mwindows
./chainengineers.exe
```

**Requirements:**
- MinGW GCC
- SDL2 + SDL2_ttf
- Node.js
- Phantom wallet with Devnet SOL (get free SOL at https://faucet.solana.com)

---

## Hardware Vision (Next Phase)

This Digital Twin validates the full payment architecture before physical deployment.

| Digital Twin | Real Hardware |
|---|---|
| SDL2 window | ILI9341 TFT LCD Display |
| Keyboard input | Physical keypad + buttons |
| Winsock2 HTTP | ESP32 WiFi stack |
| SDL_Thread | FreeRTOS task |
| QR on screen | GM65 QR scanner module |
| Status colors | LED indicators + buzzer |

**Target hardware:** ESP32-WROOM-32 + ILI9341 TFT + PN532 NFC + LiPo battery

---

## Why Solana

- **Sub-second confirmation** — essential for point-of-sale UX
- **Near-zero fees** — practical for small merchants
- **Solana Pay standard** — QR-based payment protocol built-in
- **Scalable infrastructure** — ready for machine-to-machine payments

---

## Use Cases

- Campus payments
- Small retail businesses
- Event ticketing
- Transportation systems
- Community marketplaces
- Smart vending machines

---

## Team

**ChainEngineers** — Dev3Pack Global Hackathon 2025
Built in Nigeria 🇳🇬

---

## License

MIT License
