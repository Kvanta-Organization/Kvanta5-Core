// Copyright (c) 2015-2026 The Bitcoin Core developers
// Copyright (c) 2026 The Kvanta5 Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <consensus/tx_check.h>

#include <consensus/amount.h>
#include <consensus/validation.h>
#include <primitives/transaction.h>
#include <script/script.h>

#include <set>

static bool IsKvanta5AllowedOutputScript(const CScript& script_pub_key)
{
    /*
     * Kvanta5 strict output enforcement.
     *
     * Allowed output forms:
     *
     * 1. Native P2QR:
     *      OP_RESERVED <32-byte P2QR program>
     *      Hex: 50 20 <32 bytes>
     *
     * 2. Wrapped P2SH compatibility:
     *      OP_HASH160 <20-byte script hash> OP_EQUAL
     *      Hex: a9 14 <20 bytes> 87
     *
     * Everything else is rejected:
     *   - OP_RETURN / nulldata
     *   - legacy P2PKH
     *   - SegWit v0 P2WPKH/P2WSH
     *   - Taproot v1
     *   - future witness versions
     *   - bare inscription-style scripts
     *   - arbitrary nonstandard scripts
     */

    if (script_pub_key.size() == 34 &&
        script_pub_key[0] == OP_RESERVED &&
        script_pub_key[1] == 0x20) {
        return true;
    }

    if (script_pub_key.size() == 23 &&
        script_pub_key[0] == OP_HASH160 &&
        script_pub_key[1] == 0x14 &&
        script_pub_key[22] == OP_EQUAL) {
        return true;
    }

    return false;
}

bool CheckTransaction(const CTransaction& tx, TxValidationState& state)
{
    // Basic checks that don't depend on any context
    if (tx.vin.empty()) {
        return state.Invalid(TxValidationResult::TX_CONSENSUS, "bad-txns-vin-empty");
    }

    if (tx.vout.empty()) {
        return state.Invalid(TxValidationResult::TX_CONSENSUS, "bad-txns-vout-empty");
    }

    // Size limits (this doesn't take the witness into account, as that hasn't been checked for malleability)
    if (::GetSerializeSize(TX_NO_WITNESS(tx)) * WITNESS_SCALE_FACTOR > MAX_BLOCK_WEIGHT) {
        return state.Invalid(TxValidationResult::TX_CONSENSUS, "bad-txns-oversize");
    }

    // Check for negative or overflow output values (see CVE-2010-5139)
    CAmount nValueOut = 0;
    for (const auto& txout : tx.vout) {
        if (txout.nValue < 0) {
            return state.Invalid(TxValidationResult::TX_CONSENSUS, "bad-txns-vout-negative");
        }

        if (txout.nValue > MAX_MONEY) {
            return state.Invalid(TxValidationResult::TX_CONSENSUS, "bad-txns-vout-toolarge");
        }

        /*
         * Kvanta5 strict output rule:
         *
         * Transactions may only create:
         *   - native P2QR outputs, or
         *   - wrapped P2SH compatibility outputs.
         *
         * This is intentionally consensus-level and context-free.
         */
        if (!IsKvanta5AllowedOutputScript(txout.scriptPubKey)) {
            return state.Invalid(
                TxValidationResult::TX_CONSENSUS,
                "bad-txns-forbidden-output",
                "Kvanta5 transactions may only create native P2QR or wrapped P2SH-compatible outputs"
            );
        }

        nValueOut += txout.nValue;
        if (!MoneyRange(nValueOut)) {
            return state.Invalid(TxValidationResult::TX_CONSENSUS, "bad-txns-txouttotal-toolarge");
        }
    }

    // Check for duplicate inputs (see CVE-2018-17144)
    // While Consensus::CheckTxInputs does check if all inputs of a tx are available, and UpdateCoins marks all inputs
    // of a tx as spent, it does not check if the tx has duplicate inputs.
    // Failure to run this check will result in either a crash or an inflation bug, depending on the implementation of
    // the underlying coins database.
    std::set<COutPoint> vInOutPoints;
    for (const auto& txin : tx.vin) {
        if (!vInOutPoints.insert(txin.prevout).second) {
            return state.Invalid(TxValidationResult::TX_CONSENSUS, "bad-txns-inputs-duplicate");
        }
    }

    if (tx.IsCoinBase()) {
        if (tx.vin[0].scriptSig.size() < 2 || tx.vin[0].scriptSig.size() > 100) {
            return state.Invalid(TxValidationResult::TX_CONSENSUS, "bad-cb-length");
        }
    } else {
        for (const auto& txin : tx.vin) {
            if (txin.prevout.IsNull()) {
                return state.Invalid(TxValidationResult::TX_CONSENSUS, "bad-txns-prevout-null");
            }
        }
    }

    return true;
}
