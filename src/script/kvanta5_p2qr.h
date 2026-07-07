// Copyright (c) 2026 Kvanta5 Core Developers.
// Copyright (c) 2026 Kvanta5 Organization.
// All rights reserved.
//
// Kvanta5 P2QR Transaction System Component.
//
// This file contains original Kvanta5 P2QR transaction system code,
// including but not limited to P2QR output recognition, P2QR redeem-script
// construction, P2SH-wrapped P2QR compatibility handling, P2QR signature-hash
// construction, ML-DSA-87 spend validation, P2QR multisig validation,
// signer-policy validation, and related consensus-critical transaction logic.
//
// Source access, if provided, is for transparency, audit, review,
// interoperability verification, and security inspection only.
//
// No permission is granted to copy, reproduce, modify, fork, adapt, translate,
// publish, distribute, sublicense, sell, lease, mirror, host, reimplement,
// create derivative works from, or use this file or any substantial portion
// of the Kvanta5 P2QR Transaction System without prior express written
// permission from the Kvanta5 Core Developers or an authorized representative
// of the Kvanta5 Organization.
//
// This file is NOT licensed under the MIT License.
// This file is NOT open-source software.
// This file is NOT free software.
// All rights not expressly granted in writing are reserved.
//
// Third-party components, including Bitcoin Core-derived code and external
// cryptographic libraries, remain subject to their own license terms.


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
