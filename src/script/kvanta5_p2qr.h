#ifndef KVANTA5_SCRIPT_KVANTA5_P2QR_H
#define KVANTA5_SCRIPT_KVANTA5_P2QR_H

#include <script/script.h>
#include <uint256.h>
#include <script/solver.h>
#include <crypto/pq/mldsa87/mldsa87.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

static constexpr size_t KVANTA5_P2QR_PUBKEY_SIZE = MLDSA87_PUBLIC_KEY_SIZE;
static constexpr size_t KVANTA5_P2QR_SIGNATURE_SIZE = MLDSA87_SIGNATURE_SIZE;

static constexpr unsigned int KVANTA5_P2QR_MULTISIG_MAX_KEYS = 1024;
static constexpr unsigned int KVANTA5_P2QR_MULTISIG_MAX_SIGNATURES = 1024;

struct Kvanta5P2QRMultisigSpend
{
    uint16_t required{0};
    uint16_t total{0};

    std::vector<std::vector<unsigned char>> signatures;
    std::vector<uint16_t> signer_indexes;

    // Legacy/live-chain pubkey-policy format:
    // <sig[m]> <signer_index[m]> <pubkey[n]> <m> <n>
    std::vector<std::vector<unsigned char>> pubkeys;

    // New signer-address/program-policy format:
    // <sig[m]> <signer_index[m]> <signing_pubkey[m]> <signer_program[n]> <m> <n>
    std::vector<std::vector<unsigned char>> signing_pubkeys;
    std::vector<uint256> signer_programs;
};

bool Kvanta5IsP2QRScriptPubKey(const CScript& script_pub_key, uint256* program_out = nullptr);

uint256 Kvanta5P2QRSingleProgram(const std::vector<unsigned char>& pubkey);

uint256 Kvanta5P2QRMultisigProgram(
    uint16_t required,
    const std::vector<uint256>& signer_programs);
    
uint256 Kvanta5P2QRMultisigProgram(
    uint16_t required,
    const std::vector<std::vector<unsigned char>>& pubkeys);

bool Kvanta5IsCanonicalP2QRMultisigPolicy(
    uint16_t required,
    const std::vector<std::vector<unsigned char>>& pubkeys,
    std::string* error = nullptr);

bool Kvanta5IsCanonicalP2QRMultisigPolicy(
    uint16_t required,
    const std::vector<uint256>& signer_programs,
    std::string* error = nullptr);

bool Kvanta5ParseP2QRMultisigSpend(
    const std::vector<std::vector<unsigned char>>& stack,
    Kvanta5P2QRMultisigSpend& out,
    std::string* error = nullptr);

CScript Kvanta5MakeP2QRScriptPubKey(const uint256& program);

bool Kvanta5IsCanonicalP2QRPush(opcodetype opcode, const std::vector<unsigned char>& data);

bool Kvanta5ReadCanonicalP2QRPushes(
    const CScript& script,
    std::vector<std::vector<unsigned char>>& pushes_out,
    std::string* error = nullptr,
    size_t max_pushes = 0);

#endif // KVANTA5_SCRIPT_KVANTA5_P2QR_H
