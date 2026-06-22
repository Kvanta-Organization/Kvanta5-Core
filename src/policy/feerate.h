// Copyright (c) 2015-2026 The Bitcoin Core developers
// Copyright (c) 2026 The Kvanta5 Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KVANTA5_POLICY_FEERATE_H
#define KVANTA5_POLICY_FEERATE_H

#include <consensus/amount.h>
#include <serialize.h>


#include <cstdint>
#include <string>
#include <type_traits>

const std::string CURRENCY_UNIT = "KV5"; // One formatted unit
const std::string CURRENCY_ATOM = "quark"; // One indivisible minimum value unit

/* Used to determine type of fee estimation requested */
enum class FeeEstimateMode {
    UNSET,        //!< Use default settings based on other criteria
    ECONOMICAL,   //!< Force estimateSmartFee to use non-conservative estimates
    CONSERVATIVE, //!< Force estimateSmartFee to use conservative estimates
    KV5_KVB,      //!< Use KV5/kvB fee rate unit
    QUARK_VB,       //!< Use quark/vB fee rate unit
};

/**
 * Fee rate in quarks per kilovirtualbyte: CAmount / kvB
 */
class CFeeRate
{
private:
    /** Fee rate in quark/kvB (quarks per 1000 virtualbytes) */
    CAmount nQuarksPerK;

public:
    /** Fee rate of 0 quarks per kvB */
    CFeeRate() : nQuarksPerK(0) { }
    template<std::integral I> // Disallow silent float -> int conversion
    explicit CFeeRate(const I _nQuarksPerK): nQuarksPerK(_nQuarksPerK) {
    }

    /**
     * Construct a fee rate from a fee in quarks and a vsize in vB.
     *
     * param@[in]   nFeePaid    The fee paid by a transaction, in quarks
     * param@[in]   num_bytes   The vsize of a transaction, in vbytes
     */
    CFeeRate(const CAmount& nFeePaid, uint32_t num_bytes);

    /**
     * Return the fee in quarks for the given vsize in vbytes.
     * If the calculated fee would have fractional quarks, then the
     * returned fee will always be rounded up to the nearest quark.
     */
    CAmount GetFee(uint32_t num_bytes) const;

    /**
     * Return the fee in quarks for a vsize of 1000 vbytes
     */
    CAmount GetFeePerK() const { return nQuarksPerK; }
    friend bool operator<(const CFeeRate& a, const CFeeRate& b) { return a.nQuarksPerK < b.nQuarksPerK; }
    friend bool operator>(const CFeeRate& a, const CFeeRate& b) { return a.nQuarksPerK > b.nQuarksPerK; }
    friend bool operator==(const CFeeRate& a, const CFeeRate& b) { return a.nQuarksPerK == b.nQuarksPerK; }
    friend bool operator<=(const CFeeRate& a, const CFeeRate& b) { return a.nQuarksPerK <= b.nQuarksPerK; }
    friend bool operator>=(const CFeeRate& a, const CFeeRate& b) { return a.nQuarksPerK >= b.nQuarksPerK; }
    friend bool operator!=(const CFeeRate& a, const CFeeRate& b) { return a.nQuarksPerK != b.nQuarksPerK; }
    CFeeRate& operator+=(const CFeeRate& a) { nQuarksPerK += a.nQuarksPerK; return *this; }
    std::string ToString(const FeeEstimateMode& fee_estimate_mode = FeeEstimateMode::KV5_KVB) const;
    friend CFeeRate operator*(const CFeeRate& f, int a) { return CFeeRate(a * f.nQuarksPerK); }
    friend CFeeRate operator*(int a, const CFeeRate& f) { return CFeeRate(a * f.nQuarksPerK); }

    SERIALIZE_METHODS(CFeeRate, obj) { READWRITE(obj.nQuarksPerK); }
};

#endif // KVANTA5_POLICY_FEERATE_H
