// Copyright (c) 2015-2026 The Bitcoin Core developers
// Copyright (c) 2026 The Kvanta5 Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <algorithm>
#include <common/args.h>
#include <common/messages.h>
#include <common/system.h>
#include <consensus/amount.h>
#include <consensus/validation.h>
#include <consensus/kvanta5p2qr.h>
#include <interfaces/chain.h>
#include <node/types.h>
#include <numeric>
#include <policy/policy.h>
#include <primitives/transaction.h>
#include <script/script.h>
#include <script/signingprovider.h>
#include <script/solver.h>
#include <util/check.h>
#include <util/moneystr.h>
#include <util/rbf.h>
#include <util/trace.h>
#include <util/translation.h>
#include <wallet/coincontrol.h>
#include <wallet/fees.h>
#include <wallet/receive.h>
#include <wallet/spend.h>
#include <wallet/transaction.h>
#include <wallet/wallet.h>

#include <arith_uint256.h>
#include <limits>
#include <cmath>

using common::StringForFeeReason;
using common::TransactionErrorString;
using interfaces::FoundBlock;
using node::TransactionError;

TRACEPOINT_SEMAPHORE(coin_selection, selected_coins);
TRACEPOINT_SEMAPHORE(coin_selection, normal_create_tx_internal);
TRACEPOINT_SEMAPHORE(coin_selection, attempting_aps_create_tx);
TRACEPOINT_SEMAPHORE(coin_selection, aps_create_tx_internal);

namespace wallet {
static constexpr size_t OUTPUT_GROUP_MAX_ENTRIES{100};

/** Whether the descriptor represents, directly or not, a witness program. */
static bool IsSegwit(const Descriptor& desc) {
    if (const auto typ = desc.GetOutputType()) return *typ != OutputType::LEGACY;
    return false;
}

/** Whether to assume ECDSA signatures' will be high-r. */
static bool UseMaxSig(const std::optional<CTxIn>& txin, const CCoinControl* coin_control) {
    // Use max sig if watch only inputs were used or if this particular input is an external input
    // to ensure a sufficient fee is attained for the requested feerate.
    return coin_control && (coin_control->fAllowWatchOnly || (txin && coin_control->IsExternalSelected(txin->prevout)));
}

/** Get the size of an input (in witness units) once it's signed.
 *
 * @param desc The output script descriptor of the coin spent by this input.
 * @param txin Optionally the txin to estimate the size of. Used to determine the size of ECDSA signatures.
 * @param coin_control Information about the context to determine the size of ECDSA signatures.
 * @param tx_is_segwit Whether the transaction has at least a single input spending a segwit coin.
 * @param can_grind_r Whether the signer will be able to grind the R of the signature.
 */
static std::optional<int64_t> MaxInputWeight(const Descriptor& desc, const std::optional<CTxIn>& txin,
                                             const CCoinControl* coin_control, const bool tx_is_segwit,
                                             const bool can_grind_r) {
    if (const auto sat_weight = desc.MaxSatisfactionWeight(!can_grind_r || UseMaxSig(txin, coin_control))) {
        if (const auto elems_count = desc.MaxSatisfactionElems()) {
            const bool is_segwit = IsSegwit(desc);
            // Account for the size of the scriptsig and the number of elements on the witness stack. Note
            // that if any input in the transaction is spending a witness program, we need to specify the
            // witness stack size for every input regardless of whether it is segwit itself.
            // NOTE: this also works in case of mixed scriptsig-and-witness such as in p2sh-wrapped segwit v0
            // outputs. In this case the size of the scriptsig length will always be one (since the redeemScript
            // is always a push of the witness program in this case, which is smaller than 253 bytes).
            const int64_t scriptsig_len = is_segwit ? 1 : GetSizeOfCompactSize(*sat_weight / WITNESS_SCALE_FACTOR);
            const int64_t witstack_len = is_segwit ? GetSizeOfCompactSize(*elems_count) : (tx_is_segwit ? 1 : 0);
            // previous txid + previous vout + sequence + scriptsig len + witstack size + scriptsig or witness
            // NOTE: sat_weight already accounts for the witness discount accordingly.
            return (32 + 4 + 4 + scriptsig_len) * WITNESS_SCALE_FACTOR + witstack_len + *sat_weight;
        }
    }

    return {};
}

static std::optional<int> CalculateKvanta5P2QRMaximumSignedInputSize(const CTxOut& txout)
{
    uint256 program;
    if (consensus::kvanta5p2qr::IsOutput(txout.scriptPubKey, program)) {
        // Native P2QR scriptSig:
        //   <4627-byte ML-DSA-87 signature> <2592-byte ML-DSA-87 pubkey>
        static constexpr int PUSHDATA2_OVERHEAD = 3;
        static constexpr int MLDSA87_SIGNATURE_SIZE = 4627;
        static constexpr int MLDSA87_PUBKEY_SIZE = 2592;

        const int script_sig_size =
            PUSHDATA2_OVERHEAD + MLDSA87_SIGNATURE_SIZE +
            PUSHDATA2_OVERHEAD + MLDSA87_PUBKEY_SIZE;

        // txin = prevout(36) + compactsize(scriptSig) + scriptSig + sequence(4)
        return 36 + GetSizeOfCompactSize(script_sig_size) + script_sig_size + 4;
    }

    CScriptID script_id;
    if (consensus::kvanta5p2qr::IsP2SHOutput(txout.scriptPubKey, script_id)) {
        // P2SH-carried P2QR scriptSig:
        //   <4627-byte ML-DSA-87 signature> <2592-byte ML-DSA-87 pubkey> <34-byte redeemScript>
        static constexpr int PUSHDATA2_OVERHEAD = 3;
        static constexpr int SMALL_PUSH_OVERHEAD = 1;
        static constexpr int MLDSA87_SIGNATURE_SIZE = 4627;
        static constexpr int MLDSA87_PUBKEY_SIZE = 2592;
        static constexpr int P2QR_REDEEM_SCRIPT_SIZE = 34; // 50 20 <32-byte program>

        const int script_sig_size =
            PUSHDATA2_OVERHEAD + MLDSA87_SIGNATURE_SIZE +
            PUSHDATA2_OVERHEAD + MLDSA87_PUBKEY_SIZE +
            SMALL_PUSH_OVERHEAD + P2QR_REDEEM_SCRIPT_SIZE;

        return 36 + GetSizeOfCompactSize(script_sig_size) + script_sig_size + 4;
    }

    return std::nullopt;
}

int CalculateMaximumSignedInputSize(const CTxOut& txout, const COutPoint outpoint, const SigningProvider* provider, bool can_grind_r, const CCoinControl* coin_control)
{
    if (const auto p2qr_input_size = CalculateKvanta5P2QRMaximumSignedInputSize(txout)) {
        return *p2qr_input_size;
    }

    if (!provider) return -1;

    if (const auto desc = InferDescriptor(txout.scriptPubKey, *provider)) {
        if (const auto weight = MaxInputWeight(*desc, {}, coin_control, true, can_grind_r)) {
            return static_cast<int>(GetVirtualTransactionSize(*weight, 0, 0));
        }
    }

    return -1;
}

int CalculateMaximumSignedInputSize(const CTxOut& txout, const CWallet* wallet, const CCoinControl* coin_control)
{
    const std::unique_ptr<SigningProvider> provider = wallet->GetSolvingProvider(txout.scriptPubKey);
    return CalculateMaximumSignedInputSize(txout, COutPoint(), provider.get(), wallet->CanGrindR(), coin_control);
}

/** Infer a descriptor for the given output script. */
static std::unique_ptr<Descriptor> GetDescriptor(const CWallet* wallet, const CCoinControl* coin_control,
                                                 const CScript script_pubkey)
{
    MultiSigningProvider providers;

    if (wallet) {
        for (const auto spkman : wallet->GetScriptPubKeyMans(script_pubkey)) {
            if (!spkman) {
                continue;
            }

            std::unique_ptr<SigningProvider> provider = spkman->GetSolvingProvider(script_pubkey);
            if (provider) {
                providers.AddProvider(std::move(provider));
            }
        }
    }

    if (coin_control) {
        providers.AddProvider(std::make_unique<FlatSigningProvider>(coin_control->m_external_provider));
    }

    return InferDescriptor(script_pubkey, providers);
}
/** Infer the maximum size of this input after it will be signed. */
/** Infer the maximum size of this input after it will be signed. */
static std::optional<int64_t> GetSignedTxinWeight(const CWallet* wallet, const CCoinControl* coin_control,
                                                  const CTxIn& txin, const CTxOut& txo, const bool tx_is_segwit,
                                                  const bool can_grind_r)
{
    // If weight was provided, use that.
    std::optional<int64_t> weight;
    if (coin_control && (weight = coin_control->GetInputWeight(txin.prevout))) {
        return weight.value();
    }

    // KV5 P2QR is not descriptor-solvable. It has a wallet-native spend path.
    // Use the wallet-verified P2QR spend info only when the wallet can prove
    // ownership/key material for this script.
    if (wallet) {
        const auto p2qr_info = wallet->GetKvanta5P2QRSpendInfoForScript(txo.scriptPubKey);
        if (p2qr_info) {
            // KVANTA5_P2QR spends are physically large because of the ML-DSA-87
            // scriptSig envelope, but fee policy prices each P2QR input like a
            // normal modern SegWit/Taproot-style spend.
            //
            // This affects wallet fee estimation only. Real transaction size,
            // block weight, serialization, and consensus validation are unchanged.
            return P2QR_POLICY_FEE_VBYTES_PER_INPUT * WITNESS_SCALE_FACTOR;
        }
    }

    // Otherwise, use the maximum satisfaction size provided by the descriptor.
    std::unique_ptr<Descriptor> desc{GetDescriptor(wallet, coin_control, txo.scriptPubKey)};
    if (desc) {
        return MaxInputWeight(*desc, {txin}, coin_control, tx_is_segwit, can_grind_r);
    }

    return {};
}

// txouts needs to be in the order of tx.vin
TxSize CalculateMaximumSignedTxSize(const CTransaction &tx, const CWallet *wallet, const std::vector<CTxOut>& txouts, const CCoinControl* coin_control)
{
    // version + nLockTime + input count + output count
    int64_t weight = (4 + 4 + GetSizeOfCompactSize(tx.vin.size()) + GetSizeOfCompactSize(tx.vout.size())) * WITNESS_SCALE_FACTOR;
    // Whether any input spends a witness program. Necessary to run before the next loop over the
    // inputs in order to accurately compute the compactSize length for the witness data per input.
    bool is_segwit = std::any_of(txouts.begin(), txouts.end(), [&](const CTxOut& txo) {
        // KV5 P2QR spends are native scriptSig spends, not witness spends.
        // Do not send P2QR through descriptor inference.
        if (wallet) {
            const auto p2qr_info = wallet->GetKvanta5P2QRSpendInfoForScript(txo.scriptPubKey);
            if (p2qr_info) {
                return false;
            }
         }

        std::unique_ptr<Descriptor> desc{GetDescriptor(wallet, coin_control, txo.scriptPubKey)};
        if (desc) return IsSegwit(*desc);
        return false;
    });
    // Segwit marker and flag
    if (is_segwit) weight += 2;

    // Add the size of the transaction outputs.
    for (const auto& txo : tx.vout) weight += GetSerializeSize(txo) * WITNESS_SCALE_FACTOR;

    // Add the size of the transaction inputs as if they were signed.
    for (uint32_t i = 0; i < txouts.size(); i++) {
        const auto txin_weight = GetSignedTxinWeight(wallet, coin_control, tx.vin[i], txouts[i], is_segwit, wallet->CanGrindR());
        if (!txin_weight) return TxSize{-1, -1};
        assert(*txin_weight > -1);
        weight += *txin_weight;
    }

    // It's ok to use 0 as the number of sigops since we never create any pathological transaction.
    return TxSize{GetVirtualTransactionSize(weight, 0, 0), weight};
}

TxSize CalculateMaximumSignedTxSize(const CTransaction &tx, const CWallet *wallet, const CCoinControl* coin_control)
{
    std::vector<CTxOut> txouts;
    // Look up the inputs. The inputs are either in the wallet, or in coin_control.
    for (const CTxIn& input : tx.vin) {
        const auto mi = wallet->mapWallet.find(input.prevout.hash);
        // Can not estimate size without knowing the input details
        if (mi != wallet->mapWallet.end()) {
            assert(input.prevout.n < mi->second.tx->vout.size());
            txouts.emplace_back(mi->second.tx->vout.at(input.prevout.n));
        } else if (coin_control) {
            const auto& txout{coin_control->GetExternalOutput(input.prevout)};
            if (!txout) return TxSize{-1, -1};
            txouts.emplace_back(*txout);
        } else {
            return TxSize{-1, -1};
        }
    }
    return CalculateMaximumSignedTxSize(tx, wallet, txouts, coin_control);
}

size_t CoinsResult::Size() const
{
    size_t size{0};
    for (const auto& it : coins) {
        size += it.second.size();
    }
    return size;
}

std::vector<COutput> CoinsResult::All() const
{
    std::vector<COutput> all;
    all.reserve(coins.size());
    for (const auto& it : coins) {
        all.insert(all.end(), it.second.begin(), it.second.end());
    }
    return all;
}

void CoinsResult::Clear() {
    coins.clear();
}

void CoinsResult::Erase(const std::unordered_set<COutPoint, SaltedOutpointHasher>& coins_to_remove)
{
    for (auto& [type, vec] : coins) {
        auto remove_it = std::remove_if(vec.begin(), vec.end(), [&](const COutput& coin) {
            // remove it if it's on the set
            if (coins_to_remove.count(coin.outpoint) == 0) return false;

            // update cached amounts
            total_amount -= coin.txout.nValue;
            if (coin.HasEffectiveValue()) total_effective_amount = *total_effective_amount - coin.GetEffectiveValue();
            return true;
        });
        vec.erase(remove_it, vec.end());
    }
}

void CoinsResult::Shuffle(FastRandomContext& rng_fast)
{
    for (auto& it : coins) {
        std::shuffle(it.second.begin(), it.second.end(), rng_fast);
    }
}

void CoinsResult::Add(OutputType type, const COutput& out)
{
    coins[type].emplace_back(out);
    total_amount += out.txout.nValue;
    if (out.HasEffectiveValue()) {
        total_effective_amount = total_effective_amount.has_value() ?
                *total_effective_amount + out.GetEffectiveValue() : out.GetEffectiveValue();
    }
}

static OutputType GetOutputType(TxoutType type, bool is_from_p2sh)
{
    switch (type) {
        case TxoutType::WITNESS_V1_TAPROOT:
            return OutputType::BECH32M;
        case TxoutType::WITNESS_V0_KEYHASH:
        case TxoutType::WITNESS_V0_SCRIPTHASH:
            if (is_from_p2sh) return OutputType::P2SH_SEGWIT;
            else return OutputType::BECH32;
        case TxoutType::SCRIPTHASH:
        case TxoutType::PUBKEYHASH:
            return OutputType::LEGACY;
        default:
            return OutputType::UNKNOWN;
    }
}

// Fetch and validate the coin control selected inputs.
// Coins could be internal (from the wallet) or external.
util::Result<PreSelectedInputs> FetchSelectedInputs(const CWallet& wallet, const CCoinControl& coin_control,
                                            const CoinSelectionParams& coin_selection_params)
{
    PreSelectedInputs result;
    const bool can_grind_r = wallet.CanGrindR();
    std::map<COutPoint, CAmount> map_of_bump_fees = wallet.chain().calculateIndividualBumpFees(coin_control.ListSelected(), coin_selection_params.m_effective_feerate);
    for (const COutPoint& outpoint : coin_control.ListSelected()) {
        int64_t input_bytes = coin_control.GetInputWeight(outpoint).value_or(-1);
        if (input_bytes != -1) {
            input_bytes = GetVirtualTransactionSize(input_bytes, 0, 0);
        }
        CTxOut txout;
        if (auto ptr_wtx = wallet.GetWalletTx(outpoint.hash)) {
            // Clearly invalid input, fail
            if (ptr_wtx->tx->vout.size() <= outpoint.n) {
                return util::Error{strprintf(_("Invalid pre-selected input %s"), outpoint.ToString())};
            }
            txout = ptr_wtx->tx->vout.at(outpoint.n);
            if (input_bytes == -1) {
                input_bytes = CalculateMaximumSignedInputSize(txout, &wallet, &coin_control);
            }
        } else {
            // The input is external. We did not find the tx in mapWallet.
            const auto out{coin_control.GetExternalOutput(outpoint)};
            if (!out) {
                return util::Error{strprintf(_("Not found pre-selected input %s"), outpoint.ToString())};
            }

            txout = *out;
        }

        if (input_bytes == -1) {
            input_bytes = CalculateMaximumSignedInputSize(txout, outpoint, &coin_control.m_external_provider, can_grind_r, &coin_control);
        }

        if (input_bytes == -1) {
            return util::Error{strprintf(_("Not solvable pre-selected input %s"), outpoint.ToString())}; // Not solvable, can't estimate size for fee
        }

        /* Set some defaults for depth, spendable, solvable, safe, time, and from_me as these don't matter for preset inputs since no selection is being done. */
        COutput output(outpoint, txout, /*depth=*/ 0, input_bytes, /*spendable=*/ true, /*solvable=*/ true, /*safe=*/ true, /*time=*/ 0, /*from_me=*/ false, coin_selection_params.m_effective_feerate);
        output.ApplyBumpFee(map_of_bump_fees.at(output.outpoint));
        result.Insert(output, coin_selection_params.m_subtract_fee_outputs);
    }
    return result;
}

CoinsResult AvailableCoins(const CWallet& wallet,
                           const CCoinControl* coinControl,
                           std::optional<CFeeRate> feerate,
                           const CoinFilterParams& params)
{
    AssertLockHeld(wallet.cs_wallet);

    CoinsResult result;
    // Either the WALLET_FLAG_AVOID_REUSE flag is not set (in which case we always allow), or we default to avoiding, and only in the case where
    // a coin control object is provided, and has the avoid address reuse flag set to false, do we allow already used addresses
    bool allow_used_addresses = !wallet.IsWalletFlagSet(WALLET_FLAG_AVOID_REUSE) || (coinControl && !coinControl->m_avoid_address_reuse);
    const int min_depth = {coinControl ? coinControl->m_min_depth : DEFAULT_MIN_DEPTH};
    const int max_depth = {coinControl ? coinControl->m_max_depth : DEFAULT_MAX_DEPTH};
    const bool only_safe = {coinControl ? !coinControl->m_include_unsafe_inputs : true};
    const bool can_grind_r = wallet.CanGrindR();
    std::vector<COutPoint> outpoints;

    std::set<uint256> trusted_parents;
    for (const auto& entry : wallet.mapWallet)
    {
        const uint256& txid = entry.first;
        const CWalletTx& wtx = entry.second;

        if (wallet.IsTxImmatureCoinBase(wtx) && !params.include_immature_coinbase)
            continue;

        int nDepth = wallet.GetTxDepthInMainChain(wtx);
        if (nDepth < 0)
            continue;

        // We should not consider coins which aren't at least in our mempool
        // It's possible for these to be conflicted via ancestors which we may never be able to detect
        if (nDepth == 0 && !wtx.InMempool())
            continue;

        bool safeTx = CachedTxIsTrusted(wallet, wtx, trusted_parents);

        // We should not consider coins from transactions that are replacing
        // other transactions.
        //
        // Example: There is a transaction A which is replaced by bumpfee
        // transaction B. In this case, we want to prevent creation of
        // a transaction B' which spends an output of B.
        //
        // Reason: If transaction A were initially confirmed, transactions B
        // and B' would no longer be valid, so the user would have to create
        // a new transaction C to replace B'. However, in the case of a
        // one-block reorg, transactions B' and C might BOTH be accepted,
        // when the user only wanted one of them. Specifically, there could
        // be a 1-block reorg away from the chain where transactions A and C
        // were accepted to another chain where B, B', and C were all
        // accepted.
        if (nDepth == 0 && wtx.mapValue.count("replaces_txid")) {
            safeTx = false;
        }

        // Similarly, we should not consider coins from transactions that
        // have been replaced. In the example above, we would want to prevent
        // creation of a transaction A' spending an output of A, because if
        // transaction B were initially confirmed, conflicting with A and
        // A', we wouldn't want to the user to create a transaction D
        // intending to replace A', but potentially resulting in a scenario
        // where A, A', and D could all be accepted (instead of just B and
        // D, or just A and A' like the user would want).
        if (nDepth == 0 && wtx.mapValue.count("replaced_by_txid")) {
            safeTx = false;
        }

        if (only_safe && !safeTx) {
            continue;
        }

        if (nDepth < min_depth || nDepth > max_depth) {
            continue;
        }

        bool tx_from_me = CachedTxIsFromMe(wallet, wtx, ISMINE_ALL);

        for (unsigned int i = 0; i < wtx.tx->vout.size(); i++) {
            const CTxOut& output = wtx.tx->vout[i];
            const COutPoint outpoint(Txid::FromUint256(txid), i);

            if (output.nValue < params.min_amount || output.nValue > params.max_amount)
                continue;

            // Skip manually selected coins (the caller can fetch them directly)
            if (coinControl && coinControl->HasSelected() && coinControl->IsSelected(outpoint))
                continue;

            if (wallet.IsLockedCoin(outpoint) && params.skip_locked)
                continue;

            if (wallet.IsSpent(outpoint))
                continue;

            isminetype mine = wallet.IsMine(output);

            if (mine == ISMINE_NO) {
                continue;
            }

            if (!allow_used_addresses && wallet.IsSpentKey(output.scriptPubKey)) {
                continue;
            }

            std::unique_ptr<SigningProvider> provider;
            int input_bytes = -1;
            bool solvable = false;
            bool spendable = false;

            const auto p2qr_spend_info = wallet.GetKvanta5P2QRSpendInfoForScript(output.scriptPubKey);

            if (p2qr_spend_info) {
                input_bytes = p2qr_spend_info->input_bytes;
                solvable = true;
                spendable = (mine & ISMINE_SPENDABLE) != ISMINE_NO;
            } else {
               provider = wallet.GetSolvingProvider(output.scriptPubKey);
               input_bytes = CalculateMaximumSignedInputSize(output, COutPoint(), provider.get(), can_grind_r, coinControl);

               // Because CalculateMaximumSignedInputSize infers a solvable descriptor to get the satisfaction size,
               // it is safe to assume that this input is solvable if input_bytes is greater than -1.
               solvable = input_bytes > -1;
               spendable = ((mine & ISMINE_SPENDABLE) != ISMINE_NO) ||
                           (((mine & ISMINE_WATCH_ONLY) != ISMINE_NO) &&
                           (coinControl && coinControl->fAllowWatchOnly && solvable));
            }
            
            

            // Filter by spendable outputs only
            if (!spendable && params.only_spendable) continue;

            // Obtain script type
            std::vector<std::vector<uint8_t>> script_solutions;
            TxoutType type = Solver(output.scriptPubKey, script_solutions);

            // If the output is P2SH and solvable, we want to know if it is
            // a P2SH (legacy) or one of P2SH-P2WPKH, P2SH-P2WSH (P2SH-Segwit). We can determine
            // this from the redeemScript. If the output is not solvable, it will be classified
            // as a P2SH (legacy), since we have no way of knowing otherwise without the redeemScript
            bool is_from_p2sh{false};

            if (p2qr_spend_info) {
                if (p2qr_spend_info->wrapped_p2sh) {
                    type = TxoutType::KVANTA5_P2QR;
                    is_from_p2sh = true;
                }
            } else if (type == TxoutType::SCRIPTHASH && solvable) {
                CScript script;
                if (!provider || !provider->GetCScript(CScriptID(uint160(script_solutions[0])), script)) {
                    continue;
            }
            type = Solver(script, script_solutions);
            is_from_p2sh = true;
            }

            result.Add(GetOutputType(type, is_from_p2sh),
                       COutput(outpoint, output, nDepth, input_bytes, spendable, solvable, safeTx, wtx.GetTxTime(), tx_from_me, feerate));

            outpoints.push_back(outpoint);

            // Checks the sum amount of all UTXO's.
            if (params.min_sum_amount != MAX_MONEY) {
                if (result.GetTotalAmount() >= params.min_sum_amount) {
                    return result;
                }
            }

            // Checks the maximum number of UTXO's.
            if (params.max_count > 0 && result.Size() >= params.max_count) {
                return result;
            }
        }
    }

    if (feerate.has_value()) {
        std::map<COutPoint, CAmount> map_of_bump_fees = wallet.chain().calculateIndividualBumpFees(outpoints, feerate.value());

        for (auto& [_, outputs] : result.coins) {
            for (auto& output : outputs) {
                output.ApplyBumpFee(map_of_bump_fees.at(output.outpoint));
            }
        }
    }

    return result;
}

CoinsResult AvailableCoinsListUnspent(const CWallet& wallet, const CCoinControl* coinControl, CoinFilterParams params)
{
    params.only_spendable = false;
    return AvailableCoins(wallet, coinControl, /*feerate=*/ std::nullopt, params);
}

const CTxOut& FindNonChangeParentOutput(const CWallet& wallet, const COutPoint& outpoint)
{
    AssertLockHeld(wallet.cs_wallet);
    const CWalletTx* wtx{Assert(wallet.GetWalletTx(outpoint.hash))};

    const CTransaction* ptx = wtx->tx.get();
    int n = outpoint.n;
    while (OutputIsChange(wallet, ptx->vout[n]) && ptx->vin.size() > 0) {
        const COutPoint& prevout = ptx->vin[0].prevout;
        const CWalletTx* it = wallet.GetWalletTx(prevout.hash);
        if (!it || it->tx->vout.size() <= prevout.n ||
            !wallet.IsMine(it->tx->vout[prevout.n])) {
            break;
        }
        ptx = it->tx.get();
        n = prevout.n;
    }
    return ptx->vout[n];
}

std::map<CTxDestination, std::vector<COutput>> ListCoins(const CWallet& wallet)
{
    AssertLockHeld(wallet.cs_wallet);

    std::map<CTxDestination, std::vector<COutput>> result;

    CCoinControl coin_control;
    // Include watch-only for LegacyScriptPubKeyMan wallets without private keys
    coin_control.fAllowWatchOnly = wallet.GetLegacyScriptPubKeyMan() && wallet.IsWalletFlagSet(WALLET_FLAG_DISABLE_PRIVATE_KEYS);
    CoinFilterParams coins_params;
    coins_params.only_spendable = false;
    coins_params.skip_locked = false;
    for (const COutput& coin : AvailableCoins(wallet, &coin_control, /*feerate=*/std::nullopt, coins_params).All()) {
        CTxDestination address;
        if ((coin.spendable || (wallet.IsWalletFlagSet(WALLET_FLAG_DISABLE_PRIVATE_KEYS) && coin.solvable))) {
            if (!ExtractDestination(FindNonChangeParentOutput(wallet, coin.outpoint).scriptPubKey, address)) {
                // For backwards compatibility, we convert P2PK output scripts into PKHash destinations
                if (auto pk_dest = std::get_if<PubKeyDestination>(&address)) {
                    address = PKHash(pk_dest->GetPubKey());
                } else {
                    continue;
                }
            }
            result[address].emplace_back(coin);
        }
    }
    return result;
}

FilteredOutputGroups GroupOutputs(const CWallet& wallet,
                          const CoinsResult& coins,
                          const CoinSelectionParams& coin_sel_params,
                          const std::vector<SelectionFilter>& filters,
                          std::vector<OutputGroup>& ret_discarded_groups)
{
    FilteredOutputGroups filtered_groups;

    if (!coin_sel_params.m_avoid_partial_spends) {
        // Allowing partial spends means no grouping. Each COutput gets its own OutputGroup
        for (const auto& [type, outputs] : coins.coins) {
            for (const COutput& output : outputs) {
                // Get mempool info
                size_t ancestors, descendants;
                wallet.chain().getTransactionAncestry(output.outpoint.hash, ancestors, descendants);

                // Create a new group per output and add it to the all groups vector
                OutputGroup group(coin_sel_params);
                group.Insert(std::make_shared<COutput>(output), ancestors, descendants);

                // Each filter maps to a different set of groups
                bool accepted = false;
                for (const auto& sel_filter : filters) {
                    const auto& filter = sel_filter.filter;
                    if (!group.EligibleForSpending(filter)) continue;
                    filtered_groups[filter].Push(group, type, /*insert_positive=*/true, /*insert_mixed=*/true);
                    accepted = true;
                }
                if (!accepted) ret_discarded_groups.emplace_back(group);
            }
        }
        return filtered_groups;
    }

    // We want to combine COutputs that have the same scriptPubKey into single OutputGroups
    // except when there are more than OUTPUT_GROUP_MAX_ENTRIES COutputs grouped in an OutputGroup.
    // To do this, we maintain a map where the key is the scriptPubKey and the value is a vector of OutputGroups.
    // For each COutput, we check if the scriptPubKey is in the map, and if it is, the COutput is added
    // to the last OutputGroup in the vector for the scriptPubKey. When the last OutputGroup has
    // OUTPUT_GROUP_MAX_ENTRIES COutputs, a new OutputGroup is added to the end of the vector.
    typedef std::map<std::pair<CScript, OutputType>, std::vector<OutputGroup>> ScriptPubKeyToOutgroup;
    const auto& insert_output = [&](
            const std::shared_ptr<COutput>& output, OutputType type, size_t ancestors, size_t descendants,
            ScriptPubKeyToOutgroup& groups_map) {
        std::vector<OutputGroup>& groups = groups_map[std::make_pair(output->txout.scriptPubKey,type)];

        if (groups.size() == 0) {
            // No OutputGroups for this scriptPubKey yet, add one
            groups.emplace_back(coin_sel_params);
        }

        // Get the last OutputGroup in the vector so that we can add the COutput to it
        // A pointer is used here so that group can be reassigned later if it is full.
        OutputGroup* group = &groups.back();

        // Check if this OutputGroup is full. We limit to OUTPUT_GROUP_MAX_ENTRIES when using -avoidpartialspends
        // to avoid surprising users with very high fees.
        if (group->m_outputs.size() >= OUTPUT_GROUP_MAX_ENTRIES) {
            // The last output group is full, add a new group to the vector and use that group for the insertion
            groups.emplace_back(coin_sel_params);
            group = &groups.back();
        }

        group->Insert(output, ancestors, descendants);
    };

    ScriptPubKeyToOutgroup spk_to_groups_map;
    ScriptPubKeyToOutgroup spk_to_positive_groups_map;
    for (const auto& [type, outs] : coins.coins) {
        for (const COutput& output : outs) {
            size_t ancestors, descendants;
            wallet.chain().getTransactionAncestry(output.outpoint.hash, ancestors, descendants);

            const auto& shared_output = std::make_shared<COutput>(output);
            // Filter for positive only before adding the output
            if (output.GetEffectiveValue() > 0) {
                insert_output(shared_output, type, ancestors, descendants, spk_to_positive_groups_map);
            }

            // 'All' groups
            insert_output(shared_output, type, ancestors, descendants, spk_to_groups_map);
        }
    }

    // Now we go through the entire maps and pull out the OutputGroups
    const auto& push_output_groups = [&](const ScriptPubKeyToOutgroup& groups_map, bool positive_only) {
        for (const auto& [script, groups] : groups_map) {
            // Go through the vector backwards. This allows for the first item we deal with being the partial group.
            for (auto group_it = groups.rbegin(); group_it != groups.rend(); group_it++) {
                const OutputGroup& group = *group_it;

                // Each filter maps to a different set of groups
                bool accepted = false;
                for (const auto& sel_filter : filters) {
                    const auto& filter = sel_filter.filter;
                    if (!group.EligibleForSpending(filter)) continue;

                    // Don't include partial groups if there are full groups too and we don't want partial groups
                    if (group_it == groups.rbegin() && groups.size() > 1 && !filter.m_include_partial_groups) {
                        continue;
                    }

                    OutputType type = script.second;
                    // Either insert the group into the positive-only groups or the mixed ones.
                    filtered_groups[filter].Push(group, type, positive_only, /*insert_mixed=*/!positive_only);
                    accepted = true;
                }
                if (!accepted) ret_discarded_groups.emplace_back(group);
            }
        }
    };

    push_output_groups(spk_to_groups_map, /*positive_only=*/ false);
    push_output_groups(spk_to_positive_groups_map, /*positive_only=*/ true);

    return filtered_groups;
}

FilteredOutputGroups GroupOutputs(const CWallet& wallet,
                                  const CoinsResult& coins,
                                  const CoinSelectionParams& params,
                                  const std::vector<SelectionFilter>& filters)
{
    std::vector<OutputGroup> unused;
    return GroupOutputs(wallet, coins, params, filters, unused);
}

// Returns true if the result contains an error and the message is not empty
static bool HasErrorMsg(const util::Result<SelectionResult>& res) { return !util::ErrorString(res).empty(); }

util::Result<SelectionResult> AttemptSelection(interfaces::Chain& chain, const CAmount& nTargetValue, OutputGroupTypeMap& groups,
                               const CoinSelectionParams& coin_selection_params, bool allow_mixed_output_types)
{
    // Run coin selection on each OutputType and compute the Waste Metric
    std::vector<SelectionResult> results;
    for (auto& [type, group] : groups.groups_by_type) {
        auto result{ChooseSelectionResult(chain, nTargetValue, group, coin_selection_params)};
        // If any specific error message appears here, then something particularly wrong happened.
        if (HasErrorMsg(result)) return result; // So let's return the specific error.
        // Append the favorable result.
        if (result) results.push_back(*result);
    }
    // If we have at least one solution for funding the transaction without mixing, choose the minimum one according to waste metric
    // and return the result
    if (results.size() > 0) return *std::min_element(results.begin(), results.end());

    // If we can't fund the transaction from any individual OutputType, run coin selection one last time
    // over all available coins, which would allow mixing.
    // If TypesCount() <= 1, there is nothing to mix.
    if (allow_mixed_output_types && groups.TypesCount() > 1) {
        return ChooseSelectionResult(chain, nTargetValue, groups.all_groups, coin_selection_params);
    }
    // Either mixing is not allowed and we couldn't find a solution from any single OutputType, or mixing was allowed and we still couldn't
    // find a solution using all available coins
    return util::Error();
};

util::Result<SelectionResult> ChooseSelectionResult(interfaces::Chain& chain, const CAmount& nTargetValue, Groups& groups, const CoinSelectionParams& coin_selection_params)
{
    // Vector of results. We will choose the best one based on waste.
    std::vector<SelectionResult> results;
    std::vector<util::Result<SelectionResult>> errors;
    auto append_error = [&] (util::Result<SelectionResult>&& result) {
        // If any specific error message appears here, then something different from a simple "no selection found" happened.
        // Let's save it, so it can be retrieved to the user if no other selection algorithm succeeded.
        if (HasErrorMsg(result)) {
            errors.emplace_back(std::move(result));
        }
    };

    // Maximum allowed weight for selected coins.
    int max_transaction_weight = coin_selection_params.m_max_tx_weight.value_or(MAX_STANDARD_TX_WEIGHT);
    int tx_weight_no_input = coin_selection_params.tx_noinputs_size * WITNESS_SCALE_FACTOR;
    int max_selection_weight = max_transaction_weight - tx_weight_no_input;
    if (max_selection_weight <= 0) {
        return util::Error{_("Maximum transaction weight is less than transaction weight without inputs")};
    }

    // SFFO frequently causes issues in the context of changeless input sets: skip BnB when SFFO is active
    if (!coin_selection_params.m_subtract_fee_outputs) {
        if (auto bnb_result{SelectCoinsBnB(groups.positive_group, nTargetValue, coin_selection_params.m_cost_of_change, max_selection_weight)}) {
            results.push_back(*bnb_result);
        } else append_error(std::move(bnb_result));
    }

    // Deduct change weight because remaining Coin Selection algorithms can create change output
    int change_outputs_weight = coin_selection_params.change_output_size * WITNESS_SCALE_FACTOR;
    max_selection_weight -= change_outputs_weight;
    if (max_selection_weight < 0 && results.empty()) {
        return util::Error{_("Maximum transaction weight is too low, can not accommodate change output")};
    }

    // The knapsack solver has some legacy behavior where it will spend dust outputs. We retain this behavior, so don't filter for positive only here.
    if (auto knapsack_result{KnapsackSolver(groups.mixed_group, nTargetValue, coin_selection_params.m_min_change_target, coin_selection_params.rng_fast, max_selection_weight)}) {
        results.push_back(*knapsack_result);
    } else append_error(std::move(knapsack_result));

    if (coin_selection_params.m_effective_feerate > CFeeRate{3 * coin_selection_params.m_long_term_feerate}) { // Minimize input set for feerates of at least 3×LTFRE (default: 30 ṩ/vB+)
        if (auto cg_result{CoinGrinder(groups.positive_group, nTargetValue, coin_selection_params.m_min_change_target, max_selection_weight)}) {
            cg_result->RecalculateWaste(coin_selection_params.min_viable_change, coin_selection_params.m_cost_of_change, coin_selection_params.m_change_fee);
            results.push_back(*cg_result);
        } else {
            append_error(std::move(cg_result));
        }
    }

    if (auto srd_result{SelectCoinsSRD(groups.positive_group, nTargetValue, coin_selection_params.m_change_fee, coin_selection_params.rng_fast, max_selection_weight)}) {
        results.push_back(*srd_result);
    } else append_error(std::move(srd_result));

    if (results.empty()) {
        // No solution found, retrieve the first explicit error (if any).
        // future: add 'severity level' to errors so the worst one can be retrieved instead of the first one.
        return errors.empty() ? util::Error() : std::move(errors.front());
    }

    // If the chosen input set has unconfirmed inputs, check for synergies from overlapping ancestry
    for (auto& result : results) {
        std::vector<COutPoint> outpoints;
        std::set<std::shared_ptr<COutput>> coins = result.GetInputSet();
        CAmount summed_bump_fees = 0;
        for (auto& coin : coins) {
            if (coin->depth > 0) continue; // Bump fees only exist for unconfirmed inputs
            outpoints.push_back(coin->outpoint);
            summed_bump_fees += coin->ancestor_bump_fees;
        }
        std::optional<CAmount> combined_bump_fee = chain.calculateCombinedBumpFee(outpoints, coin_selection_params.m_effective_feerate);
        if (!combined_bump_fee.has_value()) {
            return util::Error{_("Failed to calculate bump fees, because unconfirmed UTXOs depend on enormous cluster of unconfirmed transactions.")};
        }
        CAmount bump_fee_overestimate = summed_bump_fees - combined_bump_fee.value();
        if (bump_fee_overestimate) {
            result.SetBumpFeeDiscount(bump_fee_overestimate);
        }
        result.RecalculateWaste(coin_selection_params.min_viable_change, coin_selection_params.m_cost_of_change, coin_selection_params.m_change_fee);
    }

    // Choose the result with the least waste
    // If the waste is the same, choose the one which spends more inputs.
    return *std::min_element(results.begin(), results.end());
}

util::Result<SelectionResult> SelectCoins(const CWallet& wallet, CoinsResult& available_coins, const PreSelectedInputs& pre_set_inputs,
                                          const CAmount& nTargetValue, const CCoinControl& coin_control,
                                          const CoinSelectionParams& coin_selection_params)
{
    // Deduct preset inputs amount from the search target
    CAmount selection_target = nTargetValue - pre_set_inputs.total_amount;

    // Return if automatic coin selection is disabled, and we don't cover the selection target
    if (!coin_control.m_allow_other_inputs && selection_target > 0) {
        return util::Error{_("The preselected coins total amount does not cover the transaction target. "
                             "Please allow other inputs to be automatically selected or include more coins manually")};
    }

    // Return if we can cover the target only with the preset inputs
    if (selection_target <= 0) {
        SelectionResult result(nTargetValue, SelectionAlgorithm::MANUAL);
        result.AddInputs(pre_set_inputs.coins, coin_selection_params.m_subtract_fee_outputs);
        result.RecalculateWaste(coin_selection_params.min_viable_change, coin_selection_params.m_cost_of_change, coin_selection_params.m_change_fee);
        return result;
    }

    // Return early if we cannot cover the target with the wallet's UTXO.
    // We use the total effective value if we are not subtracting fee from outputs and 'available_coins' contains the data.
    CAmount available_coins_total_amount = coin_selection_params.m_subtract_fee_outputs ? available_coins.GetTotalAmount() :
            (available_coins.GetEffectiveTotalAmount().has_value() ? *available_coins.GetEffectiveTotalAmount() : 0);
    if (selection_target > available_coins_total_amount) {
        return util::Error(); // Insufficient funds
    }

    // Start wallet Coin Selection procedure
    auto op_selection_result = AutomaticCoinSelection(wallet, available_coins, selection_target, coin_selection_params);
    if (!op_selection_result) return op_selection_result;

    // If needed, add preset inputs to the automatic coin selection result
    if (!pre_set_inputs.coins.empty()) {
        SelectionResult preselected(pre_set_inputs.total_amount, SelectionAlgorithm::MANUAL);
        preselected.AddInputs(pre_set_inputs.coins, coin_selection_params.m_subtract_fee_outputs);
        op_selection_result->Merge(preselected);
        op_selection_result->RecalculateWaste(coin_selection_params.min_viable_change,
                                                coin_selection_params.m_cost_of_change,
                                                coin_selection_params.m_change_fee);

        // Verify we haven't exceeded the maximum allowed weight
        int max_inputs_weight = coin_selection_params.m_max_tx_weight.value_or(MAX_STANDARD_TX_WEIGHT) - (coin_selection_params.tx_noinputs_size * WITNESS_SCALE_FACTOR);
        if (op_selection_result->GetWeight() > max_inputs_weight) {
            return util::Error{_("The combination of the pre-selected inputs and the wallet automatic inputs selection exceeds the transaction maximum weight. "
                                 "Please try sending a smaller amount or manually consolidating your wallet's UTXOs")};
        }
    }
    return op_selection_result;
}

util::Result<SelectionResult> AutomaticCoinSelection(const CWallet& wallet, CoinsResult& available_coins, const CAmount& value_to_select, const CoinSelectionParams& coin_selection_params)
{
    unsigned int limit_ancestor_count = 0;
    unsigned int limit_descendant_count = 0;
    wallet.chain().getPackageLimits(limit_ancestor_count, limit_descendant_count);
    const size_t max_ancestors = (size_t)std::max<int64_t>(1, limit_ancestor_count);
    const size_t max_descendants = (size_t)std::max<int64_t>(1, limit_descendant_count);
    const bool fRejectLongChains = gArgs.GetBoolArg("-walletrejectlongchains", DEFAULT_WALLET_REJECT_LONG_CHAINS);

    // Cases where we have 101+ outputs all pointing to the same destination may result in
    // privacy leaks as they will potentially be deterministically sorted. We solve that by
    // explicitly shuffling the outputs before processing
    if (coin_selection_params.m_avoid_partial_spends && available_coins.Size() > OUTPUT_GROUP_MAX_ENTRIES) {
        available_coins.Shuffle(coin_selection_params.rng_fast);
    }

    // Coin Selection attempts to select inputs from a pool of eligible UTXOs to fund the
    // transaction at a target feerate. If an attempt fails, more attempts may be made using a more
    // permissive CoinEligibilityFilter.
    {
        // Place coins eligibility filters on a scope increasing order.
        std::vector<SelectionFilter> ordered_filters{
                // If possible, fund the transaction with confirmed UTXOs only. Prefer at least six
                // confirmations on outputs received from other wallets and only spend confirmed change.
                {CoinEligibilityFilter(1, 6, 0), /*allow_mixed_output_types=*/false},
                {CoinEligibilityFilter(1, 1, 0)},
        };
        // Fall back to using zero confirmation change (but with as few ancestors in the mempool as
        // possible) if we cannot fund the transaction otherwise.
        if (wallet.m_spend_zero_conf_change) {
            ordered_filters.push_back({CoinEligibilityFilter(0, 1, 2)});
            ordered_filters.push_back({CoinEligibilityFilter(0, 1, std::min(size_t{4}, max_ancestors/3), std::min(size_t{4}, max_descendants/3))});
            ordered_filters.push_back({CoinEligibilityFilter(0, 1, max_ancestors/2, max_descendants/2)});
            // If partial groups are allowed, relax the requirement of spending OutputGroups (groups
            // of UTXOs sent to the same address, which are obviously controlled by a single wallet)
            // in their entirety.
            ordered_filters.push_back({CoinEligibilityFilter(0, 1, max_ancestors-1, max_descendants-1, /*include_partial=*/true)});
            // Try with unsafe inputs if they are allowed. This may spend unconfirmed outputs
            // received from other wallets.
            if (coin_selection_params.m_include_unsafe_inputs) {
                ordered_filters.push_back({CoinEligibilityFilter(/*conf_mine=*/0, /*conf_theirs*/0, max_ancestors-1, max_descendants-1, /*include_partial=*/true)});
            }
            // Try with unlimited ancestors/descendants. The transaction will still need to meet
            // mempool ancestor/descendant policy to be accepted to mempool and broadcasted, but
            // OutputGroups use heuristics that may overestimate ancestor/descendant counts.
            if (!fRejectLongChains) {
                ordered_filters.push_back({CoinEligibilityFilter(0, 1, std::numeric_limits<uint64_t>::max(),
                                                                   std::numeric_limits<uint64_t>::max(),
                                                                   /*include_partial=*/true)});
            }
        }

        // Group outputs and map them by coin eligibility filter
        std::vector<OutputGroup> discarded_groups;
        FilteredOutputGroups filtered_groups = GroupOutputs(wallet, available_coins, coin_selection_params, ordered_filters, discarded_groups);

        // Check if we still have enough balance after applying filters (some coins might be discarded)
        CAmount total_discarded = 0;
        CAmount total_unconf_long_chain = 0;
        for (const auto& group : discarded_groups) {
            total_discarded += group.GetSelectionAmount();
            if (group.m_ancestors >= max_ancestors || group.m_descendants >= max_descendants) total_unconf_long_chain += group.GetSelectionAmount();
        }

        if (CAmount total_amount = available_coins.GetTotalAmount() - total_discarded < value_to_select) {
            // Special case, too-long-mempool cluster.
            if (total_amount + total_unconf_long_chain > value_to_select) {
                return util::Error{_("Unconfirmed UTXOs are available, but spending them creates a chain of transactions that will be rejected by the mempool")};
            }
            return util::Error{}; // General "Insufficient Funds"
        }

        // Walk-through the filters until the solution gets found.
        // If no solution is found, return the first detailed error (if any).
        // future: add "error level" so the worst one can be picked instead.
        std::vector<util::Result<SelectionResult>> res_detailed_errors;
        for (const auto& select_filter : ordered_filters) {
            auto it = filtered_groups.find(select_filter.filter);
            if (it == filtered_groups.end()) continue;
            if (auto res{AttemptSelection(wallet.chain(), value_to_select, it->second,
                                          coin_selection_params, select_filter.allow_mixed_output_types)}) {
                return res; // result found
            } else {
                // If any specific error message appears here, then something particularly wrong might have happened.
                // Save the error and continue the selection process. So if no solutions gets found, we can return
                // the detailed error to the upper layers.
                if (HasErrorMsg(res)) res_detailed_errors.emplace_back(std::move(res));
            }
        }

        // Return right away if we have a detailed error
        if (!res_detailed_errors.empty()) return std::move(res_detailed_errors.front());


        // General "Insufficient Funds"
        return util::Error{};
    }
}

static bool IsCurrentForAntiFeeSniping(interfaces::Chain& chain, const uint256& block_hash)
{
    if (chain.isInitialBlockDownload()) {
        return false;
    }
    constexpr int64_t MAX_ANTI_FEE_SNIPING_TIP_AGE = 8 * 60 * 60; // in seconds
    int64_t block_time;
    CHECK_NONFATAL(chain.findBlock(block_hash, FoundBlock().time(block_time)));
    if (block_time < (GetTime() - MAX_ANTI_FEE_SNIPING_TIP_AGE)) {
        return false;
    }
    return true;
}

/**
 * Set a height-based locktime for new transactions (uses the height of the
 * current chain tip unless we are not synced with the current chain
 */
static void DiscourageFeeSniping(CMutableTransaction& tx, FastRandomContext& rng_fast,
                                 interfaces::Chain& chain, const uint256& block_hash, int block_height)
{
    // All inputs must be added by now
    assert(!tx.vin.empty());
    // Discourage fee sniping.
    //
    // For a large miner the value of the transactions in the best block and
    // the mempool can exceed the cost of deliberately attempting to mine two
    // blocks to orphan the current best block. By setting nLockTime such that
    // only the next block can include the transaction, we discourage this
    // practice as the height restricted and limited blocksize gives miners
    // considering fee sniping fewer options for pulling off this attack.
    //
    // A simple way to think about this is from the wallet's point of view we
    // always want the blockchain to move forward. By setting nLockTime this
    // way we're basically making the statement that we only want this
    // transaction to appear in the next block; we don't want to potentially
    // encourage reorgs by allowing transactions to appear at lower heights
    // than the next block in forks of the best chain.
    //
    // Of course, the subsidy is high enough, and transaction volume low
    // enough, that fee sniping isn't a problem yet, but by implementing a fix
    // now we ensure code won't be written that makes assumptions about
    // nLockTime that preclude a fix later.
    if (IsCurrentForAntiFeeSniping(chain, block_hash)) {
        tx.nLockTime = block_height;

        // Secondly occasionally randomly pick a nLockTime even further back, so
        // that transactions that are delayed after signing for whatever reason,
        // e.g. high-latency mix networks and some CoinJoin implementations, have
        // better privacy.
        if (rng_fast.randrange(10) == 0) {
            tx.nLockTime = std::max(0, int(tx.nLockTime) - int(rng_fast.randrange(100)));
        }
    } else {
        // If our chain is lagging behind, we can't discourage fee sniping nor help
        // the privacy of high-latency transactions. To avoid leaking a potentially
        // unique "nLockTime fingerprint", set nLockTime to a constant.
        tx.nLockTime = 0;
    }
    // Sanity check all values
    assert(tx.nLockTime < LOCKTIME_THRESHOLD); // Type must be block height
    assert(tx.nLockTime <= uint64_t(block_height));
    for (const auto& in : tx.vin) {
        // Can not be FINAL for locktime to work
        assert(in.nSequence != CTxIn::SEQUENCE_FINAL);
        // May be MAX NONFINAL to disable both BIP68 and BIP125
        if (in.nSequence == CTxIn::MAX_SEQUENCE_NONFINAL) continue;
        // May be MAX BIP125 to disable BIP68 and enable BIP125
        if (in.nSequence == MAX_BIP125_RBF_SEQUENCE) continue;
        // The wallet does not support any other sequence-use right now.
        assert(false);
    }
}

size_t GetSerializeSizeForRecipient(const CRecipient& recipient)
{
    return ::GetSerializeSize(CTxOut(recipient.nAmount, GetScriptForDestination(recipient.dest)));
}

bool IsDust(const CRecipient& recipient, const CFeeRate& dustRelayFee)
{
    return ::IsDust(CTxOut(recipient.nAmount, GetScriptForDestination(recipient.dest)), dustRelayFee);
}

enum class Kvanta5ChangeKind {
    NONE,
    NATIVE_P2QR,
    COMPAT_P2QR,
};

/*
 * Classify a script as a Kvanta5 P2QR-family output.
 *
 * This does NOT require wallet ownership.
 * Use this for recipients and general output validation.
 */
static Kvanta5ChangeKind GetKvanta5OutputKindForScript(const CScript& script_pub_key)
{
    std::vector<std::vector<unsigned char>> solutions;
    const TxoutType type = Solver(script_pub_key, solutions);

    if (type == TxoutType::KVANTA5_P2QR) {
        return Kvanta5ChangeKind::NATIVE_P2QR;
    }

    /*
     * Wrapped P2SH compatibility recipient.
     *
     * External wrapped addresses look like ordinary P2SH at output creation
     * time. The redeem script is not visible until spend time. Therefore the
     * send/output path must allow SCRIPTHASH as the compatibility wrapper.
     *
     * Actual spending still fails closed unless the revealed redeemScript is:
     *
     *     OP_RESERVED <32-byte P2QR program>
     *
     * and the native P2QR ML-DSA validation succeeds.
     */
    if (type == TxoutType::SCRIPTHASH) {
        return Kvanta5ChangeKind::COMPAT_P2QR;
    }

    return Kvanta5ChangeKind::NONE;
}

/*
 * Classify a wallet-owned/spendable Kvanta5 P2QR-family script.
 *
 * This DOES require wallet ownership/spend info.
 * Use this for selected inputs and change safety checks, not external recipients.
 */
static Kvanta5ChangeKind GetKvanta5OwnedChangeKindForScript(const CWallet& wallet, const CScript& script_pub_key)
{
    AssertLockHeld(wallet.cs_wallet);

    const auto p2qr_info = wallet.GetKvanta5P2QRSpendInfoForScript(script_pub_key);
    if (!p2qr_info) {
        return Kvanta5ChangeKind::NONE;
    }

    std::vector<std::vector<unsigned char>> solutions;
    const TxoutType type = Solver(script_pub_key, solutions);

    if (type == TxoutType::KVANTA5_P2QR && !p2qr_info->wrapped_p2sh) {
        return Kvanta5ChangeKind::NATIVE_P2QR;
    }

    if (type == TxoutType::SCRIPTHASH && p2qr_info->wrapped_p2sh) {
        return Kvanta5ChangeKind::COMPAT_P2QR;
    }

    return Kvanta5ChangeKind::NONE;
}

/*
 * Compatibility wrapper for existing callers.
 *
 * Keep the old name if other wallet code already calls it expecting ownership.
 */
static Kvanta5ChangeKind GetKvanta5ChangeKindForScript(const CWallet& wallet, const CScript& script_pub_key)
{
    return GetKvanta5OwnedChangeKindForScript(wallet, script_pub_key);
}

static util::Result<Kvanta5ChangeKind> GetKvanta5ChangeKindForTransaction(
    const CWallet& wallet,
    const std::vector<CRecipient>& vecSend,
    const CCoinControl& coin_control)
{
    AssertLockHeld(wallet.cs_wallet);

    bool has_native_p2qr{false};
    bool has_compat_p2qr{false};

    /*
     * Recipients may be external. Do NOT require wallet ownership here.
     */
    for (const auto& recipient : vecSend) {
        const CScript script_pub_key = GetScriptForDestination(recipient.dest);

        switch (GetKvanta5OutputKindForScript(script_pub_key)) {
        case Kvanta5ChangeKind::NATIVE_P2QR:
            has_native_p2qr = true;
            break;

        case Kvanta5ChangeKind::COMPAT_P2QR:
            has_compat_p2qr = true;
            break;

        case Kvanta5ChangeKind::NONE:
            return util::Error{_("Kvanta5 wallet refuses to create transactions to non-P2QR outputs.")};
        }
    }

    /*
     * Selected inputs must be wallet-owned/spendable P2QR-family coins.
     */
    for (const COutPoint& outpoint : coin_control.ListSelected()) {
        const CWalletTx* wtx = wallet.GetWalletTx(outpoint.hash);
        if (!wtx || outpoint.n >= wtx->tx->vout.size()) {
            continue;
        }

        const CScript& script_pub_key = wtx->tx->vout[outpoint.n].scriptPubKey;

        switch (GetKvanta5OwnedChangeKindForScript(wallet, script_pub_key)) {
        case Kvanta5ChangeKind::NATIVE_P2QR:
            has_native_p2qr = true;
            break;

        case Kvanta5ChangeKind::COMPAT_P2QR:
            has_compat_p2qr = true;
            break;

        case Kvanta5ChangeKind::NONE:
            return util::Error{_("Kvanta5 wallet refuses to spend non-P2QR wallet inputs.")};
        }
    }

    /*
     * Do NOT reject mixed native/compat P2QR.
     *
     * The wallet hardening rule should be:
     *   allowed: native P2QR + compatibility-wrapper P2QR
     *   rejected: anything outside the Kvanta5 P2QR family
     *
     * Rejecting mixed native/compat breaks normal sends, fee bumping, RBF,
     * consolidation, and self-sends.
     */

    if (has_compat_p2qr) {
        return Kvanta5ChangeKind::COMPAT_P2QR;
    }

    if (has_native_p2qr) {
        return Kvanta5ChangeKind::NATIVE_P2QR;
    }

    return util::Error{_("Kvanta5 transaction has no native P2QR or compatibility-wrapper P2QR context.")};
}

static util::Result<void> CheckKvanta5WalletCreatedOutputs(const CWallet& wallet, const CMutableTransaction& tx)
{
    AssertLockHeld(wallet.cs_wallet);

    for (const auto& txout : tx.vout) {
        std::vector<std::vector<unsigned char>> solutions;
        const TxoutType type = Solver(txout.scriptPubKey, solutions);

        if (type == TxoutType::WITNESS_V1_TAPROOT) {
            return util::Error{_("Kvanta5 wallet attempted to create Taproot output. Refusing transaction.")};
        }

        if (type == TxoutType::WITNESS_V0_KEYHASH || type == TxoutType::WITNESS_V0_SCRIPTHASH) {
            return util::Error{_("Kvanta5 wallet attempted to create SegWit output. Refusing transaction.")};
        }

        /*
         * This checks whether the output script is P2QR-family.
         *
         * Do NOT require wallet ownership here. Recipient outputs can belong
         * to another wallet. Change ownership should be checked separately
         * where the change output position is known.
         */
        const Kvanta5ChangeKind kind = GetKvanta5OutputKindForScript(txout.scriptPubKey);
        if (kind == Kvanta5ChangeKind::NONE) {
            return util::Error{_("Kvanta5 wallet attempted to create a non-P2QR output. Refusing transaction.")};
        }
    }

    return {};
}

static util::Result<CAmount> Kvanta5ProportionalAmount(
    const CAmount amount,
    const CAmount numerator,
    const CAmount denominator)
{
    if (amount < 0) {
        return util::Error{Untranslated(strprintf(
            "Kvanta5 proportional amount failed: amount is negative: amount=%s",
            FormatMoney(amount)))};
    }

    if (numerator < 0) {
        return util::Error{Untranslated(strprintf(
            "Kvanta5 proportional amount failed: numerator is negative: numerator=%s",
            FormatMoney(numerator)))};
    }

    if (denominator <= 0) {
        return util::Error{Untranslated(strprintf(
            "Kvanta5 proportional amount failed: denominator is non-positive: denominator=%s",
            FormatMoney(denominator)))};
    }

    arith_uint256 product{static_cast<uint64_t>(amount)};
    product *= arith_uint256{static_cast<uint64_t>(numerator)};
    product /= arith_uint256{static_cast<uint64_t>(denominator)};

    const arith_uint256 max_amount{static_cast<uint64_t>(std::numeric_limits<CAmount>::max())};
    if (product > max_amount) {
        return util::Error{Untranslated(strprintf(
            "Kvanta5 proportional amount overflow: amount=%s numerator=%s denominator=%s",
            FormatMoney(amount),
            FormatMoney(numerator),
            FormatMoney(denominator)))};
    }

    return static_cast<CAmount>(product.GetLow64());
}


struct Kvanta5SelectedInputDomains
{
    CAmount native_value{0};
    CAmount compat_value{0};

    bool has_native{false};
    bool has_compat{false};

    CAmount Total() const
    {
        return native_value + compat_value;
    }

    bool IsMixed() const
    {
        return has_native && has_compat;
    }
};

static util::Result<Kvanta5SelectedInputDomains> GetKvanta5SelectedInputDomains(
    const CWallet& wallet,
    const SelectionResult& result)
{
    AssertLockHeld(wallet.cs_wallet);

    Kvanta5SelectedInputDomains domains;

    for (const auto& coin : result.GetInputSet()) {
        const CScript& script_pub_key = coin->txout.scriptPubKey;
        const CAmount value = coin->txout.nValue;

        switch (GetKvanta5OwnedChangeKindForScript(wallet, script_pub_key)) {
        case Kvanta5ChangeKind::NATIVE_P2QR:
            domains.has_native = true;
            domains.native_value += value;
            break;

        case Kvanta5ChangeKind::COMPAT_P2QR:
            domains.has_compat = true;
            domains.compat_value += value;
            break;

        case Kvanta5ChangeKind::NONE:
            return util::Error{_("Kvanta5 wallet selected a non-P2QR input. Refusing transaction.")};
        }
    }

    if (!domains.has_native && !domains.has_compat) {
        return util::Error{_("Kvanta5 wallet selected no native P2QR or wrapped P2SH P2QR inputs.")};
    }

    return domains;
}

static util::Result<int64_t> GetKvanta5WalletAdjustedVSize(
    const CWallet& wallet,
    const CTransaction& tx,
    const std::vector<std::shared_ptr<COutput>>& selected_coins)
{
    AssertLockHeld(wallet.cs_wallet);

    CCoinsView dummy_view;
    CCoinsViewCache view(&dummy_view);

    for (const auto& coin : selected_coins) {
        Coin prev_coin;
        prev_coin.out = coin->txout;

        /*
         * Height/coinbase are not used by GetP2QRAdjustedVirtualTransactionSize().
         * The adjusted-size logic only needs the prevout scriptPubKey so it can
         * identify native P2QR / P2SH-carried P2QR spends and discount the QR auth
         * payload correctly.
         */
        prev_coin.nHeight = coin->depth > 0 ? wallet.GetLastBlockHeight() - coin->depth + 1 : 0;
        prev_coin.fCoinBase = false;

        view.AddCoin(coin->outpoint, std::move(prev_coin), /*possible_overwrite=*/true);
    }

    const int64_t adjusted_vsize = GetP2QRAdjustedVirtualTransactionSize(tx, view);
    if (adjusted_vsize <= 0) {
        return util::Error{_("Kvanta5 adjusted transaction vsize calculation failed.")};
    }

    return adjusted_vsize;
}

static void ClearKvanta5MutableTxSignatures(CMutableTransaction& tx)
{
    for (CTxIn& txin : tx.vin) {
        txin.scriptSig.clear();
        txin.scriptWitness.SetNull();
    }
}

static util::Result<void> ReduceKvanta5ChangeForFeeDeficit(
    CMutableTransaction& tx,
    const std::optional<unsigned int>& change_pos,
    const CAmount deficit,
    const CFeeRate& dust_relay_fee,
    const std::vector<CRecipient>& recipients)
{
    if (deficit <= 0) {
        return {};
    }

    /*
     * First try normal change reduction.
     */
    if (change_pos) {
        for (unsigned int i = *change_pos; i < tx.vout.size(); ++i) {
            CTxOut& txout = tx.vout[i];

            if (txout.nValue <= deficit) {
                continue;
            }

            CTxOut reduced(txout.nValue - deficit, txout.scriptPubKey);
            if (IsDust(reduced, dust_relay_fee)) {
                continue;
            }

            txout.nValue -= deficit;
            return {};
        }
    }

    /*
     * If no usable change exists, fall back to subtract-fee-from-amount
     * recipient outputs. This is required for send-available-balance style
     * transactions where the wallet intentionally creates no change output.
     */
    std::vector<unsigned int> subtract_fee_outputs;

    for (unsigned int i = 0; i < tx.vout.size(); ++i) {
        for (const CRecipient& recipient : recipients) {
            if (!recipient.fSubtractFeeFromAmount) {
                continue;
            }

            const CScript recipient_script = GetScriptForDestination(recipient.dest);
            if (tx.vout[i].scriptPubKey == recipient_script) {
                if (std::find(subtract_fee_outputs.begin(), subtract_fee_outputs.end(), i) == subtract_fee_outputs.end()) {
                    subtract_fee_outputs.push_back(i);
                }
                break;
            }
        }
    }

    if (subtract_fee_outputs.empty()) {
        bool has_subtract_fee_recipient{false};

        for (const CRecipient& recipient : recipients) {
            if (recipient.fSubtractFeeFromAmount) {
                has_subtract_fee_recipient = true;
                break;
            }
        }

        /*
         * Fallback for send-available-balance style transactions.
         *
         * If the user explicitly enabled subtract-fee-from-amount and this
         * transaction has no change output, then every output is intentional
         * recipient payment. In that case, allow the fee deficit to be taken
         * from recipient outputs even if script matching failed above.
         */
        if (has_subtract_fee_recipient && !change_pos) {
            for (unsigned int i = 0; i < tx.vout.size(); ++i) {
                subtract_fee_outputs.push_back(i);
            }
        }
    }

    if (subtract_fee_outputs.empty()) {
        if (change_pos) {
            return util::Error{_("Kvanta5 transaction fee deficit exceeds usable change and no recipient output could be reduced for subtract-fee-from-amount.")};
        }

        return util::Error{_("Kvanta5 transaction requires additional fee but has no change output and no subtract-fee-from-amount recipient output could be reduced.")};
    }

    CAmount remaining_deficit = deficit;

    for (unsigned int output_index : subtract_fee_outputs) {
        if (output_index >= tx.vout.size()) {
            continue;
        }

        CTxOut& txout = tx.vout[output_index];

        if (txout.nValue <= 0) {
            continue;
        }

        /*
         * Try taking the remaining deficit from this recipient. For the common
         * send-available-balance case, this succeeds immediately.
         */
        if (txout.nValue > remaining_deficit) {
            CTxOut reduced(txout.nValue - remaining_deficit, txout.scriptPubKey);
            if (!IsDust(reduced, dust_relay_fee)) {
                txout.nValue -= remaining_deficit;
                return {};
            }
        }

        /*
         * If taking the full remaining deficit would make this output dust,
         * take as much as possible while leaving it non-dust.
         */
        CAmount best_reduction{0};

        for (CAmount candidate = txout.nValue - 1; candidate > 0; --candidate) {
            CTxOut reduced(txout.nValue - candidate, txout.scriptPubKey);
            if (!IsDust(reduced, dust_relay_fee)) {
                best_reduction = candidate;
                break;
            }

            if (candidate > 100000) {
                candidate -= 99999;
            }
        }

        if (best_reduction <= 0) {
            continue;
        }

        const CAmount actual_reduction = std::min(best_reduction, remaining_deficit);
        txout.nValue -= actual_reduction;
        remaining_deficit -= actual_reduction;

        if (remaining_deficit <= 0) {
            return {};
        }
    }

    return util::Error{_("Kvanta5 transaction fee deficit exceeds usable change and subtract-fee-from-amount recipient output(s).")};
}

static util::Result<CreatedTransactionResult> CreateTransactionInternal(
        CWallet& wallet,
        const std::vector<CRecipient>& vecSend,
        std::optional<unsigned int> change_pos,
        const CCoinControl& coin_control,
        bool sign) EXCLUSIVE_LOCKS_REQUIRED(wallet.cs_wallet)
{
    AssertLockHeld(wallet.cs_wallet);

    FastRandomContext rng_fast;
    CMutableTransaction txNew; // The resulting transaction that we make

    if (coin_control.m_version) {
        txNew.version = coin_control.m_version.value();
    }

    CoinSelectionParams coin_selection_params{rng_fast}; // Parameters for coin selection, init with dummy
    coin_selection_params.m_avoid_partial_spends = coin_control.m_avoid_partial_spends;
    coin_selection_params.m_include_unsafe_inputs = coin_control.m_include_unsafe_inputs;
    /*
     * Kvanta5 P2QR / P2SH-wrapped-P2QR spends may legitimately be much larger
     * than Bitcoin's inherited standard transaction weight. Use the P2QR policy
     * ceiling for wallet construction so institutional fanout/consolidation
     * transactions can fill a full Kvanta5 block when needed.
     *
     * Non-P2QR oversized transactions are still rejected later by mempool policy,
     * which has prevout context and enforces the actual P2QR-only exception.
     */
     coin_selection_params.m_max_tx_weight = coin_control.m_max_tx_weight.value_or(MAX_STANDARD_P2QR_TX_WEIGHT);

     int minimum_tx_weight = MIN_STANDARD_TX_NONWITNESS_SIZE * WITNESS_SCALE_FACTOR;
     if (coin_selection_params.m_max_tx_weight.value() < minimum_tx_weight ||
         coin_selection_params.m_max_tx_weight.value() > MAX_STANDARD_P2QR_TX_WEIGHT) {
         return util::Error{strprintf(
            _("Maximum transaction weight must be between %d and %d"),
            minimum_tx_weight,
            MAX_STANDARD_P2QR_TX_WEIGHT)};
    }

    // Set the long term feerate estimate to the wallet's consolidate feerate.
    coin_selection_params.m_long_term_feerate = wallet.m_consolidate_feerate;

    // Static vsize overhead + outputs vsize.
    // 4 nVersion, 4 nLocktime, 1 input count, 1 witness overhead (dummy, flag, stack size)
    coin_selection_params.tx_noinputs_size = 10 + GetSizeOfCompactSize(vecSend.size()); // bytes for output count

    CAmount recipients_sum = 0;

    // Kvanta5 change policy is P2QR-aware and must not fall back to Bitcoin Core's
    // normal OutputType / ReserveDestination machinery.
    const auto kv5_change_kind = GetKvanta5ChangeKindForTransaction(wallet, vecSend, coin_control);
    if (!kv5_change_kind) {
        return util::Error{util::ErrorString(kv5_change_kind)};
    }

    unsigned int outputs_to_subtract_fee_from = 0; // The number of outputs which we are subtracting the fee from

    for (const auto& recipient : vecSend) {
        if (IsDust(recipient, wallet.chain().relayDustFee())) {
            return util::Error{_("Transaction amount too small")};
        }

        // Include the fee cost for outputs.
        coin_selection_params.tx_noinputs_size += GetSerializeSizeForRecipient(recipient);
        recipients_sum += recipient.nAmount;

        if (recipient.fSubtractFeeFromAmount) {
            outputs_to_subtract_fee_from++;
            coin_selection_params.m_subtract_fee_outputs = true;
        }
    }

    // Create change script that will be used if we need change.
    CScript scriptChange;
    bilingual_str error; // possible error str

    // Coin control: send change to custom address.
    if (!std::get_if<CNoDestination>(&coin_control.destChange)) {
        scriptChange = GetScriptForDestination(coin_control.destChange);

        const Kvanta5ChangeKind explicit_change_kind = GetKvanta5ChangeKindForScript(wallet, scriptChange);
        if (explicit_change_kind == Kvanta5ChangeKind::NONE) {
            return util::Error{_("Kvanta5 change address must be wallet-owned native P2QR or wallet-owned compatibility-wrapper P2QR.")};
        }

        if (explicit_change_kind != *kv5_change_kind) {
            return util::Error{_("Kvanta5 change address type must match transaction type: native P2QR uses native P2QR change; compatibility-wrapper P2QR uses compatibility-wrapper change.")};
        }
    } else {
        if (*kv5_change_kind == Kvanta5ChangeKind::NATIVE_P2QR) {
            auto op_script = wallet.GetNewKvanta5P2QRChangeScript();
            if (!op_script) {
                error = _("Transaction needs a native P2QR change address, but we can't generate it.") + Untranslated(" ") + util::ErrorString(op_script);
            } else {
                scriptChange = *op_script;
            }
        } else if (*kv5_change_kind == Kvanta5ChangeKind::COMPAT_P2QR) {
            auto op_script = wallet.GetNewKvanta5P2QRCompatibilityChangeScript();
            if (!op_script) {
                error = _("Transaction needs a compatibility-wrapper P2QR change address, but we can't generate it.") + Untranslated(" ") + util::ErrorString(op_script);
            } else {
                scriptChange = *op_script;
            }
        } else {
            return util::Error{_("Kvanta5 wallet refuses to create non-P2QR change. Use a native P2QR or compatibility-wrapper P2QR recipient.")};
        }
    }

    CTxOut change_prototype_txout(0, scriptChange);
    coin_selection_params.change_output_size = GetSerializeSize(change_prototype_txout);

    // Get size of spending the change output.
    int change_spend_size = CalculateMaximumSignedInputSize(change_prototype_txout, &wallet, /*coin_control=*/nullptr);

    // If the wallet doesn't know how to sign change output, assume p2sh-p2wpkh
    // as lower-bound to allow BnB to do its thing.
    if (change_spend_size == -1) {
        coin_selection_params.change_spend_size = DUMMY_NESTED_P2WPKH_INPUT_SIZE;
    } else {
        coin_selection_params.change_spend_size = change_spend_size;
    }

    // Set discard feerate.
    coin_selection_params.m_discard_feerate = GetDiscardRate(wallet);

    // Get the fee rate to use effective values in coin selection.
    FeeCalculation feeCalc;
    coin_selection_params.m_effective_feerate = GetMinimumFeeRate(wallet, coin_control, &feeCalc);

    // Do not, ever, assume that it's fine to change the fee rate if the user has explicitly provided one.
    if (coin_control.m_feerate && coin_selection_params.m_effective_feerate > *coin_control.m_feerate) {
        return util::Error{strprintf(_("Fee rate (%s) is lower than the minimum fee rate setting (%s)"), coin_control.m_feerate->ToString(FeeEstimateMode::QUARK_VB), coin_selection_params.m_effective_feerate.ToString(FeeEstimateMode::QUARK_VB))};
    }

    if (feeCalc.reason == FeeReason::FALLBACK && !wallet.m_allow_fallback_fee) {
        return util::Error{strprintf(_("Fee estimation failed. Fallbackfee is disabled. Wait a few blocks or enable %s."), "-fallbackfee")};
    }

    // Calculate the cost of change.
    // Cost of change is the cost of creating the change output + cost of spending the change output in the future.
    coin_selection_params.m_change_fee = coin_selection_params.m_effective_feerate.GetFee(coin_selection_params.change_output_size);
    coin_selection_params.m_cost_of_change = coin_selection_params.m_discard_feerate.GetFee(coin_selection_params.change_spend_size) + coin_selection_params.m_change_fee;

    coin_selection_params.m_min_change_target = GenerateChangeTarget(std::floor(recipients_sum / vecSend.size()), coin_selection_params.m_change_fee, rng_fast);

    // The smallest change amount should be:
    // 1. at least equal to dust threshold
    // 2. at least 1 quark greater than fees to spend it at m_discard_feerate
    const auto dust = GetDustThreshold(change_prototype_txout, coin_selection_params.m_discard_feerate);
    const auto change_spend_fee = coin_selection_params.m_discard_feerate.GetFee(coin_selection_params.change_spend_size);
    coin_selection_params.min_viable_change = std::max(change_spend_fee + 1, dust);

    // Include the fees for things that aren't inputs, excluding the change output.
    const CAmount not_input_fees = coin_selection_params.m_effective_feerate.GetFee(coin_selection_params.m_subtract_fee_outputs ? 0 : coin_selection_params.tx_noinputs_size);
    CAmount selection_target = recipients_sum + not_input_fees;

    // This can only happen if feerate is 0, requested destinations are value 0,
    // and no pre-selected inputs. This would result in a consensus-invalid 0-input tx.
    if (selection_target == 0 && !coin_control.HasSelected()) {
        return util::Error{_("Transaction requires one destination of non-0 value, a non-0 feerate, or a pre-selected input")};
    }

    // Fetch manually selected coins.
    PreSelectedInputs preset_inputs;
    if (coin_control.HasSelected()) {
        auto res_fetch_inputs = FetchSelectedInputs(wallet, coin_control, coin_selection_params);
        if (!res_fetch_inputs) return util::Error{util::ErrorString(res_fetch_inputs)};
        preset_inputs = *res_fetch_inputs;
    }

    // Fetch wallet available coins if "other inputs" are allowed.
    CoinsResult available_coins;
    if (coin_control.m_allow_other_inputs) {
        available_coins = AvailableCoins(wallet, &coin_control, coin_selection_params.m_effective_feerate);
    }

    // Choose coins to use.
    auto select_coins_res = SelectCoins(wallet, available_coins, preset_inputs, /*nTargetValue=*/selection_target, coin_control, coin_selection_params);
    if (!select_coins_res) {
        const bilingual_str& err = util::ErrorString(select_coins_res);
        return util::Error{err.empty() ? _("Insufficient funds") : err};
    }

    const SelectionResult& result = *select_coins_res;
    TRACEPOINT(coin_selection, selected_coins,
           wallet.GetName().c_str(),
           GetAlgorithmName(result.GetAlgo()).c_str(),
           result.GetTarget(),
           result.GetWaste(),
           result.GetSelectedValue());

    // Vouts to the payees.
    txNew.vout.reserve(vecSend.size() + 1); // + 1 because of possible later insert
    for (const auto& recipient : vecSend) {
        txNew.vout.emplace_back(recipient.nAmount, GetScriptForDestination(recipient.dest));
    }

    const CAmount change_amount = result.GetChange(coin_selection_params.min_viable_change, coin_selection_params.m_change_fee);
    if (change_amount > 0) {
	    /*
	     * Kvanta5 automatic change policy:
	     *
	     * Native P2QR inputs produce native P2QR change.
	     * Wrapped P2SH/P2QR inputs produce wrapped P2SH/P2QR change.
	     * Mixed native+wrapped input spends produce mixed change outputs.
	     *
	     * Explicit Coin Control change is handled earlier through scriptChange.
	     */
	    if (!std::get_if<CNoDestination>(&coin_control.destChange)) {
	        CTxOut newTxOut(change_amount, scriptChange);

	        if (!change_pos) {
	            change_pos = rng_fast.randrange(txNew.vout.size() + 1);
	        } else if ((unsigned int)*change_pos > txNew.vout.size()) {
	            return util::Error{_("Transaction change output index out of range")};
	        }

	        txNew.vout.insert(txNew.vout.begin() + *change_pos, newTxOut);
	    } else {
	        const auto domains_res = GetKvanta5SelectedInputDomains(wallet, result);
	        if (!domains_res) {
	            return util::Error{util::ErrorString(domains_res)};
	        }

	        const Kvanta5SelectedInputDomains& domains = *domains_res;

	        std::vector<CTxOut> kv5_change_outputs;

	        if (domains.has_native && !domains.has_compat) {
	            auto op_script = wallet.GetNewKvanta5P2QRChangeScript();
	            if (!op_script) {
	                return util::Error{_("Transaction needs a native P2QR change address, but we can't generate it.") + Untranslated(" ") + util::ErrorString(op_script)};
	            }

	            kv5_change_outputs.emplace_back(change_amount, *op_script);
	        } else if (domains.has_compat && !domains.has_native) {
	            auto op_script = wallet.GetNewKvanta5P2QRCompatibilityChangeScript();
	            if (!op_script) {
	                return util::Error{_("Transaction needs a wrapped P2SH P2QR change address, but we can't generate it.") + Untranslated(" ") + util::ErrorString(op_script)};
	            }

	            kv5_change_outputs.emplace_back(change_amount, *op_script);
	        } else {
	            /*
	             * Mixed native+wrapped spend.
	             *
	             * Split change proportionally by selected input value. This preserves
	             * both address domains without forcing all change native or all change
	             * wrapped.
	             *
	             * Later this can be made more exact by doing domain-aware coin
	             * selection and recipient funding. This is the safe first pass:
	             * mixed spend creates mixed change.
	             */
	            const CAmount selected_value = result.GetSelectedValue();
                const CAmount domain_total = domains.Total();

                if (selected_value <= 0) {
                    return util::Error{_("Kvanta5 mixed-input change split failed: selected value is zero.")};
                }

                if (domain_total != selected_value) {
                    return util::Error{Untranslated(strprintf(
                        "Kvanta5 mixed-input change split failed: selected-domain accounting mismatch: "
                        "native=%s compat=%s domain_total=%s selected_value=%s",
                        FormatMoney(domains.native_value),
                        FormatMoney(domains.compat_value),
                        FormatMoney(domain_total),
                        FormatMoney(selected_value)))};
                 }

                 auto native_change_res = Kvanta5ProportionalAmount(change_amount, domains.native_value, selected_value);
                 if (!native_change_res) {
                     return util::Error{util::ErrorString(native_change_res)};
                 }

                 CAmount native_change = *native_change_res;
                 CAmount compat_change = change_amount - native_change;

                 if (native_change < 0 || compat_change < 0 || native_change + compat_change != change_amount) {
                     return util::Error{Untranslated(strprintf(
                        "Kvanta5 mixed-input change split failed: invalid split: "
                        "native_change=%s compat_change=%s change_amount=%s "
                        "native_selected=%s compat_selected=%s selected_value=%s",
                        FormatMoney(native_change),
                        FormatMoney(compat_change),
                        FormatMoney(change_amount),
                        FormatMoney(domains.native_value),
                        FormatMoney(domains.compat_value),
                        FormatMoney(selected_value)))};
                 }

	            /*
	             * Avoid creating dust/miniscule change outputs. Tiny fragments are
	             * left as additional fee rather than crossing domains.
	             */
	            if (native_change > 0 && native_change >= coin_selection_params.min_viable_change) {
	                auto op_native_script = wallet.GetNewKvanta5P2QRChangeScript();
	                if (!op_native_script) {
	                    return util::Error{_("Transaction needs a native P2QR change address, but we can't generate it.") + Untranslated(" ") + util::ErrorString(op_native_script)};
	                }

	                kv5_change_outputs.emplace_back(native_change, *op_native_script);
	            }

	            if (compat_change > 0 && compat_change >= coin_selection_params.min_viable_change) {
	                auto op_compat_script = wallet.GetNewKvanta5P2QRCompatibilityChangeScript();
	                if (!op_compat_script) {
	                    return util::Error{_("Transaction needs a wrapped P2SH P2QR change address, but we can't generate it.") + Untranslated(" ") + util::ErrorString(op_compat_script)};
	                }

	                kv5_change_outputs.emplace_back(compat_change, *op_compat_script);
	            }

	            if (kv5_change_outputs.empty()) {
	                change_pos = std::nullopt;
	            }
	        }

	        if (!kv5_change_outputs.empty()) {
 	           if (!change_pos) {
	                change_pos = rng_fast.randrange(txNew.vout.size() + 1);
	            } else if ((unsigned int)*change_pos > txNew.vout.size()) {
	                return util::Error{_("Transaction change output index out of range")};
	            }

	            unsigned int insert_pos = *change_pos;
	            for (const CTxOut& change_txout : kv5_change_outputs) {
	                txNew.vout.insert(txNew.vout.begin() + insert_pos, change_txout);
	                ++insert_pos;
	            }
	        }
	    }
	} else {
	    change_pos = std::nullopt;
	}

    // Shuffle selected coins and fill in final vin.
    std::vector<std::shared_ptr<COutput>> selected_coins = result.GetShuffledInputVector();

    if (coin_control.HasSelected() && coin_control.HasSelectedOrder()) {
        // When there are preselected inputs, put them first and preserve their selected order.
        std::stable_sort(selected_coins.begin(), selected_coins.end(),
            [&coin_control](const std::shared_ptr<COutput>& a, const std::shared_ptr<COutput>& b) {
                auto a_pos = coin_control.GetSelectionPos(a->outpoint);
                auto b_pos = coin_control.GetSelectionPos(b->outpoint);
                if (a_pos.has_value() && b_pos.has_value()) {
                    return a_pos.value() < b_pos.value();
                } else if (a_pos.has_value() && !b_pos.has_value()) {
                    return true;
                } else {
                    return false;
                }
            });
    }

    // The sequence number is set to non-maxint so that DiscourageFeeSniping works.
    bool use_anti_fee_sniping = true;
    const uint32_t default_sequence{coin_control.m_signal_bip125_rbf.value_or(wallet.m_signal_rbf) ? MAX_BIP125_RBF_SEQUENCE : CTxIn::MAX_SEQUENCE_NONFINAL};

    txNew.vin.reserve(selected_coins.size());
    for (const auto& coin : selected_coins) {
        std::optional<uint32_t> sequence = coin_control.GetSequence(coin->outpoint);
        if (sequence) {
            // If an input has a preset sequence, we can't do anti-fee-sniping.
            use_anti_fee_sniping = false;
        }

        txNew.vin.emplace_back(coin->outpoint, CScript{}, sequence.value_or(default_sequence));

        auto scripts = coin_control.GetScripts(coin->outpoint);
        if (scripts.first) {
            txNew.vin.back().scriptSig = *scripts.first;
        }
        if (scripts.second) {
            txNew.vin.back().scriptWitness = *scripts.second;
        }
    }

    if (coin_control.m_locktime) {
        txNew.nLockTime = coin_control.m_locktime.value();
        use_anti_fee_sniping = false;
    }

    if (use_anti_fee_sniping) {
        DiscourageFeeSniping(txNew, rng_fast, wallet.chain(), wallet.GetLastBlockHash(), wallet.GetLastBlockHeight());
    }

    // Calculate the transaction fee.
    TxSize tx_sizes = CalculateMaximumSignedTxSize(CTransaction(txNew), &wallet, &coin_control);
    int nBytes = tx_sizes.vsize;
    if (nBytes == -1) {
        return util::Error{_("Missing solving data for estimating transaction size")};
    }

    CAmount fee_needed = coin_selection_params.m_effective_feerate.GetFee(nBytes) + result.GetTotalBumpFees();
    const CAmount output_value = CalculateOutputValue(txNew);
    Assume(recipients_sum + change_amount == output_value);
    CAmount current_fee = result.GetSelectedValue() - output_value;

    // Sanity check that the fee cannot be negative.
    if (current_fee < 0) {
    return util::Error{Untranslated(strprintf(
        "Kvanta5 fee invariant failed: selected=%s output_value=%s recipients_sum=%s "
        "change_amount=%s current_fee=%s txouts=%u",
        FormatMoney(result.GetSelectedValue()),
        FormatMoney(output_value),
        FormatMoney(recipients_sum),
        FormatMoney(change_amount),
        FormatMoney(current_fee),
        txNew.vout.size()))};
    }

    // If there is a change output and we overpay the fees, increase the change to match the fee needed.
    if (change_pos && fee_needed < current_fee) {
        auto& change = txNew.vout.at(*change_pos);
        change.nValue += current_fee - fee_needed;
        current_fee = result.GetSelectedValue() - CalculateOutputValue(txNew);
        if (fee_needed != current_fee) {
            return util::Error{Untranslated(STR_INTERNAL_BUG("Change adjustment: Fee needed != fee paid"))};
        }
    }

    // Reduce output values for subtractFeeFromAmount.
    if (coin_selection_params.m_subtract_fee_outputs) {
        CAmount to_reduce = fee_needed - current_fee;
        unsigned int i = 0;
        bool fFirst = true;

        for (const auto& recipient : vecSend) {
            if (change_pos && i == *change_pos) {
                ++i;
            }

            CTxOut& txout = txNew.vout[i];

            if (recipient.fSubtractFeeFromAmount) {
                txout.nValue -= to_reduce / outputs_to_subtract_fee_from;

                if (fFirst) {
                    fFirst = false;
                    txout.nValue -= to_reduce % outputs_to_subtract_fee_from;
                }

                // Error if this output is reduced to be below dust.
                if (IsDust(txout, wallet.chain().relayDustFee())) {
                    if (txout.nValue < 0) {
                        return util::Error{_("The transaction amount is too small to pay the fee")};
                    } else {
                        return util::Error{_("The transaction amount is too small to send after the fee has been deducted")};
                    }
                }
            }

            ++i;
        }

        current_fee = result.GetSelectedValue() - CalculateOutputValue(txNew);
        if (fee_needed != current_fee) {
            return util::Error{Untranslated(STR_INTERNAL_BUG("SFFO: Fee needed != fee paid"))};
        }
    }

    // fee_needed should now always be less than or equal to the current fees that we pay.
    if (fee_needed > current_fee) {
        return util::Error{Untranslated(STR_INTERNAL_BUG("Fee needed > fee paid"))};
    }

    // Give up if change generation failed and change is required.
    if (scriptChange.empty() && change_pos) {
        return util::Error{error};
    }

    // Final Kvanta5 wallet-created-output guard.
    // This catches any accidental SegWit/Taproot/legacy fallback before signing/broadcast.
    auto kv5_output_check = CheckKvanta5WalletCreatedOutputs(wallet, txNew);
    if (!kv5_output_check) {
        return util::Error{util::ErrorString(kv5_output_check)};
    }

    if (sign && !wallet.SignTransaction(txNew)) {
    return util::Error{_("Signing transaction failed")};
	}

	/*
	 * Kvanta5 final signed-transaction fee gate.
	 *
	 * The earlier fee_needed calculation is still useful for coin selection, but it
	 * is only an estimate. QR spends must be checked after signing because the real
	 * ML-DSA/P2QR scriptSig payload exists only after wallet.SignTransaction().
	 *
	 * Preserve Bitcoin Core smart-fee behavior:
	 *   - user fee rate
	 *   - smart fee estimates
	 *   - fallback fee
	 *   - mempool minimum
	 *
	 * But apply that selected feerate to the same P2QR-adjusted final signed vsize
	 * used by mempool/block policy.
	 */
	if (sign) {
	    auto adjusted_vsize_res = GetKvanta5WalletAdjustedVSize(wallet, CTransaction(txNew), selected_coins);
	    if (!adjusted_vsize_res) {
	        return util::Error{util::ErrorString(adjusted_vsize_res)};
	    }

	    int64_t adjusted_vsize = *adjusted_vsize_res;
	    CAmount adjusted_fee_needed = coin_selection_params.m_effective_feerate.GetFee(adjusted_vsize) + result.GetTotalBumpFees();
	    CAmount adjusted_current_fee = result.GetSelectedValue() - CalculateOutputValue(txNew);

	    if (adjusted_current_fee < 0) {
	        return util::Error{Untranslated(STR_INTERNAL_BUG("Kvanta5 final fee paid < 0"))};
	    }

	    if (adjusted_current_fee < adjusted_fee_needed) {
	        const CAmount deficit = adjusted_fee_needed - adjusted_current_fee;

	        auto reduce_res = ReduceKvanta5ChangeForFeeDeficit(
               txNew,
               change_pos,
               deficit,
               wallet.chain().relayDustFee(),
               vecSend
            );

	        if (!reduce_res) {
	            return util::Error{util::ErrorString(reduce_res)};
	        }

	        /*
	         * Changing an output invalidates all signatures. Clear and re-sign.
	         */
	        ClearKvanta5MutableTxSignatures(txNew);

	        if (!wallet.SignTransaction(txNew)) {
	            return util::Error{_("Signing transaction failed after Kvanta5 final fee adjustment")};
	        }

	        auto adjusted_vsize_res_2 = GetKvanta5WalletAdjustedVSize(wallet, CTransaction(txNew), selected_coins);
	        if (!adjusted_vsize_res_2) {
	            return util::Error{util::ErrorString(adjusted_vsize_res_2)};
	        }

	        adjusted_vsize = *adjusted_vsize_res_2;
	        adjusted_fee_needed = coin_selection_params.m_effective_feerate.GetFee(adjusted_vsize) + result.GetTotalBumpFees();
	        adjusted_current_fee = result.GetSelectedValue() - CalculateOutputValue(txNew);

	        if (adjusted_current_fee < adjusted_fee_needed) {
	            return util::Error{strprintf(
	                _("Kvanta5 final signed transaction fee is too low for P2QR-adjusted relay policy after fee adjustment: fee %s, required %s, adjusted vsize %d. Refusing to create an unbroadcast transaction."),
	                FormatMoney(adjusted_current_fee),
	                FormatMoney(adjusted_fee_needed),
	                adjusted_vsize)};
	        }
	    }

	    current_fee = adjusted_current_fee;
	    fee_needed = adjusted_fee_needed;
	    nBytes = adjusted_vsize;
	}

	// Return the constructed transaction data.
	CTransactionRef tx = MakeTransactionRef(std::move(txNew));

    // Limit size.
    //
    // Normal Bitcoin-style transactions are still restricted by mempool policy,
    // but wallet construction must allow Kvanta5 P2QR / P2SH-wrapped-P2QR
    // institutional fanout and consolidation transactions up to the P2QR policy
    // ceiling. The mempool prevout-aware checks decide whether the oversized
    // exception is actually allowed.
    const int64_t tx_weight = sign ? GetTransactionWeight(*tx) : tx_sizes.weight;

    if (tx_weight > MAX_STANDARD_P2QR_TX_WEIGHT) {
        return util::Error{_("Transaction too large")};
    }

    if (current_fee > wallet.m_default_max_tx_fee) {
        return util::Error{TransactionErrorString(TransactionError::MAX_FEE_EXCEEDED)};
    }

    if (gArgs.GetBoolArg("-walletrejectlongchains", DEFAULT_WALLET_REJECT_LONG_CHAINS)) {
        // Lastly, ensure this tx will pass the mempool's chain limits.
        auto result = wallet.chain().checkChainLimits(tx);
        if (!result) {
            return util::Error{util::ErrorString(result)};
        }
    }

    wallet.WalletLogPrintf("Coin Selection: Algorithm:%s, Waste Metric Score:%d\n", GetAlgorithmName(result.GetAlgo()), result.GetWaste());
    wallet.WalletLogPrintf("Fee Calculation: Fee:%d Bytes:%u Tgt:%d (requested %d) Reason:\"%s\" Decay %.5f: Estimation: (%g - %g) %.2f%% %.1f/(%.1f %d mem %.1f out) Fail: (%g - %g) %.2f%% %.1f/(%.1f %d mem %.1f out)\n",
              current_fee, nBytes, feeCalc.returnedTarget, feeCalc.desiredTarget, StringForFeeReason(feeCalc.reason), feeCalc.est.decay,
              feeCalc.est.pass.start, feeCalc.est.pass.end,
              (feeCalc.est.pass.totalConfirmed + feeCalc.est.pass.inMempool + feeCalc.est.pass.leftMempool) > 0.0 ? 100 * feeCalc.est.pass.withinTarget / (feeCalc.est.pass.totalConfirmed + feeCalc.est.pass.inMempool + feeCalc.est.pass.leftMempool) : 0.0,
              feeCalc.est.pass.withinTarget, feeCalc.est.pass.totalConfirmed, feeCalc.est.pass.inMempool, feeCalc.est.pass.leftMempool,
              feeCalc.est.fail.start, feeCalc.est.fail.end,
              (feeCalc.est.fail.totalConfirmed + feeCalc.est.fail.inMempool + feeCalc.est.fail.leftMempool) > 0.0 ? 100 * feeCalc.est.fail.withinTarget / (feeCalc.est.fail.totalConfirmed + feeCalc.est.fail.inMempool + feeCalc.est.fail.leftMempool) : 0.0,
              feeCalc.est.fail.withinTarget, feeCalc.est.fail.totalConfirmed, feeCalc.est.fail.inMempool, feeCalc.est.fail.leftMempool);

    return CreatedTransactionResult(tx, current_fee, change_pos, feeCalc);
}


util::Result<CreatedTransactionResult> CreateTransaction(
        CWallet& wallet,
        const std::vector<CRecipient>& vecSend,
        std::optional<unsigned int> change_pos,
        const CCoinControl& coin_control,
        bool sign)
{
    if (vecSend.empty()) {
        return util::Error{_("Transaction must have at least one recipient")};
    }

    if (std::any_of(vecSend.cbegin(), vecSend.cend(), [](const auto& recipient){ return recipient.nAmount < 0; })) {
        return util::Error{_("Transaction amounts must not be negative")};
    }

    LOCK(wallet.cs_wallet);

    auto res = CreateTransactionInternal(wallet, vecSend, change_pos, coin_control, sign);
    TRACEPOINT(coin_selection, normal_create_tx_internal,
           wallet.GetName().c_str(),
           bool(res),
           res ? res->fee : 0,
           res && res->change_pos.has_value() ? int32_t(*res->change_pos) : -1);
    if (!res) return res;
    const auto& txr_ungrouped = *res;
    // try with avoidpartialspends unless it's enabled already
    if (txr_ungrouped.fee > 0 /* 0 means non-functional fee rate estimation */ && wallet.m_max_aps_fee > -1 && !coin_control.m_avoid_partial_spends) {
        TRACEPOINT(coin_selection, attempting_aps_create_tx, wallet.GetName().c_str());
        CCoinControl tmp_cc = coin_control;
        tmp_cc.m_avoid_partial_spends = true;

        // Reuse the change destination from the first creation attempt to avoid skipping BIP44 indexes
        if (txr_ungrouped.change_pos) {
            ExtractDestination(txr_ungrouped.tx->vout[*txr_ungrouped.change_pos].scriptPubKey, tmp_cc.destChange);
        }

        auto txr_grouped = CreateTransactionInternal(wallet, vecSend, change_pos, tmp_cc, sign);
        // if fee of this alternative one is within the range of the max fee, we use this one
        const bool use_aps{txr_grouped.has_value() ? (txr_grouped->fee <= txr_ungrouped.fee + wallet.m_max_aps_fee) : false};
        TRACEPOINT(coin_selection, aps_create_tx_internal,
               wallet.GetName().c_str(),
               use_aps,
               txr_grouped.has_value(),
               txr_grouped.has_value() ? txr_grouped->fee : 0,
               txr_grouped.has_value() && txr_grouped->change_pos.has_value() ? int32_t(*txr_grouped->change_pos) : -1);
        if (txr_grouped) {
            wallet.WalletLogPrintf("Fee non-grouped = %lld, grouped = %lld, using %s\n",
                txr_ungrouped.fee, txr_grouped->fee, use_aps ? "grouped" : "non-grouped");
            if (use_aps) return txr_grouped;
        }
    }
    return res;
}

util::Result<CreatedTransactionResult> FundTransaction(CWallet& wallet, const CMutableTransaction& tx, const std::vector<CRecipient>& vecSend, std::optional<unsigned int> change_pos, bool lockUnspents, CCoinControl coinControl)
{
    // We want to make sure tx.vout is not used now that we are passing outputs as a vector of recipients.
    // This sets us up to remove tx completely in a future PR in favor of passing the inputs directly.
    assert(tx.vout.empty());

    // Set the user desired locktime
    coinControl.m_locktime = tx.nLockTime;

    // Set the user desired version
    coinControl.m_version = tx.version;

    // Acquire the locks to prevent races to the new locked unspents between the
    // CreateTransaction call and LockCoin calls (when lockUnspents is true).
    LOCK(wallet.cs_wallet);

    // Fetch specified UTXOs from the UTXO set to get the scriptPubKeys and values of the outputs being selected
    // and to match with the given solving_data. Only used for non-wallet outputs.
    std::map<COutPoint, Coin> coins;
    for (const CTxIn& txin : tx.vin) {
        coins[txin.prevout]; // Create empty map entry keyed by prevout.
    }
    wallet.chain().findCoins(coins);

    for (const CTxIn& txin : tx.vin) {
        const auto& outPoint = txin.prevout;
        PreselectedInput& preset_txin = coinControl.Select(outPoint);
        if (!wallet.IsMine(outPoint)) {
            if (coins[outPoint].out.IsNull()) {
                return util::Error{_("Unable to find UTXO for external input")};
            }

            // The input was not in the wallet, but is in the UTXO set, so select as external
            preset_txin.SetTxOut(coins[outPoint].out);
        }
        preset_txin.SetSequence(txin.nSequence);
        preset_txin.SetScriptSig(txin.scriptSig);
        preset_txin.SetScriptWitness(txin.scriptWitness);
    }

    auto res = CreateTransaction(wallet, vecSend, change_pos, coinControl, false);
    if (!res) {
        return res;
    }

    if (lockUnspents) {
        for (const CTxIn& txin : res->tx->vin) {
            wallet.LockCoin(txin.prevout);
        }
    }

    return res;
}
} // namespace wallet
