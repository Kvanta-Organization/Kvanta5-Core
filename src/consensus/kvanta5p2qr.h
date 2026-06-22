#ifndef KVANTA5_CONSENSUS_Kvanta5P2QR_H
#define KVANTA5_CONSENSUS_Kvanta5P2QR_H

#include <consensus/amount.h>
#include <primitives/transaction.h>
#include <script/script.h>
#include <uint256.h>

#include <cstdint>
#include <string>
#include <vector>

/**
 * Native KV5 P2QR consensus validation.
 *
 * KVANTA5_P2QR outputs are identified by:
 *
 *   OP_QuantumQuark / OP_RESERVED <32-byte p2qr_program>
 *
 * Spending native KVANTA5_P2QR is not Bitcoin Script execution. The spending input
 * carries native P2QR spend data in scriptSig as a strict two-push envelope:
 *
 *   <ML-DSA-87 signature> <ML-DSA-87 public key>
 *
 * P2SH-carried KVANTA5_P2QR compatibility outputs are identified by:
 *
 *   OP_HASH160 HASH160(<p2qr_redeem_script>) OP_EQUAL
 *
 * where:
 *
 *   p2qr_redeem_script = OP_QuantumQuark / OP_RESERVED <32-byte p2qr_program>
 *
 * Spending a P2SH-carried KVANTA5_P2QR output carries a strict three-push envelope:
 *
 *   <ML-DSA-87 signature> <ML-DSA-87 public key> <p2qr_redeem_script>
 *
 * P2SH-carried P2QR is only a compatibility container. Once detected, it must
 * route to native KVANTA5_P2QR consensus validation and must not succeed by normal
 * legacy P2SH script evaluation.
 */
namespace consensus::kvanta5p2qr {

/** Return true if script_pubkey is native KVANTA5_P2QR and extract the 32-byte program. */
bool IsOutput(const CScript& script_pubkey, uint256& program_out);

/** Return true if redeem_script is the native KVANTA5_P2QR redeem marker and extract the 32-byte program. */
bool IsRedeemScript(const CScript& redeem_script, uint256& program_out);

/** Build the canonical P2QR redeem script: OP_RESERVED <32-byte p2qr_program>. */
CScript MakeRedeemScript(const uint256& program);

/** Build a P2SH-compatible scriptPubKey that carries the canonical P2QR redeem script. */
CScript MakeP2SHScriptPubKey(const uint256& program);

/** Return true if script_pubkey is P2SH and extract the script hash. */
bool IsP2SHOutput(const CScript& script_pubkey, CScriptID& script_id_out);

/**
 * Return true if this input appears to be a P2SH-carried KVANTA5_P2QR spend.
 *
 * This only detects the wrapper/redeemScript routing condition. Full signature,
 * public key, program, and sighash validation is done by CheckSpendP2SHWrapped().
 */
bool IsP2SHWrappedSpendCandidate(
    const CScript& spent_script_pubkey,
    const CScript& script_sig);

/** Return true if any spent output is native KVANTA5_P2QR. */
bool SpendsAnyP2QR(const std::vector<CTxOut>& spent_outputs);

/**
 * Native KVANTA5_P2QR sighash.
 *
 * v1 deliberately supports SIGHASH_ALL only. This avoids inheriting legacy,
 * witness, or Taproot sighash corner cases.
 */
uint256 SignatureHash(
    const CTransaction& tx,
    uint32_t input_index,
    const CTxOut& spent_output,
    const std::vector<CTxOut>& spent_outputs);

/**
 * Native KVANTA5_P2QR sighash for a P2SH-carried P2QR spend.
 *
 * The spent_output is the actual P2SH prevout. The wrapper is only a carrier;
 * the signature still commits through the KVANTA5_P2QR tagged hash path.
 */
uint256 SignatureHashP2SHWrapped(
    const CTransaction& tx,
    uint32_t input_index,
    const CTxOut& spent_output,
    const std::vector<CTxOut>& spent_outputs);


/**
 * Parse native KVANTA5_P2QR spend envelope from scriptSig.
 *
 * Expected scriptSig:
 *
 *   <4627-byte ML-DSA-87 signature> <2592-byte ML-DSA-87 public key>
 *
 * This parser does not execute script and does not apply MAX_SCRIPT_ELEMENT_SIZE.
 */
bool ParseSpendData(
    const CScript& script_sig,
    std::vector<unsigned char>& signature_out,
    std::vector<unsigned char>& pubkey_out,
    std::string& error_out);

/**
 * Parse P2SH-carried KVANTA5_P2QR spend envelope from scriptSig.
 *
 * Expected scriptSig:
 *
 *   <4627-byte ML-DSA-87 signature>
 *   <2592-byte ML-DSA-87 public key>
 *   <p2qr_redeem_script = OP_RESERVED <32-byte p2qr_program>>
 *
 * This parser does not execute script and does not apply MAX_SCRIPT_ELEMENT_SIZE.
 */
bool ParseP2SHWrappedSpendData(
    const CScript& script_sig,
    std::vector<unsigned char>& signature_out,
    std::vector<unsigned char>& pubkey_out,
    CScript& redeem_script_out,
    uint256& program_out,
    std::string& error_out);

/**
 * Validate one native KVANTA5_P2QR input spend.
 *
 * This assumes activation has already been checked by the caller.
 */
bool CheckSpend(
    const CTransaction& tx,
    uint32_t input_index,
    const CTxOut& spent_output,
    const std::vector<CTxOut>& spent_outputs,
    std::string& error_out);

/**
 * Validate one P2SH-carried KVANTA5_P2QR input spend.
 *
 * This assumes activation has already been checked by the caller.
 *
 * The P2SH wrapper is only a carrier. The spend must validate through the same
 * native ML-DSA-87 P2QR consensus path.
 */
bool CheckSpendP2SHWrapped(
    const CTransaction& tx,
    uint32_t input_index,
    const CTxOut& spent_output,
    const std::vector<CTxOut>& spent_outputs,
    std::string& error_out);

} // namespace consensus::kvanta5p2qr

#endif // KVANTA5_CONSENSUS_Kvanta5P2QR_H
