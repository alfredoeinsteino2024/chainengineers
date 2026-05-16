/*
 * server.js — ChainEngineers Payment Terminal Backend
 * Phase 3: Real Solana Devnet Integration
 * Phase 3.1: Added /history and /balance endpoints
 *
 * Endpoints:
 *   GET  /health              → health check
 *   POST /payment/create      → create payment session
 *   GET  /payment/:id/status  → poll payment status
 *   GET  /history             → last 10 confirmed transactions
 *   GET  /balance             → terminal wallet SOL balance
 */

const express    = require('express');
const crypto     = require('crypto');
const solanaWeb3 = require('@solana/web3.js');
const app        = express();
const PORT       = 3000;

app.use(express.json());

/* ─────────────────────────────────────────────
   SOLANA DEVNET CONFIG
───────────────────────────────────────────── */
const connection = new solanaWeb3.Connection(
    solanaWeb3.clusterApiUrl('devnet'),
    'confirmed'
);

const SOL_PER_NAIRA = 1 / 1_600_000;

/* ─────────────────────────────────────────────
   TERMINAL WALLET
───────────────────────────────────────────── */
const fs = require('fs');
let terminalKeypair;
if (fs.existsSync('terminal_keypair.json')) {
    const secret = Uint8Array.from(JSON.parse(fs.readFileSync('terminal_keypair.json')));
    terminalKeypair = solanaWeb3.Keypair.fromSecretKey(secret);
    console.log('Loaded existing terminal keypair');
} else {
    terminalKeypair = solanaWeb3.Keypair.generate();
    fs.writeFileSync('terminal_keypair.json', JSON.stringify(Array.from(terminalKeypair.secretKey)));
    console.log('Generated new terminal keypair — saved to terminal_keypair.json');
}
const terminalAddress = terminalKeypair.publicKey.toString();

/* ─────────────────────────────────────────────
   IN-MEMORY PAYMENT STORE
───────────────────────────────────────────── */
const payments = {};

/* ─────────────────────────────────────────────
   TRANSACTION HISTORY STORE
   Keeps last 10 confirmed transactions.
───────────────────────────────────────────── */
const txHistory = [];
const MAX_HISTORY = 10;

function addToHistory(payment_id, payment) {
    txHistory.unshift({
        payment_id   : payment_id,
        amount_naira : payment.amount_naira,
        amount_sol   : payment.amount_sol,
        tx_signature : payment.tx_signature,
        timestamp    : new Date().toLocaleTimeString(),
        status       : 'confirmed'
    });
    if (txHistory.length > MAX_HISTORY) txHistory.pop();
}

/* ─────────────────────────────────────────────
   HELPERS
───────────────────────────────────────────── */
function generatePaymentId() {
    return crypto.randomBytes(8).toString('hex').toUpperCase();
}

function nairaToSol(naira) {
    return (naira * SOL_PER_NAIRA).toFixed(6);
}

function log(msg) {
    console.log(`[${new Date().toLocaleTimeString()}] ${msg}`);
}

/* ─────────────────────────────────────────────
   SOLANA: poll Devnet for payment
───────────────────────────────────────────── */
async function monitorPayment(payment_id) {
    const payment = payments[payment_id];
    if (!payment) return;

    const sigsBefore = new Set();
    const existing = await connection.getSignaturesForAddress(
        new solanaWeb3.PublicKey(payment.receive_address),
        { limit: 10 }
    );
    existing.forEach(s => sigsBefore.add(s.signature));

    log(`[SOL] Monitoring ${payment.receive_address} for ${payment.amount_sol} SOL`);

    const MAX_POLLS  = 60;
    const POLL_DELAY = 5000;

    for (let i = 0; i < MAX_POLLS; i++) {
        await new Promise(r => setTimeout(r, POLL_DELAY));
        if (payment.status !== 'pending') break;

        try {
            const pubkey = new solanaWeb3.PublicKey(payment.receive_address);
            const sigs   = await connection.getSignaturesForAddress(pubkey, { limit: 10 });

            for (const sigInfo of sigs) {
                if (sigsBefore.has(sigInfo.signature)) continue;
                log(`[SOL] New TX detected: ${sigInfo.signature.slice(0, 16)}...`);
                payment.status       = 'confirmed';
                payment.tx_signature = sigInfo.signature;
                addToHistory(payment_id, payment);
                log(`[SOL] Payment CONFIRMED | ID=${payment_id}`);
                return;
            }
        } catch (err) {
            log(`[SOL] Monitor poll error: ${err.message}`);
        }
        log(`[SOL] Still waiting... poll ${i + 1}/${MAX_POLLS}`);
    }

    if (payment.status === 'pending') {
        payment.status = 'expired';
        log(`[SOL] Payment expired | ID=${payment_id}`);
    }
}

/* ─────────────────────────────────────────────
   GET /health
───────────────────────────────────────────── */
app.get('/health', async (req, res) => {
    let devnet = 'unknown';
    try {
        const slot = await connection.getSlot();
        devnet = `slot ${slot}`;
    } catch (e) { devnet = 'unreachable'; }
    log(`Health check | Devnet: ${devnet}`);
    res.json({ status: 'ok', service: 'ChainEngineers Backend', solana_devnet: devnet, terminal_address: terminalAddress });
});

/* ─────────────────────────────────────────────
   POST /payment/create
───────────────────────────────────────────── */
app.post('/payment/create', (req, res) => {
    const { amount } = req.body;
    if (!amount || isNaN(amount) || Number(amount) <= 0)
        return res.status(400).json({ status: 'error', error_msg: 'Invalid amount' });

    const naira      = Number(amount);
    const sol        = nairaToSol(naira);
    const payment_id = generatePaymentId();
    const receive_address = terminalAddress;
    const qr_data = `solana:${receive_address}?amount=${sol}&memo=${payment_id}`;

    payments[payment_id] = {
        amount_naira: naira, amount_sol: sol, qr_data,
        receive_address, created_at: Date.now(),
        status: 'pending', tx_signature: null
    };

    log(`Payment created | ID=${payment_id} | ₦${naira} → ${sol} SOL`);
    monitorPayment(payment_id).catch(err => log(`[SOL] Monitor error: ${err.message}`));

    res.json({ payment_id, qr_data, amount_sol: sol, receive_address, status: 'pending' });
});

/* ─────────────────────────────────────────────
   GET /payment/:id/status
───────────────────────────────────────────── */
app.get('/payment/:id/status', (req, res) => {
    const { id } = req.params;
    const payment = payments[id];
    if (!payment)
        return res.status(404).json({ status: 'error', error_msg: 'Payment not found' });

    const elapsed  = Date.now() - payment.created_at;
    const response = { payment_id: id, status: payment.status, amount_sol: payment.amount_sol };
    if (payment.status === 'confirmed') response.tx_signature = payment.tx_signature;

    log(`Poll | ID=${id} | status=${payment.status} | elapsed=${(elapsed/1000).toFixed(1)}s`);
    res.json(response);
});

/* ─────────────────────────────────────────────
   GET /history
   Returns last 10 confirmed transactions.
   Response: {
     count: N,
     transactions: [
       { payment_id, amount_naira, amount_sol,
         tx_signature, timestamp, status }
     ]
   }
───────────────────────────────────────────── */
app.get('/history', (req, res) => {
    log(`History request | ${txHistory.length} transactions`);
    res.json({
        count        : txHistory.length,
        transactions : txHistory
    });
});

/* ─────────────────────────────────────────────
   GET /balance
   Returns terminal wallet SOL balance from Devnet.
   Response: {
     address, balance_sol, balance_lamports
   }
───────────────────────────────────────────── */
app.get('/balance', async (req, res) => {
    try {
        const pubkey   = new solanaWeb3.PublicKey(terminalAddress);
        const lamports = await connection.getBalance(pubkey);
        const sol      = (lamports / solanaWeb3.LAMPORTS_PER_SOL).toFixed(6);
        log(`Balance request | ${sol} SOL | ${lamports} lamports`);
        res.json({ address: terminalAddress, balance_sol: sol, balance_lamports: lamports });
    } catch (err) {
        log(`Balance error: ${err.message}`);
        res.status(500).json({ status: 'error', error_msg: 'Failed to fetch balance' });
    }
});

/* ─────────────────────────────────────────────
   START
───────────────────────────────────────────── */
app.listen(PORT, async () => {
    console.log(`\n╔══════════════════════════════════════════╗`);
    console.log(`║   ChainEngineers Backend  v3.1 — Devnet  ║`);
    console.log(`║   http://localhost:${PORT}                ║`);
    console.log(`╚══════════════════════════════════════════╝\n`);
    console.log(`  Terminal address: ${terminalAddress}\n`);
    console.log(`  GET  /history`);
    console.log(`  GET  /balance\n`);
    try {
        const slot = await connection.getSlot();
        console.log(`  Solana Devnet: ONLINE (slot ${slot})\n`);
    } catch (e) {
        console.log(`  Solana Devnet: OFFLINE\n`);
    }
});

app.get('/wallet/balance', async (req, res) => {
    try {
        const pubkey  = new solanaWeb3.PublicKey(terminalAddress);
        const balance = await connection.getBalance(pubkey);
        const sol     = (balance / solanaWeb3.LAMPORTS_PER_SOL).toFixed(6);

        /* Get latest TX */
        const sigs = await connection.getSignaturesForAddress(pubkey, { limit: 1 });
        const lastTx = sigs.length > 0 ? sigs[0].signature : '';

        res.json({
            address     : terminalAddress,
            balance_sol : sol,
            incoming_sol: sol,
            last_tx     : lastTx
        });
    } catch (err) {
        res.status(500).json({ error: err.message });
    }
});
