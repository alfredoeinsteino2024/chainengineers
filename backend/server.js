/*
 * server.js — ChainEngineers Payment Terminal Backend
 * Phase 3: Real Solana Devnet Integration
 *
 * Flow:
 *   1. POST /payment/create  → generates a Devnet receiving address + amount
 *   2. GET  /payment/:id/status → monitors Devnet for incoming transaction
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
   One keypair per terminal — generated fresh on
   startup. In production this would be loaded
   from secure storage on the ESP32.
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
   SOLANA: get recent transactions for address
   Returns the latest signature or null.
───────────────────────────────────────────── */
async function getLatestSignature(address) {
    try {
        const pubkey = new solanaWeb3.PublicKey(address);
        const sigs   = await connection.getSignaturesForAddress(pubkey, { limit: 5 });
        if (sigs.length === 0) return null;
        return sigs[0].signature;
    } catch (err) {
        log(`[SOL] getLatestSignature error: ${err.message}`);
        return null;
    }
}

/* ─────────────────────────────────────────────
   SOLANA: verify a transaction actually sent
   the right amount to our terminal address.
   Returns true if valid, false otherwise.
───────────────────────────────────────────── */
async function verifyTransaction(signature, expectedLamports, recipientAddress) {
    try {
        const tx = await connection.getTransaction(signature, {
            commitment             : 'confirmed',
            maxSupportedTransactionVersion: 0
        });

        if (!tx) return false;

        /* Check transaction succeeded */
        if (tx.meta.err) return false;

        /* Check recipient received expected lamports */
        const accountKeys = tx.transaction.message.accountKeys;
        const postBalances = tx.meta.postBalances;
        const preBalances  = tx.meta.preBalances;

        for (let i = 0; i < accountKeys.length; i++) {
            if (accountKeys[i].toString() === recipientAddress) {
                const received = postBalances[i] - preBalances[i];
                log(`[SOL] Received ${received} lamports, expected ${expectedLamports}`);
                /* Allow 1% tolerance for fees */
                if (received >= expectedLamports * 0.99) return true;
            }
        }
        return false;
    } catch (err) {
        log(`[SOL] verifyTransaction error: ${err.message}`);
        return false;
    }
}

/* ─────────────────────────────────────────────
   SOLANA: poll Devnet for payment
   Runs in background after payment is created.
   Updates payment.status when confirmed.
───────────────────────────────────────────── */
async function monitorPayment(payment_id) {
    const payment = payments[payment_id];
    if (!payment) return;

    const expectedLamports = Math.floor(
        parseFloat(payment.amount_sol) * solanaWeb3.LAMPORTS_PER_SOL
    );

    /* Record signatures already on the address before payment */
    const sigsBefore = new Set();
    const existing = await connection.getSignaturesForAddress(
        new solanaWeb3.PublicKey(payment.receive_address),
        { limit: 10 }
    );
    existing.forEach(s => sigsBefore.add(s.signature));

    log(`[SOL] Monitoring ${payment.receive_address} for ${payment.amount_sol} SOL`);

    const MAX_POLLS  = 60;   /* 60 * 5s = 5 minutes */
    const POLL_DELAY = 5000; /* 5 seconds */

    for (let i = 0; i < MAX_POLLS; i++) {
        await new Promise(r => setTimeout(r, POLL_DELAY));

        if (payment.status !== 'pending') break;

        try {
            const pubkey = new solanaWeb3.PublicKey(payment.receive_address);
            const sigs   = await connection.getSignaturesForAddress(
                pubkey, { limit: 10 }
            );

            /* Find new signatures since we started */
            for (const sigInfo of sigs) {
                if (sigsBefore.has(sigInfo.signature)) continue;

                log(`[SOL] New TX detected: ${sigInfo.signature.slice(0, 16)}...`);

                /* Accept any new incoming transaction to this address */
                payment.status       = 'confirmed';
                payment.tx_signature = sigInfo.signature;
                log(`[SOL] Payment CONFIRMED | ID=${payment_id} | TX=${sigInfo.signature.slice(0, 16)}...`);
                return;
            }
        } catch (err) {
            log(`[SOL] Monitor poll error: ${err.message}`);
        }

        log(`[SOL] Still waiting... poll ${i + 1}/${MAX_POLLS}`);
    }

    /* Timeout — expire the payment */
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
    } catch (e) {
        devnet = 'unreachable';
    }

    log(`Health check | Devnet: ${devnet}`);
    res.json({
        status          : 'ok',
        service         : 'ChainEngineers Backend',
        solana_devnet   : devnet,
        terminal_address: terminalAddress
    });
});

/* ─────────────────────────────────────────────
   POST /payment/create
───────────────────────────────────────────── */
app.post('/payment/create', (req, res) => {
    const { amount } = req.body;

    if (!amount || isNaN(amount) || Number(amount) <= 0) {
        return res.status(400).json({ status: 'error', error_msg: 'Invalid amount' });
    }

    const naira      = Number(amount);
    const sol        = nairaToSol(naira);
    const payment_id = generatePaymentId();

    /* Each payment uses the terminal's main address.
       In production, derive a unique address per payment. */
    const receive_address = terminalAddress;
    const qr_data = `solana:${receive_address}?amount=${sol}&memo=${payment_id}`;

    payments[payment_id] = {
        amount_naira    : naira,
        amount_sol      : sol,
        qr_data         : qr_data,
        receive_address : receive_address,
        created_at      : Date.now(),
        status          : 'pending',
        tx_signature    : null
    };

    log(`Payment created | ID=${payment_id} | ₦${naira} → ${sol} SOL | addr=${receive_address.slice(0,8)}...`);

    /* Start background Devnet monitor */
    monitorPayment(payment_id).catch(err =>
        log(`[SOL] Monitor error: ${err.message}`)
    );

    res.json({
        payment_id      : payment_id,
        qr_data         : qr_data,
        amount_sol      : sol,
        receive_address : receive_address,
        status          : 'pending'
    });
});

/* ─────────────────────────────────────────────
   GET /payment/:id/status
───────────────────────────────────────────── */
app.get('/payment/:id/status', (req, res) => {
    const { id } = req.params;
    const payment = payments[id];

    if (!payment) {
        return res.status(404).json({ status: 'error', error_msg: 'Payment not found' });
    }

    const elapsed  = Date.now() - payment.created_at;
    const response = {
        payment_id : id,
        status     : payment.status,
        amount_sol : payment.amount_sol
    };

    if (payment.status === 'confirmed') {
        response.tx_signature = payment.tx_signature;
    }

    log(`Poll | ID=${id} | status=${payment.status} | elapsed=${(elapsed/1000).toFixed(1)}s`);
    res.json(response);
});

/* ─────────────────────────────────────────────
   START
───────────────────────────────────────────── */
app.listen(PORT, async () => {
    console.log(`\n╔══════════════════════════════════════════╗`);
    console.log(`║   ChainEngineers Backend  v3.0 — Devnet  ║`);
    console.log(`║   http://localhost:${PORT}                ║`);
    console.log(`╚══════════════════════════════════════════╝\n`);
    console.log(`  Terminal address: ${terminalAddress}\n`);

    /* Check Devnet connectivity */
    try {
        const slot = await connection.getSlot();
        console.log(`  Solana Devnet: ONLINE (slot ${slot})\n`);
    } catch (e) {
        console.log(`  Solana Devnet: OFFLINE — check internet connection\n`);
    }
});
