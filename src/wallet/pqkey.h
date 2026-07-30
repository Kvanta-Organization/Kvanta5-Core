// Copyright (c) 2015-2026 The Bitcoin Core developers
// Copyright (c) 2026 The Kvanta5 Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.


#ifndef KVANTA5_WALLET_PQKEY_H
#define KVANTA5_WALLET_PQKEY_H

#include <addresstype.h>
#include <crypto/pq/mldsa87/mldsa87.h>
#include <serialize.h>
#include <span.h>
#include <uint256.h>

#include <cstdint>
#include <vector>

class P2QRKeyRecord
{
public:
    std::vector<unsigned char> seed;
    std::vector<unsigned char> pubkey;
    int64_t creation_time{0};

    SERIALIZE_METHODS(P2QRKeyRecord, obj)
    {
        READWRITE(obj.seed, obj.pubkey, obj.creation_time);
    }
};

class P2QRSeedRecord
{
public:
    std::vector<unsigned char> seed;
    int64_t creation_time{0};

    SERIALIZE_METHODS(P2QRSeedRecord, obj)
    {
        READWRITE(obj.seed, obj.creation_time);
    }
};

class Kvanta5P2QRMultisigRecord
{
public:
    uint16_t required{0};

    /*
     * Vault multisig policy is committed to signer P2QR programs/addresses,
     * not full signer pubkeys. Pubkeys are only revealed by signers at spend
     * time and must derive back to these signer programs.
     */
    std::vector<uint256> signer_programs;

    SERIALIZE_METHODS(Kvanta5P2QRMultisigRecord, obj)
    {
        READWRITE(obj.required, obj.signer_programs);
    }
};

class CPQPubKey
{
private:
    std::vector<unsigned char> m_pubkey;

public:
    CPQPubKey() = default;
    explicit CPQPubKey(std::vector<unsigned char> pubkey);

    bool IsValid() const;
    Span<const unsigned char> Bytes() const;
    const std::vector<unsigned char>& Raw() const;

    uint256 GetHash() const;
    Kvanta5P2QRDestination GetDestination() const;
};

class CPQKey
{
private:
    std::vector<unsigned char> m_seed;
    std::vector<unsigned char> m_pubkey;
    std::vector<unsigned char> m_seckey;

public:
    CPQKey() = default;

    bool MakeNewKey();
    bool SetSeed(Span<const unsigned char> seed);
    bool SetSeedBytes(const std::vector<unsigned char>& seed);
    bool IsValid() const;
    bool HasSeed() const;

    CPQPubKey GetPubKey() const;
    const std::vector<unsigned char>& GetSeedBytes() const;
    const std::vector<unsigned char>& GetPubKeyBytes() const;
    const std::vector<unsigned char>& GetSecretKeyBytes() const;

    uint256 GetHash() const;
    Kvanta5P2QRDestination GetDestination() const;

    bool Sign(Span<const unsigned char> msg, std::vector<unsigned char>& sig_out) const;
    bool SelfTest() const;
};

#endif // KVANTA5_WALLET_PQKEY_H
