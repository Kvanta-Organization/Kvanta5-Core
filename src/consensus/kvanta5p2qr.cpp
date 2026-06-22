#include <consensus/kvanta5p2qr.h>

#include <crypto/pq/mldsa87/mldsa87.h>
#include <hash.h>
#include <script/kvanta5_p2qr.h>
#include <script/solver.h>
#include <span.h>
#include <tinyformat.h>

#include <algorithm>
#include <limits>

namespace consensus::kvanta5p2qr {
namespace {

static const HashWriter HASHER_Kvanta5P2QR_SIGHASH{TaggedHash("Kvanta5P2QRSighash")};

static constexpr size_t KVANTA5_P2QR_SINGLE_PUSHES = 2;
static constexpr size_t KVANTA5_P2QR_SINGLE_P2SH_PUSHES = 3;

static constexpr size_t KVANTA5_P2QR_MULTISIG_MAX_PUSHES =
    KVANTA5_P2QR_MULTISIG_MAX_SIGNATURES +
    KVANTA5_P2QR_MULTISIG_MAX_SIGNATURES +
    KVANTA5_P2QR_MULTISIG_MAX_KEYS +
    2;

static constexpr size_t KVANTA5_P2QR_MULTISIG_P2SH_MAX_PUSHES =
    KVANTA5_P2QR_MULTISIG_MAX_PUSHES + 1;

std::vector<unsigned char> ToByteVector(const uint256& value)
{
    return std::vector<unsigned char>(value.begin(), value.end());
}

uint256 HashPrevouts(const CTransaction& tx)
{
    HashWriter ss{};
    for (const CTxIn& txin : tx.vin) {
        ss << txin.prevout;
    }
    return ss.GetSHA256();
}

uint256 HashSequences(const CTransaction& tx)
{
    HashWriter ss{};
    for (const CTxIn& txin : tx.vin) {
        ss << txin.nSequence;
    }
    return ss.GetSHA256();
}

uint256 HashOutputs(const CTransaction& tx)
{
    HashWriter ss{};
    for (const CTxOut& txout : tx.vout) {
        ss << txout;
    }
    return ss.GetSHA256();
}

uint256 HashSpentAmounts(const std::vector<CTxOut>& spent_outputs)
{
    HashWriter ss{};
    for (const CTxOut& txout : spent_outputs) {
        ss << txout.nValue;
    }
    return ss.GetSHA256();
}

uint256 HashSpentScripts(const std::vector<CTxOut>& spent_outputs)
{
    HashWriter ss{};
    for (const CTxOut& txout : spent_outputs) {
        ss << txout.scriptPubKey;
    }
    return ss.GetSHA256();
}

bool ReadPushesOnly(
    const CScript& script,
    std::vector<std::vector<unsigned char>>& pushes_out,
    std::string& error_out,
    const size_t max_pushes)
{
    pushes_out.clear();

    std::string push_error;

    if (!Kvanta5ReadCanonicalP2QRPushes(script, pushes_out, &push_error, max_pushes)) {
        error_out = strprintf("KVANTA5_P2QR scriptSig push format invalid: %s", push_error);
        return false;
    }

    return true;
}

uint256 SignatureHashInternal(
    const CTransaction& tx,
    uint32_t input_index,
    const CTxOut& spent_output,
    const std::vector<CTxOut>& spent_outputs)
{
    assert(input_index < tx.vin.size());
    assert(spent_outputs.size() == tx.vin.size());

    HashWriter ss{HASHER_Kvanta5P2QR_SIGHASH};

    static constexpr uint8_t KVANTA5_P2QR_SIGHASH_EPOCH = 0;
    static constexpr int32_t KVANTA5_P2QR_HASH_TYPE = 1; // SIGHASH_ALL

    ss << KVANTA5_P2QR_SIGHASH_EPOCH;

    ss << tx.version;
    ss << tx.nLockTime;
    ss << KVANTA5_P2QR_HASH_TYPE;
    ss << input_index;

    ss << HashPrevouts(tx);
    ss << HashSequences(tx);
    ss << HashOutputs(tx);
    ss << HashSpentAmounts(spent_outputs);
    ss << HashSpentScripts(spent_outputs);

    ss << tx.vin[input_index].prevout;
    ss << tx.vin[input_index].nSequence;
    ss << spent_output.nValue;
    ss << spent_output.scriptPubKey;

    return ss.GetSHA256();
}

bool CheckSignatureAndProgram(
    const CTransaction& tx,
    uint32_t input_index,
    const CTxOut& spent_output,
    const std::vector<CTxOut>& spent_outputs,
    const uint256& committed_program,
    const std::vector<unsigned char>& signature,
    const std::vector<unsigned char>& pubkey,
    std::string& error_out)
{
    if (signature.size() != MLDSA87_SIGNATURE_SIZE) {
        error_out = "KVANTA5_P2QR signature has invalid size";
        return false;
    }

    if (pubkey.size() != MLDSA87_PUBLIC_KEY_SIZE) {
        error_out = "KVANTA5_P2QR public key has invalid size";
        return false;
    }

    const uint256 pubkey_hash = Hash(pubkey);
    if (pubkey_hash != committed_program) {
        error_out = "KVANTA5_P2QR public key does not match committed program";
        return false;
    }

    const uint256 sighash = SignatureHashInternal(tx, input_index, spent_output, spent_outputs);

    if (!MLDSA87Verify(
            Span<const unsigned char>(pubkey.data(), pubkey.size()),
            Span<const unsigned char>(sighash.begin(), sighash.size()),
            Span<const unsigned char>(signature.data(), signature.size()))) {
        error_out = "KVANTA5_P2QR ML-DSA-87 signature verification failed";
        return false;
    }

    return true;
}

bool CheckMultisigProgramAndSignatures(
    const CTransaction& tx,
    uint32_t input_index,
    const CTxOut& spent_output,
    const std::vector<CTxOut>& spent_outputs,
    const uint256& committed_program,
    const Kvanta5P2QRMultisigSpend& spend,
    std::string& error_out)
{
    const bool has_legacy_pubkey_policy = !spend.pubkeys.empty();
    const bool has_signer_program_policy = !spend.signer_programs.empty();

    if (has_legacy_pubkey_policy && has_signer_program_policy) {
        error_out = "KVANTA5_P2QR multisig spend mixes legacy pubkey policy and signer address policy";
        return false;
    }

    if (!has_legacy_pubkey_policy && !has_signer_program_policy) {
        error_out = "KVANTA5_P2QR multisig spend has no signer policy";
        return false;
    }

    if (spend.required == 0 || spend.total == 0 || spend.required > spend.total) {
        error_out = "KVANTA5_P2QR multisig invalid threshold";
        return false;
    }

    if (spend.signatures.size() != spend.required ||
        spend.signer_indexes.size() != spend.required) {
        error_out = "KVANTA5_P2QR multisig approval vectors are inconsistent";
        return false;
    }

    const uint256 sighash = SignatureHashInternal(tx, input_index, spent_output, spent_outputs);

    /*
     * Legacy/live-chain multisig format:
     *
     *   <sig[m]> <signer_index[m]> <pubkey[n]> <m> <n>
     *
     * This must remain valid forever so existing historical blocks, including
     * the old-format vault spend at height 13153, continue to validate.
     */
    if (has_legacy_pubkey_policy) {
        if (spend.pubkeys.size() != spend.total) {
            error_out = "KVANTA5_P2QR legacy multisig pubkey count does not match total";
            return false;
        }

        const uint256 expected_program = Kvanta5P2QRMultisigProgram(spend.required, spend.pubkeys);

        if (expected_program != committed_program) {
            error_out = "KVANTA5_P2QR legacy multisig pubkey policy does not match committed program";
            return false;
        }

        for (size_t i = 0; i < spend.signatures.size(); ++i) {
            const uint16_t signer_index = spend.signer_indexes[i];

            if (signer_index >= spend.pubkeys.size()) {
                error_out = "KVANTA5_P2QR legacy multisig signer index out of range";
                return false;
            }

            const std::vector<unsigned char>& pubkey = spend.pubkeys[signer_index];
            const std::vector<unsigned char>& signature = spend.signatures[i];

            if (pubkey.size() != KVANTA5_P2QR_PUBKEY_SIZE) {
                error_out = "KVANTA5_P2QR legacy multisig bad pubkey size";
                return false;
            }

            if (!MLDSA87Verify(
                    Span<const unsigned char>(pubkey.data(), pubkey.size()),
                    Span<const unsigned char>(sighash.begin(), sighash.size()),
                    Span<const unsigned char>(signature.data(), signature.size()))) {
                error_out = "KVANTA5_P2QR legacy multisig ML-DSA-87 signature verification failed";
                return false;
            }
        }

        return true;
    }

    /*
     * New signer-address/program multisig format:
     *
     *   <sig[m]> <signer_index[m]> <signing_pubkey[m]> <signer_program[n]> <m> <n>
     *
     * The policy commits to signer programs. Signing pubkeys are revealed only
     * by actual approving signers, then checked against their committed signer
     * program before signature verification.
     */
    if (spend.signatures.size() != spend.required ||
        spend.signer_indexes.size() != spend.required ||
        spend.signing_pubkeys.size() != spend.required) {
        error_out = "KVANTA5_P2QR multisig approval vectors are inconsistent";
        return false;
    }

    if (spend.signer_programs.size() != spend.total) {
        error_out = "KVANTA5_P2QR multisig signer program count does not match total";
        return false;
    }

    const uint256 expected_program = Kvanta5P2QRMultisigProgram(spend.required, spend.signer_programs);

    if (expected_program != committed_program) {
        error_out = "KVANTA5_P2QR multisig signer address policy does not match committed program";
        return false;
    }

    for (size_t i = 0; i < spend.signatures.size(); ++i) {
        const uint16_t signer_index = spend.signer_indexes[i];

        if (signer_index >= spend.signer_programs.size()) {
            error_out = "KVANTA5_P2QR multisig signer index out of range";
            return false;
        }

        const std::vector<unsigned char>& pubkey = spend.signing_pubkeys[i];
        const std::vector<unsigned char>& signature = spend.signatures[i];

        if (pubkey.size() != KVANTA5_P2QR_PUBKEY_SIZE) {
            error_out = "KVANTA5_P2QR multisig bad signing pubkey size";
            return false;
        }

        const uint256 derived_signer_program = Kvanta5P2QRSingleProgram(pubkey);
        if (derived_signer_program != spend.signer_programs[signer_index]) {
            error_out = "KVANTA5_P2QR multisig signing pubkey does not match committed signer address";
            return false;
        }

        if (!MLDSA87Verify(
                Span<const unsigned char>(pubkey.data(), pubkey.size()),
                Span<const unsigned char>(sighash.begin(), sighash.size()),
                Span<const unsigned char>(signature.data(), signature.size()))) {
            error_out = "KVANTA5_P2QR multisig ML-DSA-87 signature verification failed";
            return false;
        }
    }

    return true;
}

} // namespace

bool IsOutput(const CScript& script_pubkey, uint256& program_out)
{
    std::vector<std::vector<unsigned char>> solutions;
    if (Solver(script_pubkey, solutions) != TxoutType::KVANTA5_P2QR) {
        return false;
    }

    if (solutions.size() != 1 || solutions[0].size() != 32) {
        return false;
    }

    std::copy(solutions[0].begin(), solutions[0].end(), program_out.begin());
    return true;
}

bool IsRedeemScript(const CScript& redeem_script, uint256& program_out)
{
    return IsOutput(redeem_script, program_out);
}

CScript MakeRedeemScript(const uint256& program)
{
    return CScript() << OP_RESERVED << ToByteVector(program);
}

CScript MakeP2SHScriptPubKey(const uint256& program)
{
    const CScript redeem_script = MakeRedeemScript(program);
    const CScriptID script_id(redeem_script);

    return CScript()
        << OP_HASH160
        << std::vector<unsigned char>(script_id.begin(), script_id.end())
        << OP_EQUAL;
}

bool IsP2SHOutput(const CScript& script_pubkey, CScriptID& script_id_out)
{
    std::vector<std::vector<unsigned char>> solutions;
    if (Solver(script_pubkey, solutions) != TxoutType::SCRIPTHASH) {
        return false;
    }

    if (solutions.size() != 1 || solutions[0].size() != 20) {
        return false;
    }

    std::copy(solutions[0].begin(), solutions[0].end(), script_id_out.begin());
    return true;
}

bool IsP2SHWrappedSpendCandidate(
    const CScript& spent_script_pubkey,
    const CScript& script_sig)
{
    CScriptID expected_script_id;
    if (!IsP2SHOutput(spent_script_pubkey, expected_script_id)) {
        return false;
    }

    std::string error;
    std::vector<std::vector<unsigned char>> pushes;
    if (!ReadPushesOnly(script_sig, pushes, error, KVANTA5_P2QR_MULTISIG_P2SH_MAX_PUSHES)) {
        return false;
    }

    if (pushes.empty()) {
        return false;
    }

    const CScript redeem_script(pushes.back().begin(), pushes.back().end());

    uint256 program;
    if (!IsRedeemScript(redeem_script, program)) {
        return false;
    }

    const CScriptID actual_script_id(redeem_script);
    return actual_script_id == expected_script_id;
}

bool SpendsAnyP2QR(const std::vector<CTxOut>& spent_outputs)
{
    uint256 program;
    for (const CTxOut& spent_output : spent_outputs) {
        if (IsOutput(spent_output.scriptPubKey, program)) {
            return true;
        }
    }
    return false;
}

uint256 SignatureHash(
    const CTransaction& tx,
    uint32_t input_index,
    const CTxOut& spent_output,
    const std::vector<CTxOut>& spent_outputs)
{
    assert(input_index < tx.vin.size());
    assert(spent_outputs.size() == tx.vin.size());

    uint256 program;
    assert(IsOutput(spent_output.scriptPubKey, program));

    return SignatureHashInternal(tx, input_index, spent_output, spent_outputs);
}

uint256 SignatureHashP2SHWrapped(
    const CTransaction& tx,
    uint32_t input_index,
    const CTxOut& spent_output,
    const std::vector<CTxOut>& spent_outputs)
{
    assert(input_index < tx.vin.size());
    assert(spent_outputs.size() == tx.vin.size());

    CScriptID script_id;
    assert(IsP2SHOutput(spent_output.scriptPubKey, script_id));

    return SignatureHashInternal(tx, input_index, spent_output, spent_outputs);
}

bool ParseSpendData(
    const CScript& script_sig,
    std::vector<unsigned char>& signature_out,
    std::vector<unsigned char>& pubkey_out,
    std::string& error_out)
{
    signature_out.clear();
    pubkey_out.clear();
    error_out.clear();

    std::vector<std::vector<unsigned char>> pushes;
    if (!ReadPushesOnly(script_sig, pushes, error_out, 2)) {
        return false;
    }

    if (pushes.size() != 2) {
        error_out = "KVANTA5_P2QR scriptSig must contain exactly signature and public key";
        return false;
    }

    signature_out = std::move(pushes[0]);
    pubkey_out = std::move(pushes[1]);

    if (signature_out.size() != MLDSA87_SIGNATURE_SIZE) {
        error_out = "KVANTA5_P2QR signature has invalid size";
        return false;
    }

    if (pubkey_out.size() != MLDSA87_PUBLIC_KEY_SIZE) {
        error_out = "KVANTA5_P2QR public key has invalid size";
        return false;
    }

    return true;
}

bool ParseP2SHWrappedSpendData(
    const CScript& script_sig,
    std::vector<unsigned char>& signature_out,
    std::vector<unsigned char>& pubkey_out,
    CScript& redeem_script_out,
    uint256& program_out,
    std::string& error_out)
{
    signature_out.clear();
    pubkey_out.clear();
    redeem_script_out.clear();
    program_out.SetNull();
    error_out.clear();

    std::vector<std::vector<unsigned char>> pushes;
    if (!ReadPushesOnly(script_sig, pushes, error_out, 3)) {
        return false;
    }

    if (pushes.size() != 3) {
        error_out = "KVANTA5_P2QR P2SH scriptSig must contain signature, public key, and redeem script";
        return false;
    }

    signature_out = std::move(pushes[0]);
    pubkey_out = std::move(pushes[1]);
    redeem_script_out = CScript(pushes[2].begin(), pushes[2].end());

    if (!IsRedeemScript(redeem_script_out, program_out)) {
        error_out = "KVANTA5_P2QR P2SH redeem script is not a P2QR redeem script";
        return false;
    }

    if (signature_out.size() != MLDSA87_SIGNATURE_SIZE) {
        error_out = "KVANTA5_P2QR signature has invalid size";
        return false;
    }

    if (pubkey_out.size() != MLDSA87_PUBLIC_KEY_SIZE) {
        error_out = "KVANTA5_P2QR public key has invalid size";
        return false;
    }

    return true;
}

bool CheckSpend(
    const CTransaction& tx,
    uint32_t input_index,
    const CTxOut& spent_output,
    const std::vector<CTxOut>& spent_outputs,
    std::string& error_out)
{
    error_out.clear();

    if (input_index >= tx.vin.size()) {
        error_out = "KVANTA5_P2QR input index out of range";
        return false;
    }

    if (spent_outputs.size() != tx.vin.size()) {
        error_out = "KVANTA5_P2QR validation requires all spent outputs";
        return false;
    }

    const CTxIn& txin = tx.vin[input_index];

    if (!txin.scriptWitness.IsNull()) {
        error_out = "KVANTA5_P2QR spends must not contain witness data";
        return false;
    }

    uint256 committed_program;
    if (!IsOutput(spent_output.scriptPubKey, committed_program)) {
        error_out = "KVANTA5_P2QR validation called for non-P2QR output";
        return false;
    }

    std::vector<std::vector<unsigned char>> pushes;
    if (!ReadPushesOnly(txin.scriptSig, pushes, error_out, KVANTA5_P2QR_MULTISIG_MAX_PUSHES)) {
        return false;
    }

    if (pushes.size() == KVANTA5_P2QR_SINGLE_PUSHES) {
        const std::vector<unsigned char>& signature = pushes[0];
        const std::vector<unsigned char>& pubkey = pushes[1];

        return CheckSignatureAndProgram(
            tx,
            input_index,
            spent_output,
            spent_outputs,
            committed_program,
            signature,
            pubkey,
            error_out);
    }

    Kvanta5P2QRMultisigSpend multisig_spend;
    std::string parse_error;

    if (!Kvanta5ParseP2QRMultisigSpend(pushes, multisig_spend, &parse_error)) {
        error_out = strprintf("KVANTA5_P2QR multisig spend invalid: %s", parse_error);
        return false;
    }

    return CheckMultisigProgramAndSignatures(
        tx,
        input_index,
        spent_output,
        spent_outputs,
        committed_program,
        multisig_spend,
        error_out);
}

bool CheckSpendP2SHWrapped(
    const CTransaction& tx,
    uint32_t input_index,
    const CTxOut& spent_output,
    const std::vector<CTxOut>& spent_outputs,
    std::string& error_out)
{
    error_out.clear();

    if (input_index >= tx.vin.size()) {
        error_out = "KVANTA5_P2QR P2SH input index out of range";
        return false;
    }

    if (spent_outputs.size() != tx.vin.size()) {
        error_out = "KVANTA5_P2QR P2SH validation requires all spent outputs";
        return false;
    }

    const CTxIn& txin = tx.vin[input_index];

    if (!txin.scriptWitness.IsNull()) {
        error_out = "KVANTA5_P2QR P2SH spends must not contain witness data";
        return false;
    }

    CScriptID expected_script_id;
    if (!IsP2SHOutput(spent_output.scriptPubKey, expected_script_id)) {
        error_out = "KVANTA5_P2QR P2SH validation called for non-P2SH output";
        return false;
    }

    std::vector<std::vector<unsigned char>> pushes;
    if (!ReadPushesOnly(txin.scriptSig, pushes, error_out, KVANTA5_P2QR_MULTISIG_P2SH_MAX_PUSHES)) {
        return false;
    }

    if (pushes.empty()) {
        error_out = "KVANTA5_P2QR P2SH scriptSig is missing redeem script";
        return false;
    }

    const CScript redeem_script(pushes.back().begin(), pushes.back().end());
    pushes.pop_back();

    uint256 committed_program;
    if (!IsRedeemScript(redeem_script, committed_program)) {
        error_out = "KVANTA5_P2QR P2SH redeem script is not a P2QR redeem script";
        return false;
    }

    const CScriptID actual_script_id(redeem_script);
    if (actual_script_id != expected_script_id) {
        error_out = "KVANTA5_P2QR P2SH redeem script hash does not match prevout";
        return false;
    }

    if (pushes.size() == KVANTA5_P2QR_SINGLE_PUSHES) {
        const std::vector<unsigned char>& signature = pushes[0];
        const std::vector<unsigned char>& pubkey = pushes[1];

        return CheckSignatureAndProgram(
            tx,
            input_index,
            spent_output,
            spent_outputs,
            committed_program,
            signature,
            pubkey,
            error_out);
    }

    Kvanta5P2QRMultisigSpend multisig_spend;
    std::string parse_error;

    if (!Kvanta5ParseP2QRMultisigSpend(pushes, multisig_spend, &parse_error)) {
        error_out = strprintf("KVANTA5_P2QR P2SH multisig spend invalid: %s", parse_error);
        return false;
    }

    return CheckMultisigProgramAndSignatures(
        tx,
        input_index,
        spent_output,
        spent_outputs,
        committed_program,
        multisig_spend,
        error_out);
}

} // namespace consensus::kvanta5p2qr
