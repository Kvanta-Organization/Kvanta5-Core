// Copyright (c) 2015-2026 The Bitcoin Core developers
// Copyright (c) 2026 The Kvanta5 Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// NOTE: This file is intended to be customised by the end user, and includes only local node policy logic

#include <policy/policy.h>

#include <coins.h>
#include <consensus/amount.h>
#include <consensus/consensus.h>
#include <consensus/validation.h>
#include <consensus/kvanta5p2qr.h>
#include <policy/feerate.h>
#include <primitives/transaction.h>
#include <script/interpreter.h>
#include <script/kvanta5_p2qr.h>
#include <script/script.h>
#include <script/solver.h>
#include <serialize.h>
#include <span.h>

#include <algorithm>
#include <cstddef>
#include <vector>

CAmount GetDustThreshold(const CTxOut& txout, const CFeeRate& dustRelayFeeIn)
{
    // "Dust" is defined in terms of dustRelayFee,
    // which has units quarks-per-kilobyte.
    // If you'd pay more in fees than the value of the output
    // to spend something, then we consider it dust.
    // A typical spendable non-segwit txout is 34 bytes big, and will
    // need a CTxIn of at least 148 bytes to spend:
    // so dust is a spendable txout less than
    // 182*dustRelayFee/1000 (in quarks).
    // 546 quarks at the default rate of 3000 quark/kvB.
    // A typical spendable segwit P2WPKH txout is 31 bytes big, and will
    // need a CTxIn of at least 67 bytes to spend:
    // so dust is a spendable txout less than
    // 98*dustRelayFee/1000 (in quarks).
    // 294 quarks at the default rate of 3000 quark/kvB.
    if (txout.scriptPubKey.IsUnspendable())
        return 0;

    size_t nSize = GetSerializeSize(txout);
    int witnessversion = 0;
    std::vector<unsigned char> witnessprogram;

    // Note this computation is for spending a Segwit v0 P2WPKH output (a 33 bytes
    // public key + an ECDSA signature). For Segwit v1 Taproot outputs the minimum
    // satisfaction is lower (a single BIP340 signature) but this computation was
    // kept to not further reduce the dust level.
    // See discussion in https://github.com/kvanta5/kvanta5/pull/22779 for details.
    if (txout.scriptPubKey.IsWitnessProgram(witnessversion, witnessprogram)) {
        // sum the sizes of the parts of a transaction input
        // with 75% segwit discount applied to the script size.
        nSize += (32 + 4 + 1 + (107 / WITNESS_SCALE_FACTOR) + 4);
    } else {
        nSize += (32 + 4 + 1 + 107 + 4); // the 148 mentioned above
    }

    return dustRelayFeeIn.GetFee(nSize);
}

bool IsDust(const CTxOut& txout, const CFeeRate& dustRelayFeeIn)
{
    return (txout.nValue < GetDustThreshold(txout, dustRelayFeeIn));
}

std::vector<uint32_t> GetDust(const CTransaction& tx, CFeeRate dust_relay_rate)
{
    std::vector<uint32_t> dust_outputs;
    for (uint32_t i{0}; i < tx.vout.size(); ++i) {
        if (IsDust(tx.vout[i], dust_relay_rate)) dust_outputs.push_back(i);
    }
    return dust_outputs;
}

bool IsStandard(const CScript& scriptPubKey, const std::optional<unsigned>& max_datacarrier_bytes, TxoutType& whichType)
{
    std::vector<std::vector<unsigned char> > vSolutions;
    whichType = Solver(scriptPubKey, vSolutions);

    if (whichType == TxoutType::NONSTANDARD) {
        return false;
    } else if (whichType == TxoutType::MULTISIG) {
        unsigned char m = vSolutions.front()[0];
        unsigned char n = vSolutions.back()[0];
        // Support up to x-of-3 multisig txns as standard
        if (n < 1 || n > 3)
            return false;
        if (m < 1 || m > n)
            return false;
    } else if (whichType == TxoutType::NULL_DATA) {
        if (!max_datacarrier_bytes || scriptPubKey.size() > *max_datacarrier_bytes) {
            return false;
        }
    }

    return true;
}

bool IsStandardTx(const CTransaction& tx, const std::optional<unsigned>& max_datacarrier_bytes, bool permit_bare_multisig, const CFeeRate& dust_relay_fee, std::string& reason)
{
    if (tx.version > TX_MAX_STANDARD_VERSION || tx.version < 1) {
        reason = "version";
        return false;
    }

    /*
     * Normal transactions remain capped by MAX_STANDARD_TX_WEIGHT.
     *
     * Kvanta5 P2QR / P2SH-wrapped-P2QR consolidation, migration, and
     * institutional fanout transactions may be much larger because ML-DSA-87
     * authorization data is large by design.
     *
     * IsStandardTx() does not have prevout context, so it cannot prove whether
     * an oversized transaction actually spends Kvanta5 P2QR inputs. Therefore:
     *
     *   - anything above MAX_STANDARD_P2QR_TX_WEIGHT is rejected immediately
     *   - anything above MAX_STANDARD_TX_WEIGHT but below/equal to the P2QR
     *     ceiling is deferred to the prevout-aware policy path
     *
     * The prevout-aware policy path must later prove that the transaction spends
     * native P2QR or P2SH-wrapped P2QR inputs before accepting it as standard.
     */
    const unsigned int sz = GetTransactionWeight(tx);

    if (sz > MAX_STANDARD_P2QR_TX_WEIGHT) {
        reason = "tx-size";
        return false;
    }

    const bool oversized_p2qr_candidate = sz > MAX_STANDARD_TX_WEIGHT;

    for (const CTxIn& txin : tx.vin)
    {
        /*
         * Legacy Bitcoin policy assumes scriptSig is small. That is not true
         * for P2SH-wrapped Kvanta5 P2QR, where the ML-DSA-87 signature,
         * signing pubkey/policy pushes, and redeemScript live in scriptSig.
         *
         * IsStandardTx() cannot tell whether an oversized scriptSig belongs to
         * wrapped P2QR because it has no prevout context. If the whole tx is
         * within the P2QR oversized transaction ceiling, defer this decision to
         * AreInputsStandard()/prevout-aware validation.
         */
        if (txin.scriptSig.size() > MAX_STANDARD_SCRIPTSIG_SIZE) {
            reason = "p2qr-scriptsig-size-deferred";
            return false;
        }

        /*
         * Even deferred P2QR scriptSigs must be push-only. Native and wrapped
         * P2QR spends are encoded as pushed data. Non-push scriptSig data is
         * still nonstandard.
         */
        if (!txin.scriptSig.IsPushOnly()) {
            reason = "scriptsig-not-pushonly";
            return false;
        }
    }

    unsigned int nDataOut = 0;
    TxoutType whichType;

    for (const CTxOut& txout : tx.vout) {
        if (!::IsStandard(txout.scriptPubKey, max_datacarrier_bytes, whichType)) {
            reason = "scriptpubkey";
            return false;
        }

        if (whichType == TxoutType::NULL_DATA) {
            ++nDataOut;
        } else if ((whichType == TxoutType::MULTISIG) && (!permit_bare_multisig)) {
            reason = "bare-multisig";
            return false;
        }
    }

    if (GetDust(tx, dust_relay_fee).size() > MAX_DUST_OUTPUTS_PER_TX) {
        reason = "dust";
        return false;
    }

    if (nDataOut > 1) {
        reason = "multi-op-return";
        return false;
    }

    if (oversized_p2qr_candidate) {
        reason = "p2qr-tx-size-deferred";
        return false;
    }

    return true;
}

/**
 * Check transaction inputs to mitigate two
 * potential denial-of-service attacks:
 *
 * 1. scriptSigs with extra data stuffed into them,
 *    not consumed by scriptPubKey (or P2SH script)
 * 2. P2SH scripts with a crazy number of expensive
 *    CHECKSIG/CHECKMULTISIG operations
 *
 * Why bother? To avoid denial-of-service attacks; an attacker
 * can submit a standard HASH... OP_EQUAL transaction,
 * which will get accepted into blocks. The redemption
 * script can be anything; an attacker could use a very
 * expensive-to-check-upon-redemption script like:
 *   DUP CHECKSIG DROP ... repeated 100 times... OP_1
 *
 * Note that only the non-witness portion of the transaction is checked here.
 */

bool AreInputsStandard(const CTransaction& tx, const CCoinsViewCache& mapInputs)
{
    if (tx.IsCoinBase()) {
        return true; // Coinbases don't use vin normally
    }

    for (unsigned int i = 0; i < tx.vin.size(); i++) {
        const CTxIn& txin = tx.vin[i];
        const CTxOut& prev = mapInputs.AccessCoin(txin.prevout).out;

        std::vector<std::vector<unsigned char>> vSolutions;
        TxoutType whichType = Solver(prev.scriptPubKey, vSolutions);

        if (whichType == TxoutType::NONSTANDARD || whichType == TxoutType::WITNESS_UNKNOWN) {
            // WITNESS_UNKNOWN failures are typically also caught with a policy
            // flag in the script interpreter, but it can be helpful to catch
            // this type of NONSTANDARD transaction earlier in transaction
            // validation.
            return false;
        }

        // Native KVANTA5_P2QR:
        //
        // Single-key:
        //   prevout scriptPubKey: 50 20 <program>
        //   scriptSig:           <ML-DSA-87 sig> <ML-DSA-87 pubkey>
        //
        // Multisig:
        //   prevout scriptPubKey: 50 20 <program>
        //   scriptSig:           <sig_0> ... <sig_m-1>
        //                        <idx_0> ... <idx_m-1>
        //                        <pubkey_0> ... <pubkey_n-1>
        //                        <m> <n>
        //
        // This is not descriptor/script-interpreter standardness. It has its
        // own native wallet/consensus validation path, so only verify the
        // standard envelope shape here.
        uint256 native_p2qr_program;
        if (consensus::kvanta5p2qr::IsOutput(prev.scriptPubKey, native_p2qr_program)) {
            std::vector<std::vector<unsigned char>> pushes;
            CScript::const_iterator pc = txin.scriptSig.begin();

            while (pc < txin.scriptSig.end()) {
                opcodetype opcode;
                std::vector<unsigned char> data;

                if (!txin.scriptSig.GetOp(pc, opcode, data)) {
                    return false;
                }

                if (opcode > OP_16) {
                    return false;
                }

                pushes.push_back(std::move(data));
            }

            if (pushes.size() == 2) {
                CScript native_spend_script;
                native_spend_script << pushes[0] << pushes[1];

                std::vector<unsigned char> sig;
                std::vector<unsigned char> pubkey;
                std::string p2qr_error;

                if (!consensus::kvanta5p2qr::ParseSpendData(native_spend_script, sig, pubkey, p2qr_error)) {
                    return false;
                }

                continue;
            }

            Kvanta5P2QRMultisigSpend multisig_spend;
            std::string p2qr_error;
            if (!Kvanta5ParseP2QRMultisigSpend(pushes, multisig_spend, &p2qr_error)) {
                return false;
            }

            continue;
        }

        if (whichType == TxoutType::SCRIPTHASH) {
            // P2SH-carried KVANTA5_P2QR:
            //
            // Single-key:
            //   prevout scriptPubKey: a9 14 HASH160(50 20 <program>) 87
            //   scriptSig:           <ML-DSA-87 sig> <ML-DSA-87 pubkey> <50 20 <program>>
            //
            // Multisig:
            //   prevout scriptPubKey: a9 14 HASH160(50 20 <program>) 87
            //   scriptSig:           <sig_0> ... <sig_m-1>
            //                        <idx_0> ... <idx_m-1>
            //                        <pubkey_0> ... <pubkey_n-1>
            //                        <m> <n>
            //                        <50 20 <program>>
            //
            // Do NOT use EvalScript() for this path. The ML-DSA pushes are far
            // larger than legacy script element assumptions. Parse push-only
            // data manually, prove the redeemScript is the committed native
            // P2QR marker, and then verify the native P2QR spend envelope shape.
            CScriptID p2sh_script_id;
            if (consensus::kvanta5p2qr::IsP2SHOutput(prev.scriptPubKey, p2sh_script_id)) {
                std::vector<std::vector<unsigned char>> pushes;
                CScript::const_iterator pc = txin.scriptSig.begin();

                bool push_parse_ok = true;
                while (pc < txin.scriptSig.end()) {
                    opcodetype opcode;
                    std::vector<unsigned char> data;

                    if (!txin.scriptSig.GetOp(pc, opcode, data)) {
                        push_parse_ok = false;
                        break;
                    }

                    if (opcode > OP_16) {
                        push_parse_ok = false;
                        break;
                    }

                    pushes.push_back(std::move(data));
                }

                if (push_parse_ok && pushes.size() >= 3) {
                    const std::vector<unsigned char> redeem_script_bytes = pushes.back();
                    pushes.pop_back();

                    const CScript redeem_script(redeem_script_bytes.begin(), redeem_script_bytes.end());

                    uint256 carried_p2qr_program;
                    if (consensus::kvanta5p2qr::IsOutput(redeem_script, carried_p2qr_program)) {
                        if (CScriptID(redeem_script) != p2sh_script_id) {
                            return false;
                        }

                        if (pushes.size() == 2) {
                            CScript native_spend_script;
                            native_spend_script << pushes[0] << pushes[1];

                            std::vector<unsigned char> sig;
                            std::vector<unsigned char> pubkey;
                            std::string p2qr_error;

                            if (!consensus::kvanta5p2qr::ParseSpendData(native_spend_script, sig, pubkey, p2qr_error)) {
                                return false;
                            }

                            continue;
                        }

                        Kvanta5P2QRMultisigSpend multisig_spend;
                        std::string p2qr_error;
                        if (!Kvanta5ParseP2QRMultisigSpend(pushes, multisig_spend, &p2qr_error)) {
                            return false;
                        }

                        continue;
                    }
                }
            }

            // Normal legacy P2SH path. Keep existing behavior for non-P2QR P2SH.
            std::vector<std::vector<unsigned char>> stack;

            // Convert the scriptSig into a stack, so we can inspect the redeemScript.
            if (!EvalScript(stack, txin.scriptSig, SCRIPT_VERIFY_NONE, BaseSignatureChecker(), SigVersion::BASE)) {
                return false;
            }

            if (stack.empty()) {
                return false;
            }

            CScript subscript(stack.back().begin(), stack.back().end());
            if (subscript.GetSigOpCount(true) > MAX_P2SH_SIGOPS) {
                return false;
            }
        }
    }

    return true;
}


bool IsWitnessStandard(const CTransaction& tx, const CCoinsViewCache& mapInputs)
{
    if (tx.IsCoinBase())
        return true; // Coinbases are skipped

    for (unsigned int i = 0; i < tx.vin.size(); i++)
    {
        // We don't care if witness for this input is empty, since it must not be bloated.
        // If the script is invalid without witness, it would be caught sooner or later during validation.
        if (tx.vin[i].scriptWitness.IsNull())
            continue;

        const CTxOut &prev = mapInputs.AccessCoin(tx.vin[i].prevout).out;

        // get the scriptPubKey corresponding to this input:
        CScript prevScript = prev.scriptPubKey;

        // witness stuffing detected
        if (prevScript.IsPayToAnchor()) {
            return false;
        }

        bool p2sh = false;
        if (prevScript.IsPayToScriptHash()) {
            std::vector <std::vector<unsigned char> > stack;
            // If the scriptPubKey is P2SH, we try to extract the redeemScript casually by converting the scriptSig
            // into a stack. We do not check IsPushOnly nor compare the hash as these will be done later anyway.
            // If the check fails at this stage, we know that this txid must be a bad one.
            if (!EvalScript(stack, tx.vin[i].scriptSig, SCRIPT_VERIFY_NONE, BaseSignatureChecker(), SigVersion::BASE))
                return false;
            if (stack.empty())
                return false;
            prevScript = CScript(stack.back().begin(), stack.back().end());
            p2sh = true;
        }

        int witnessversion = 0;
        std::vector<unsigned char> witnessprogram;

        // Non-witness program must not be associated with any witness
        if (!prevScript.IsWitnessProgram(witnessversion, witnessprogram))
            return false;

        // Check P2WSH standard limits
        if (witnessversion == 0 && witnessprogram.size() == WITNESS_V0_SCRIPTHASH_SIZE) {
            if (tx.vin[i].scriptWitness.stack.back().size() > MAX_STANDARD_P2WSH_SCRIPT_SIZE)
                return false;
            size_t sizeWitnessStack = tx.vin[i].scriptWitness.stack.size() - 1;
            if (sizeWitnessStack > MAX_STANDARD_P2WSH_STACK_ITEMS)
                return false;
            for (unsigned int j = 0; j < sizeWitnessStack; j++) {
                if (tx.vin[i].scriptWitness.stack[j].size() > MAX_STANDARD_P2WSH_STACK_ITEM_SIZE)
                    return false;
            }
        }

        // Check policy limits for Taproot spends:
        // - MAX_STANDARD_TAPSCRIPT_STACK_ITEM_SIZE limit for stack item size
        // - No annexes
        if (witnessversion == 1 && witnessprogram.size() == WITNESS_V1_TAPROOT_SIZE && !p2sh) {
            // Taproot spend (non-P2SH-wrapped, version 1, witness program size 32; see BIP 341)
            Span stack{tx.vin[i].scriptWitness.stack};
            if (stack.size() >= 2 && !stack.back().empty() && stack.back()[0] == ANNEX_TAG) {
                // Annexes are nonstandard as long as no semantics are defined for them.
                return false;
            }
            if (stack.size() >= 2) {
                // Script path spend (2 or more stack elements after removing optional annex)
                const auto& control_block = SpanPopBack(stack);
                SpanPopBack(stack); // Ignore script
                if (control_block.empty()) return false; // Empty control block is invalid
                if ((control_block[0] & TAPROOT_LEAF_MASK) == TAPROOT_LEAF_TAPSCRIPT) {
                    // Leaf version 0xc0 (aka Tapscript, see BIP 342)
                    for (const auto& item : stack) {
                        if (item.size() > MAX_STANDARD_TAPSCRIPT_STACK_ITEM_SIZE) return false;
                    }
                }
            } else if (stack.size() == 1) {
                // Key path spend (1 stack element after removing optional annex)
                // (no policy rules apply)
            } else {
                // 0 stack elements; this is already invalid by consensus rules
                return false;
            }
        }
    }
    return true;
}

int64_t GetVirtualTransactionSize(int64_t nWeight, int64_t nSigOpCost, unsigned int bytes_per_sigop)
{
    return (std::max(nWeight, nSigOpCost * bytes_per_sigop) + WITNESS_SCALE_FACTOR - 1) / WITNESS_SCALE_FACTOR;
}

int64_t GetVirtualTransactionSize(const CTransaction& tx, int64_t nSigOpCost, unsigned int bytes_per_sigop)
{
    return GetVirtualTransactionSize(GetTransactionWeight(tx), nSigOpCost, bytes_per_sigop);
}

int64_t GetVirtualTransactionInputSize(const CTxIn& txin, int64_t nSigOpCost, unsigned int bytes_per_sigop)
{
    return GetVirtualTransactionSize(GetTransactionInputWeight(txin), nSigOpCost, bytes_per_sigop);
}
int64_t GetP2QRFeeVirtualSize(const CTransaction& tx, const CCoinsViewCache& mapInputs, int64_t actual_vsize)
{
    if (actual_vsize <= 0 || tx.IsCoinBase()) {
        return actual_vsize;
    }

    int64_t p2qr_scriptsig_bytes{0};
    int64_t p2qr_inputs{0};

    for (const CTxIn& txin : tx.vin) {
        const Coin& coin = mapInputs.AccessCoin(txin.prevout);
        if (coin.IsSpent()) {
            continue;
        }

        uint256 ignored_program;
        CScriptID ignored_script_id;

        const bool is_p2qr_spend =
            consensus::kvanta5p2qr::IsOutput(coin.out.scriptPubKey, ignored_program) ||
            consensus::kvanta5p2qr::IsP2SHOutput(coin.out.scriptPubKey, ignored_script_id);

        if (!is_p2qr_spend) {
            continue;
        }

        p2qr_scriptsig_bytes += static_cast<int64_t>(txin.scriptSig.size());
        ++p2qr_inputs;
    }

    if (p2qr_inputs == 0 || p2qr_scriptsig_bytes <= 0) {
        return actual_vsize;
    }

    const int64_t non_p2qr_part = std::max<int64_t>(1, actual_vsize - p2qr_scriptsig_bytes);
    const int64_t discounted_p2qr_part = p2qr_inputs * P2QR_POLICY_FEE_VBYTES_PER_INPUT;

    return std::max<int64_t>(1, non_p2qr_part + discounted_p2qr_part);
}

int64_t GetP2QRAuthBytes(const CTransaction& tx, const CCoinsViewCache& mapInputs)
{
    if (tx.IsCoinBase()) {
        return 0;
    }

    int64_t p2qr_auth_bytes{0};

    for (const CTxIn& txin : tx.vin) {
        const Coin& coin = mapInputs.AccessCoin(txin.prevout);
        if (coin.IsSpent()) {
            continue;
        }

        uint256 p2qr_program;
        if (consensus::kvanta5p2qr::IsOutput(coin.out.scriptPubKey, p2qr_program)) {
            std::vector<unsigned char> sig;
            std::vector<unsigned char> pubkey;
            std::string err;

            if (consensus::kvanta5p2qr::ParseSpendData(txin.scriptSig, sig, pubkey, err)) {
                p2qr_auth_bytes += static_cast<int64_t>(txin.scriptSig.size());
            }

            continue;
        }

        CScriptID p2sh_script_id;
        if (consensus::kvanta5p2qr::IsP2SHOutput(coin.out.scriptPubKey, p2sh_script_id)) {
            std::vector<std::vector<unsigned char>> pushes;
            CScript::const_iterator pc = txin.scriptSig.begin();

            bool push_parse_ok = true;
            while (pc < txin.scriptSig.end()) {
                opcodetype opcode;
                std::vector<unsigned char> data;

                if (!txin.scriptSig.GetOp(pc, opcode, data)) {
                    push_parse_ok = false;
                    break;
                }

                if (opcode > OP_16) {
                    push_parse_ok = false;
                    break;
                }

                pushes.push_back(std::move(data));
            }

            if (!push_parse_ok || pushes.size() != 3) {
                continue;
            }

            const CScript redeem_script(pushes.back().begin(), pushes.back().end());

            uint256 carried_program;
            if (!consensus::kvanta5p2qr::IsOutput(redeem_script, carried_program)) {
                continue;
            }

            if (CScriptID(redeem_script) != p2sh_script_id) {
                continue;
            }

            CScript native_spend_script;
            native_spend_script << pushes[0] << pushes[1];

            std::vector<unsigned char> sig;
            std::vector<unsigned char> pubkey;
            std::string err;

            if (consensus::kvanta5p2qr::ParseSpendData(native_spend_script, sig, pubkey, err)) {
                // Discount only sig+pubkey auth payload, not the P2SH redeemScript.
                p2qr_auth_bytes += static_cast<int64_t>(native_spend_script.size());
            }
        }
    }

    return p2qr_auth_bytes;
}

int64_t GetP2QRAdjustedTransactionWeight(const CTransaction& tx, const CCoinsViewCache& mapInputs)
{
    const int64_t raw_weight = GetTransactionWeight(tx);
    const int64_t p2qr_auth_bytes = GetP2QRAuthBytes(tx, mapInputs);

    if (p2qr_auth_bytes <= 0) {
        return raw_weight;
    }

    // P2QR auth is currently scriptSig/base data, already counted 4x.
    // Adjust it to 1x by subtracting 3 WU per auth byte.
    const int64_t adjusted_weight = raw_weight - (p2qr_auth_bytes * (WITNESS_SCALE_FACTOR - 1));

    return std::max<int64_t>(adjusted_weight, 1);
}

int64_t GetP2QRAdjustedVirtualTransactionSize(const CTransaction& tx, const CCoinsViewCache& mapInputs)
{
    return (GetP2QRAdjustedTransactionWeight(tx, mapInputs) + WITNESS_SCALE_FACTOR - 1) / WITNESS_SCALE_FACTOR;
}
