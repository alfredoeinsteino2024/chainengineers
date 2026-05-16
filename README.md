# ChainEngineers - Solana Embedded Payment Terminal

> **Dev3Pack Global Hackathon 2026 - Solana Track**

A low-cost embedded payment terminal that brings real Solana transactions into physical commerce. Built as a **Digital Twin simulator** in C with SDL2, communicating with a Node.js backend and confirming real transactions on **Solana Devnet**.

---

## The Problem

Small merchants in emerging economies like Nigeria have no access to affordable decentralized payment infrastructure. Existing crypto payment systems are:
- Software-only and technically intimidating
- Dependent on expensive centralized hardware
- Impractical for local markets, campuses, and transport systems
- Inaccessible to customers who don't own crypto wallets

## The Solution

ChainEngineers is an embedded Solana payment terminal that operates in **two modes simultaneously**:

**Active Mode** - Merchant initiates payment:
- Merchants enter an amount in **Naira**
- The system converts to **SOL** and generates a real **Solana Pay QR code**
- The customer scans with **Phantom wallet** and sends real SOL
- The terminal detects the **on-chain transaction** and confirms with a real **TX signature**

**Passive Mode** - Always-on background receiver:
- Background thread monitors wallet 24/7
- Detects any incoming SOL even when terminal is IDLE
- Automatically records to history and transitions to CONFIRMED
- Behaves exactly like a real embedded POS terminal

---

## Key Differentiator - Dual Mode Architecture

```
┌─────────────────────────────────────────────────────────┐
│              ChainEngineers Terminal                     │
│                                                         │
│   ACTIVE MODE              PASSIVE MODE                 │
│   ───────────              ────────────                 │
│   Merchant enters amount   Background thread runs 24/7  │
│   QR code generated        Polls /wallet/balance        │
│   Customer scans & pays    Detects any incoming SOL     │
│   Terminal confirms        Auto-records to history      │
│                            Auto-transitions to CONFIRMED │
└─────────────────────────────────────────────────────────┘
```

This makes ChainEngineers behave exactly like a real POS terminal - money can arrive at any time without the merchant needing to initiate a transaction.

---

## Demo

🎥 **Demo Video:** https://www.loom.com/share/e6a757cfbf9748fab841fae9ae3bcf45

### Terminal State Machine (8 States)

```
STATE_IDLE → STATE_ENTER_AMOUNT → STATE_PROCESSING
          → STATE_WAITING_PAYMENT → STATE_CONFIRMED → STATE_IDLE
                                 → STATE_FAILED    → STATE_IDLE
STATE_IDLE → STATE_HISTORY → STATE_IDLE
STATE_IDLE → STATE_BALANCE → STATE_IDLE
```

| State | Description |
|---|---|
| IDLE | Splash screen, waiting for merchant |
| ENTER_AMOUNT | Merchant keys in Naira amount |
| PROCESSING | Terminal connects to backend, creates payment session |
| WAITING_PAYMENT | Real QR code displayed, polling Solana Devnet |
| CONFIRMED | Payment verified on-chain, TX signature displayed |
| FAILED | Timeout or rejection handling |
| HISTORY | Last 4 confirmed transactions with TX signatures |
| BALANCE | Current SOL balance + Naira equivalent |

**Terminal Wallet Address (Devnet):**
```
AiYTy2PLhJLsDRnFMNV886Y6fHvfb9sRouiLkgkKnGdx
```
Verify on Solana Explorer:
https://explorer.solana.com/address/AiYTy2PLhJLsDRnFMNV886Y6fHvfb9sRouiLkgkKnGdx?cluster=devnet

---

## Features

### Transaction History (`H` key from IDLE or CONFIRMED)
- Displays up to 4 most recent confirmed transactions
- Each row shows: Naira amount, SOL equivalent, truncated TX signature, timestamp
- Arrow keys navigate between records
- Auto-populates after every confirmed payment

### Balance Screen (`B` key from IDLE)
- Shows current SOL balance of terminal wallet
- Shows Naira equivalent
- Updates automatically after every confirmed payment

### Passive Payment Monitor
- Background SDL thread runs continuously
- Polls `/wallet/balance` every 10 seconds
- Detects incoming SOL even when terminal is IDLE
- Automatically records to history and updates balance
- Transitions terminal to CONFIRMED screen on detection

### Key Controls
```
ENTER    - start new payment
H        - view transaction history
B        - view wallet balance
ESC      - cancel / return to IDLE
↑ ↓      - navigate history records
```

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
│  8-State Machine    │    payment_id + status  │  Solana Devnet       │
│  Active + Passive   │    balance + history    │  Transaction Monitor │
│  Payment Modes      │                         │  Balance Query       │
└─────────────────────┘                         └──────────────────────┘
        │                                                   │
        │ Passive Monitor Thread                            ▼
        │ (polls every 10s)                    ┌──────────────────────┐
        └──────────────────────────────────────│   Solana Devnet      │
                                               │   Real TX Confirmed  │
                                               └──────────────────────┘
```

---

## Payment Flow

### Active Mode
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
10. Real TX signature displayed and saved to history
```

### Passive Mode
```
1. Background thread polls /wallet/balance every 10 seconds
2. New incoming SOL detected on terminal wallet
3. Transaction recorded to history automatically
4. Balance updated in real time
5. Terminal transitions to CONFIRMED screen
6. Merchant notified of incoming payment
```

---

## Backend API

```
POST /payment/create      - create payment session
GET  /payment/:id/status  - poll for confirmation
GET  /wallet/balance      - current SOL balance + latest TX
GET  /health              - server health + devnet status
```

---

## Project Structure

```
chainengineers/
├── Dev3Pack.c          - main loop, 8-state machine, dual-mode threading
├── ui/
│   ├── ui.c            - SDL2 render dispatch, 8 screen renderers
│   └── ui.h
├── states/
│   ├── states.c        - state machine, AmountInput, TxHistory structs
│   └── states.h
├── network/
│   ├── network.c       - raw HTTP client, SDL threads, mutex shared state
│   └── network.h
├── qr/
│   ├── qrcodegen.c     - Nayuki QR encoder (pure C)
│   └── qrcodegen.h
├── backend/
│   ├── server.js       - Express + Solana Devnet monitor + balance endpoint
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

## Roadmap

### ✅ Phase 1 - Concept & Architecture
- State machine design
- SDL2 simulator
- UI screens and rendering

### ✅ Phase 2 - Live Payment Flow (Current)
- Real Solana Pay QR code generation
- Raw TCP socket backend communication
- Live Solana Devnet transaction confirmation
- Real TX signature displayed on terminal
- Transaction history and balance screens
- Passive background payment monitor

### 🔜 Phase 3 - Fiat On-Ramp (SolaChain)
The biggest barrier to crypto adoption in Nigeria is that most people don't own crypto wallets. Phase 3 solves this with a **dual payment mode**:

- **Mode 1:** Customer scans QR code → pays SOL directly via Phantom wallet
- **Mode 2:** Customer sends NGN to a **SolaChain virtual bank account** → auto-converts to SOL → merchant receives SOL

Both modes show on the same terminal screen. The merchant always receives SOL regardless of how the customer pays. This means **any Nigerian with a bank account can pay at a ChainEngineers terminal** — no crypto wallet required.

**Target:** 40 million Nigerian micro-merchants currently excluded from digital payments.

### 🔜 Phase 4 - Hardware Deployment
Deploy on ESP32 hardware for real-world merchant locations.

---

## Hardware Vision

This Digital Twin validates the full payment architecture before physical deployment.

| Digital Twin | Real Hardware |
|---|---|
| SDL2 window | ILI9341 TFT LCD Display |
| Keyboard input | Physical keypad + buttons |
| Winsock2 HTTP | ESP32 WiFi stack |
| SDL_Thread | FreeRTOS task |
| QR on screen | GM65 QR scanner module |
| Passive monitor | Always-on FreeRTOS daemon task |
| Status colors | LED indicators + buzzer |

**Target hardware:** ESP32-WROOM-32 + ILI9341 TFT + PN532 NFC + LiPo battery

---

## Why Solana

- **Sub-second confirmation** - essential for point-of-sale UX
- **Near-zero fees** - practical for small merchants
- **Solana Pay standard** - QR-based payment protocol built-in
- **Scalable infrastructure** - ready for machine-to-machine payments

---

## Use Cases

- Campus payments
- Small retail businesses
- Event ticketing
- Transportation systems
- Community marketplaces
- Smart vending machines

---

## The Story

I walked into Dev3Pack Global Hackathon 2026 with no idea what I was going to build. I saw teams building web apps and asked myself what I could contribute with my background in C and embedded systems.

I noticed that crypto payments in Nigeria are still mostly software-only and inaccessible to everyday merchants. That gap felt real to me because I see it around me daily.

This was my first time writing real blockchain code. I researched as I built, figured things out step by step, and shipped a working system with real on-chain transactions in under 48 hours.

I am still a student. I am still learning. But I built something real that solves a real problem I see around me. That is why I want to keep building it.

---

## Team

**Fadipe Toluwanimi Alfred** - Solo Developer
Dev3Pack Global Hackathon 2026
Built in Nigeria 🇳🇬

---

## License

MIT License
