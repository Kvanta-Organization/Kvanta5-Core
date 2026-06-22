#include <script/kvanta5_p2qr.h>

#include <hash.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

static void Kvanta5SetP2QRError(std::string* error, const std::string& msg)
{
    if (error) *error = msg;
}


bool Kvanta5IsCanonicalP2QRPush(opcodetype opcode, const std::vector<unsigned char>& data)
{
    const size_t size = data.size();

    /*
     * Kvanta5 P2QR canonical scriptSig push rules.
     *
     * This intentionally does NOT require OP_1..OP_16 for numeric values,
     * because Kvanta5 wallet signing currently encodes m/n/index values as
     * byte-vector pushes.
     */

    if (size == 0) {
        return opcode == OP_0;
    }

    if (size <= 75) {
        return opcode == static_cast<opcodetype>(size);
    }

    if (size <= 0xff) {
        return opcode == OP_PUSHDATA1;
    }

    if (size <= 0xffff) {
        return opcode == OP_PUSHDATA2;
    }

    return opcode == OP_PUSHDATA4;
}

bool Kvanta5ReadCanonicalP2QRPushes(
    const CScript& script,
    std::vector<std::vector<unsigned char>>& pushes_out,
    std::string* error,
    size_t max_pushes)
{
    pushes_out.clear();

    if (script.empty()) {
        Kvanta5SetP2QRError(error, "p2qr-script-empty");
        return false;
    }

    if (!script.IsPushOnly()) {
        Kvanta5SetP2QRError(error, "p2qr-script-not-push-only");
        return false;
    }

    CScript::const_iterator pc = script.begin();

    while (pc < script.end()) {
        opcodetype opcode;
        std::vector<unsigned char> data;

        if (!script.GetOp(pc, opcode, data)) {
            Kvanta5SetP2QRError(error, "p2qr-script-invalid-push-opcode");
            return false;
        }

        if (opcode > OP_PUSHDATA4) {
            Kvanta5SetP2QRError(error, "p2qr-script-non-push-opcode");
            return false;
        }

        if (!Kvanta5IsCanonicalP2QRPush(opcode, data)) {
            Kvanta5SetP2QRError(error, "p2qr-script-non-canonical-push");
            return false;
        }

        pushes_out.push_back(std::move(data));

        if (max_pushes != 0 && pushes_out.size() > max_pushes) {
            Kvanta5SetP2QRError(error, "p2qr-script-too-many-pushes");
            return false;
        }
    }

    if (pushes_out.empty()) {
        Kvanta5SetP2QRError(error, "p2qr-script-no-pushes");
        return false;
    }

    return true;
}


static bool Kvanta5ReadMinimalUInt16NonZero(const std::vector<unsigned char>& v, uint16_t& out)
{
    if (v.empty()) return false;
    if (v.size() > 2) return false;

    if (v.size() == 1) {
        if (v[0] == 0x00) return false;
        out = v[0];
        return true;
    }

    // 2-byte little-endian. Values <= 255 must use 1 byte.
    if (v[1] == 0x00) return false;

    out = static_cast<uint16_t>(v[0]) |
          (static_cast<uint16_t>(v[1]) << 8);

    if (out <= 0xff) return false;

    return true;
}

static bool Kvanta5ReadMinimalUInt16AllowZero(const std::vector<unsigned char>& v, uint16_t& out)
{
    if (v.empty()) return false;
    if (v.size() > 2) return false;

    if (v.size() == 1) {
        out = v[0];
        return true;
    }

    // 2-byte little-endian. Values <= 255 must use 1 byte.
    if (v[1] == 0x00) return false;

    out = static_cast<uint16_t>(v[0]) |
          (static_cast<uint16_t>(v[1]) << 8);

    if (out <= 0xff) return false;

    return true;
}

static uint256 Kvanta5Uint256FromP2QRProgramPush(const std::vector<unsigned char>& push)
{
    uint256 program;
    std::copy(push.begin(), push.end(), program.begin());
    return program;
}

bool Kvanta5IsP2QRScriptPubKey(const CScript& script_pub_key, uint256* program_out)
{
    if (script_pub_key.size() != 34) return false;

    auto it = script_pub_key.begin();

    if (it == script_pub_key.end() || *it++ != OP_RESERVED) return false;
    if (it == script_pub_key.end() || *it++ != 0x20) return false;

    std::vector<unsigned char> program;
    program.reserve(KVANTA5_P2QR_PROGRAM_SIZE);

    for (size_t i = 0; i < KVANTA5_P2QR_PROGRAM_SIZE; ++i) {
        if (it == script_pub_key.end()) return false;
        program.push_back(*it++);
    }

    if (it != script_pub_key.end()) return false;

    if (program_out) {
        std::copy(program.begin(), program.end(), program_out->begin());
    }

    return true;
}

CScript Kvanta5MakeP2QRScriptPubKey(const uint256& program)
{
    return CScript() << OP_RESERVED << std::vector<unsigned char>(program.begin(), program.end());
}

uint256 Kvanta5P2QRSingleProgram(const std::vector<unsigned char>& pubkey)
{
    // Preserve existing Kvanta5 single-sig P2QR consensus behavior:
    // program = Hash(pubkey)
    return Hash(pubkey);
}

bool Kvanta5IsCanonicalP2QRMultisigPolicy(
    uint16_t required,
    const std::vector<uint256>& signer_programs,
    std::string* error)
{
    const uint16_t total = static_cast<uint16_t>(signer_programs.size());

    if (required == 0) {
        Kvanta5SetP2QRError(error, "p2qr-multisig-required-zero");
        return false;
    }

    if (total == 0) {
        Kvanta5SetP2QRError(error, "p2qr-multisig-total-zero");
        return false;
    }

    if (required > total) {
        Kvanta5SetP2QRError(error, "p2qr-multisig-required-greater-than-total");
        return false;
    }

    if (total > KVANTA5_P2QR_MULTISIG_MAX_KEYS) {
        Kvanta5SetP2QRError(error, "p2qr-multisig-too-many-keys");
        return false;
    }

    if (required > KVANTA5_P2QR_MULTISIG_MAX_SIGNATURES) {
        Kvanta5SetP2QRError(error, "p2qr-multisig-too-many-signatures");
        return false;
    }

    for (size_t i = 1; i < signer_programs.size(); ++i) {
        if (!(signer_programs[i - 1] < signer_programs[i])) {
            Kvanta5SetP2QRError(error, "p2qr-multisig-signer-programs-not-strictly-sorted");
            return false;
        }
    }

    return true;
}

bool Kvanta5IsCanonicalP2QRMultisigPolicy(
    uint16_t required,
    const std::vector<std::vector<unsigned char>>& pubkeys,
    std::string* error)
{
    if (required == 0 || pubkeys.empty() || required > pubkeys.size()) {
        Kvanta5SetP2QRError(error, "p2qr-multisig-invalid-threshold");
        return false;
    }

    if (pubkeys.size() > KVANTA5_P2QR_MULTISIG_MAX_KEYS) {
        Kvanta5SetP2QRError(error, "p2qr-multisig-too-many-keys");
        return false;
    }

    if (required > KVANTA5_P2QR_MULTISIG_MAX_SIGNATURES) {
        Kvanta5SetP2QRError(error, "p2qr-multisig-too-many-signatures");
        return false;
    }

    for (const auto& pubkey : pubkeys) {
        if (pubkey.size() != KVANTA5_P2QR_PUBKEY_SIZE) {
            Kvanta5SetP2QRError(error, "p2qr-multisig-bad-pubkey-size");
            return false;
        }
    }

    for (size_t i = 1; i < pubkeys.size(); ++i) {
        if (!(pubkeys[i - 1] < pubkeys[i])) {
            Kvanta5SetP2QRError(error, "p2qr-multisig-pubkeys-not-strictly-sorted");
            return false;
        }
    }

    return true;
}


uint256 Kvanta5P2QRMultisigProgram(
    uint16_t required,
    const std::vector<uint256>& signer_programs)
{
    HashWriter ss{};

    const std::string domain{"KV5:P2QR:MSIG:ADDR:v1"};

    ss << domain;
    ss << required;
    ss << static_cast<uint16_t>(signer_programs.size());

    for (const uint256& signer_program : signer_programs) {
        ss << signer_program;
    }

    return ss.GetSHA256();
}

uint256 Kvanta5P2QRMultisigProgram(
    uint16_t required,
    const std::vector<std::vector<unsigned char>>& pubkeys)
{
    HashWriter ss{};

    /*
     * Legacy/live-chain pubkey-policy multisig commitment.
     *
     * DO NOT change this domain once historical blocks exist.
     * This must match the version that created old vault outputs already
     * present on-chain, including the spend in block 13153.
     */
    const std::string domain{"KV5:P2QR:MSIG:v1"};

    ss << domain;
    ss << required;
    ss << static_cast<uint16_t>(pubkeys.size());

    for (const std::vector<unsigned char>& pubkey : pubkeys) {
        ss << pubkey;
    }

    return ss.GetSHA256();
}


bool Kvanta5ParseP2QRMultisigSpend(
    const std::vector<std::vector<unsigned char>>& stack,
    Kvanta5P2QRMultisigSpend& out,
    std::string* error)
{
    out = Kvanta5P2QRMultisigSpend{};

    /*
     * Permanent dual-format P2QR multisig parser.
     *
     * Legacy/live-chain pubkey-policy stack:
     *
     *   <signature[0]> ... <signature[m-1]>
     *   <signer_index[0]> ... <signer_index[m-1]>
     *   <pubkey[0]> ... <pubkey[n-1]>
     *   <m>
     *   <n>
     *
     * New signer-address/program-policy stack:
     *
     *   <signature[0]> ... <signature[m-1]>
     *   <signer_index[0]> ... <signer_index[m-1]>
     *   <signing_pubkey[0]> ... <signing_pubkey[m-1]>
     *   <signer_program[0]> ... <signer_program[n-1]>
     *   <m>
     *   <n>
     *
     * The last two pushes are always m and n, so we can decode threshold first,
     * then disambiguate by total stack size.
     */
    if (stack.size() < 5) {
        Kvanta5SetP2QRError(error, "p2qr-multisig-stack-too-small");
        return false;
    }

    uint16_t total = 0;
    uint16_t required = 0;

    if (!Kvanta5ReadMinimalUInt16NonZero(stack[stack.size() - 1], total)) {
        Kvanta5SetP2QRError(error, "p2qr-multisig-bad-total-encoding");
        return false;
    }

    if (!Kvanta5ReadMinimalUInt16NonZero(stack[stack.size() - 2], required)) {
        Kvanta5SetP2QRError(error, "p2qr-multisig-bad-required-encoding");
        return false;
    }

    if (required == 0 || total == 0 || required > total) {
        Kvanta5SetP2QRError(error, "p2qr-multisig-invalid-threshold");
        return false;
    }

    if (total > KVANTA5_P2QR_MULTISIG_MAX_KEYS) {
        Kvanta5SetP2QRError(error, "p2qr-multisig-too-many-keys");
        return false;
    }

    if (required > KVANTA5_P2QR_MULTISIG_MAX_SIGNATURES) {
        Kvanta5SetP2QRError(error, "p2qr-multisig-too-many-signatures");
        return false;
    }

    const size_t legacy_expected_stack_size =
        static_cast<size_t>(required) + // signatures
        static_cast<size_t>(required) + // signer indexes
        static_cast<size_t>(total) +    // pubkeys
        2;                              // m, n

    const size_t signer_program_expected_stack_size =
        static_cast<size_t>(required) + // signatures
        static_cast<size_t>(required) + // signer indexes
        static_cast<size_t>(required) + // signing pubkeys
        static_cast<size_t>(total) +    // signer programs
        2;                              // m, n

    const bool is_legacy_pubkey_policy = stack.size() == legacy_expected_stack_size;
    const bool is_signer_program_policy = stack.size() == signer_program_expected_stack_size;

    if (!is_legacy_pubkey_policy && !is_signer_program_policy) {
        Kvanta5SetP2QRError(error, "p2qr-multisig-extra-or-missing-stack-items");
        return false;
    }

    const size_t sig_begin = 0;
    const size_t sig_end = static_cast<size_t>(required);

    const size_t idx_begin = sig_end;
    const size_t idx_end = idx_begin + static_cast<size_t>(required);

    out.required = required;
    out.total = total;

    out.signatures.reserve(required);
    out.signer_indexes.reserve(required);

    for (size_t i = sig_begin; i < sig_end; ++i) {
        if (stack[i].size() != KVANTA5_P2QR_SIGNATURE_SIZE) {
            Kvanta5SetP2QRError(error, "p2qr-multisig-bad-signature-size");
            return false;
        }

        out.signatures.push_back(stack[i]);
    }

    uint16_t previous_index = 0;

    for (size_t i = idx_begin; i < idx_end; ++i) {
        uint16_t signer_index = 0;

        if (!Kvanta5ReadMinimalUInt16AllowZero(stack[i], signer_index)) {
            Kvanta5SetP2QRError(error, "p2qr-multisig-bad-signer-index-encoding");
            return false;
        }

        if (signer_index >= total) {
            Kvanta5SetP2QRError(error, "p2qr-multisig-signer-index-out-of-range");
            return false;
        }

        const size_t approval_pos = i - idx_begin;
        if (approval_pos > 0 && signer_index <= previous_index) {
            Kvanta5SetP2QRError(error, "p2qr-multisig-signer-indexes-not-strictly-sorted");
            return false;
        }

        previous_index = signer_index;
        out.signer_indexes.push_back(signer_index);
    }

    if (is_legacy_pubkey_policy) {
        /*
         * Legacy/live-chain format:
         *
         *   <sig[m]> <signer_index[m]> <pubkey[n]> <m> <n>
         *
         * This must validate historical chain data, including old vault spends.
         */
        const size_t pubkey_begin = idx_end;
        const size_t pubkey_end = pubkey_begin + static_cast<size_t>(total);

        out.pubkeys.reserve(total);

        for (size_t i = pubkey_begin; i < pubkey_end; ++i) {
            if (stack[i].size() != KVANTA5_P2QR_PUBKEY_SIZE) {
                Kvanta5SetP2QRError(error, "p2qr-multisig-bad-pubkey-size");
                return false;
            }

            out.pubkeys.push_back(stack[i]);
        }

        for (size_t i = 1; i < out.pubkeys.size(); ++i) {
            if (!(out.pubkeys[i - 1] < out.pubkeys[i])) {
                Kvanta5SetP2QRError(error, "p2qr-multisig-pubkeys-not-strictly-sorted");
                return false;
            }
        }

        return true;
    }

    /*
     * New signer-address/program-policy format:
     *
     *   <sig[m]> <signer_index[m]> <signing_pubkey[m]> <signer_program[n]> <m> <n>
     */
    const size_t signing_pubkey_begin = idx_end;
    const size_t signing_pubkey_end = signing_pubkey_begin + static_cast<size_t>(required);

    const size_t signer_program_begin = signing_pubkey_end;
    const size_t signer_program_end = signer_program_begin + static_cast<size_t>(total);

    out.signing_pubkeys.reserve(required);
    out.signer_programs.reserve(total);

    for (size_t i = signing_pubkey_begin; i < signing_pubkey_end; ++i) {
        if (stack[i].size() != KVANTA5_P2QR_PUBKEY_SIZE) {
            Kvanta5SetP2QRError(error, "p2qr-multisig-bad-signing-pubkey-size");
            return false;
        }

        out.signing_pubkeys.push_back(stack[i]);
    }

    for (size_t i = signer_program_begin; i < signer_program_end; ++i) {
        if (stack[i].size() != KVANTA5_P2QR_PROGRAM_SIZE) {
            Kvanta5SetP2QRError(error, "p2qr-multisig-bad-signer-program-size");
            return false;
        }

        out.signer_programs.push_back(Kvanta5Uint256FromP2QRProgramPush(stack[i]));
    }

    if (!Kvanta5IsCanonicalP2QRMultisigPolicy(required, out.signer_programs, error)) {
        return false;
    }

    /*
     * Each revealed signing pubkey must derive to the signer program at its
     * claimed signer index.
     */
    for (size_t i = 0; i < out.signing_pubkeys.size(); ++i) {
        const uint16_t signer_index = out.signer_indexes[i];
        const uint256 derived_program = Kvanta5P2QRSingleProgram(out.signing_pubkeys[i]);

        if (derived_program != out.signer_programs[signer_index]) {
            Kvanta5SetP2QRError(error, "p2qr-multisig-signing-pubkey-does-not-match-signer-program");
            return false;
        }
    }

    return true;
}
