// Copyright (c) 2015-2026 The Bitcoin Core developers
// Copyright (c) 2026 The Kvanta5 Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// The Solver functions are used by policy and the wallet, but not consensus.

#ifndef KVANTA5_SCRIPT_SOLVER_H
#define KVANTA5_SCRIPT_SOLVER_H

#include <attributes.h>
#include <script/script.h>
#include <span.h>

#include <string>
#include <optional>
#include <utility>
#include <vector>

class CPubKey;

enum class TxoutType {
    NONSTANDARD,
    // 'standard' transaction types:
    ANCHOR, //!< anyone can spend script
    PUBKEY,
    PUBKEYHASH,
    SCRIPTHASH,
    MULTISIG,
    NULL_DATA, //!< unspendable OP_RETURN script that carries data
    WITNESS_V0_SCRIPTHASH,
    WITNESS_V0_KEYHASH,
    WITNESS_V1_TAPROOT,
    WITNESS_UNKNOWN, //!< Only for Witness versions not already defined above
    //! Native Kvanta5 quantum-resistant output.
    //! Not witness. Not OP_RETURN.
    KVANTA5_P2QR,
};

/** Get the name of a TxoutType as a string */
std::string GetTxnOutputType(TxoutType t);

constexpr bool IsPushdataOp(opcodetype opcode)
{
    return opcode > OP_FALSE && opcode <= OP_PUSHDATA4;
}

static constexpr unsigned int KVANTA5_P2QR_PROGRAM_SIZE = 32;

// Native KV5 quantum-resistant output marker.
// Old nodes execute OP_RESERVED and fail closed.
// Upgraded nodes recognize this as OP_QuantumQuark after activation.
static constexpr opcodetype OP_QuantumQuark = OP_RESERVED;
static constexpr opcodetype OP_KVANTA5_P2QR = OP_QuantumQuark;

/**
 * Parse a scriptPubKey and identify script type for standard scripts. If
 * successful, returns script type and parsed pubkeys or hashes, depending on
 * the type. For example, for a P2SH script, vSolutionsRet will contain the
 * script hash, for P2PKH it will contain the key hash, etc.
 *
 * @param[in]   scriptPubKey   Script to parse
 * @param[out]  vSolutionsRet  Vector of parsed pubkeys and hashes
 * @return                     The script type. TxoutType::NONSTANDARD represents a failed solve.
 */
TxoutType Solver(const CScript& scriptPubKey, std::vector<std::vector<unsigned char>>& vSolutionsRet);

/** Generate a P2PK script for the given pubkey. */
CScript GetScriptForRawPubKey(const CPubKey& pubkey);

/** Determine if script is a "multi_a" script. Returns (threshold, keyspans) if so, and nullopt otherwise.
 *  The keyspans refer to bytes in the passed script. */
std::optional<std::pair<int, std::vector<Span<const unsigned char>>>> MatchMultiA(const CScript& script LIFETIMEBOUND);

/** Generate a multisig script. */
CScript GetScriptForMultisig(int nRequired, const std::vector<CPubKey>& keys);

#endif // KVANTA5_SCRIPT_SOLVER_H
