// Copyright (c) 2015-2026 The Bitcoin Core developers
// Copyright (c) 2026 The Kvanta5 Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KVANTA5_CONSENSUS_DEVFUND_H
#define KVANTA5_CONSENSUS_DEVFUND_H

#include <coins.h>
#include <consensus/amount.h>
#include <primitives/transaction.h>
#include <script/script.h>
#include <uint256.h>
#include <util/strencodings.h>

#include <array>
#include <cstddef>
#include <vector>

static constexpr int KVANTA5_DEVFUND_BLOCK_HEIGHT = 1;
static constexpr int KVANTA5_FIRST_NORMAL_SUBSIDY_HEIGHT = 2;

static constexpr int KVANTA5_DEVFUND_FIRST_UNLOCK_HEIGHT = 2;
static constexpr int KVANTA5_DEVFUND_UNLOCK_INTERVAL = 262800; // ~6 months at 1-minute blocks

static constexpr CAmount KVANTA5_DEVFUND_OUTPUT_AMOUNT = 1000000 * COIN;
static constexpr CAmount KVANTA5_DEVFUND_TOTAL_AMOUNT = 21000000 * COIN;

static constexpr std::size_t KVANTA5_DEVFUND_OUTPUT_COUNT = 21;

struct Kvanta5DevFundEntry {
    const char* p2qr_program_hex;
    CAmount amount;
    int unlock_height;
};

static constexpr std::array<Kvanta5DevFundEntry, KVANTA5_DEVFUND_OUTPUT_COUNT> KVANTA5_DEVFUND_OUTPUTS{{
    {"57bbf8552c15302a447300590136ee2af11d6d7de264e90e6a2fe66120ce421a", KVANTA5_DEVFUND_OUTPUT_AMOUNT, 2},
    {"82c91b4ef34ae90f2a9e6195e09959c3eecc57d736c220b5a8b4b1216aa03c55", KVANTA5_DEVFUND_OUTPUT_AMOUNT, 262802},
    {"fea9594f3bf310ab9f533f0a81d106c1d57fcc4d6d1760934332793699ec54c7", KVANTA5_DEVFUND_OUTPUT_AMOUNT, 525602},
    {"6ff59f0d35c96c52038626b1a5d48570f1febb328e0bdd294fc81eb77d4bae44", KVANTA5_DEVFUND_OUTPUT_AMOUNT, 788402},
    {"3cedb6aafd6cca2e9b3e1119a902202c4343f8bfe982d5f5d53cd967fcf32d56", KVANTA5_DEVFUND_OUTPUT_AMOUNT, 1051202},
    {"8778479bbb616af1abb96fb1061f9e9f4d952c5ed5e05ed3deac8ab922762169", KVANTA5_DEVFUND_OUTPUT_AMOUNT, 1314002},
    {"4ea70f8d02e6035bf99cb859d3796f4cb1f12d7e38e02276bd9a887c4e8e586f", KVANTA5_DEVFUND_OUTPUT_AMOUNT, 1576802},
    {"e37f2d1b9e595a42ba8c1bff194dda11ea986d79ab8e56713b31f4430d592d2c", KVANTA5_DEVFUND_OUTPUT_AMOUNT, 1839602},
    {"86868ab5b4ab6dc7741c48548767cd8107bc66e317b1d6a2f297a118a39b3463", KVANTA5_DEVFUND_OUTPUT_AMOUNT, 2102402},
    {"62430c378aa379c92912866b1748f898f00510efc445ab047583c415fd3272ac", KVANTA5_DEVFUND_OUTPUT_AMOUNT, 2365202},
    {"0940c31609d82ef881674c53251839769289b8c672d23197cac2490f6e7f1476", KVANTA5_DEVFUND_OUTPUT_AMOUNT, 2628002},
    {"0c0f16ff254280f848666575d893ef36636364635def3562a4e7e52ad070d2a3", KVANTA5_DEVFUND_OUTPUT_AMOUNT, 2890802},
    {"8dfeb6a9439c660f0d27df6958c63aa23aa0105a857bbece408e18fc6714a1e8", KVANTA5_DEVFUND_OUTPUT_AMOUNT, 3153602},
    {"990b7646588b3fedca387758e2baf1a6147520069e57c02bf60ef7648c559916", KVANTA5_DEVFUND_OUTPUT_AMOUNT, 3416402},
    {"22db117a5784c7b65ec14f539749b90b707a83937efd054ab030bb3182293d1e", KVANTA5_DEVFUND_OUTPUT_AMOUNT, 3679202},
    {"75cf2005e4fb4759b8dbc921eff2f76f54ae14e872eefc7c40b82b95b56e7c9d", KVANTA5_DEVFUND_OUTPUT_AMOUNT, 3942002},
    {"dc3c9e7e1bc4075a6ccde17dbe6580e9522fa8eeba92e09a96ac9fea436c080b", KVANTA5_DEVFUND_OUTPUT_AMOUNT, 4204802},
    {"e8c232c9a2fe8453f6a22efa47f5e495665af65cc80c9e703ebf2d0a58d0f3b9", KVANTA5_DEVFUND_OUTPUT_AMOUNT, 4467602},
    {"6d4a90100e3f0c4ccb7dfb32661d1b1003464c777c03038d219dd5c2c3d7278a", KVANTA5_DEVFUND_OUTPUT_AMOUNT, 4730402},
    {"bd6e1bd6adb0bd5967a00879944c994610d182f4432ae63d24352a663a42fa31", KVANTA5_DEVFUND_OUTPUT_AMOUNT, 4993202},
    {"2e08d96727de1a1151d5f8f6960f07641ab6f485d96ad2f938412d4d3d229d36", KVANTA5_DEVFUND_OUTPUT_AMOUNT, 5256002},
}};

static inline CScript Kvanta5DevFundScript(const std::size_t index)
{
    const std::vector<unsigned char> program{ParseHex(KVANTA5_DEVFUND_OUTPUTS.at(index).p2qr_program_hex)};
    return CScript() << OP_RESERVED << program;
}

static inline CTxOut Kvanta5DevFundTxOut(const std::size_t index)
{
    return CTxOut(KVANTA5_DEVFUND_OUTPUTS.at(index).amount, Kvanta5DevFundScript(index));
}

static inline std::vector<CTxOut> Kvanta5DevFundCoinbaseOutputs()
{
    std::vector<CTxOut> outputs;
    outputs.reserve(KVANTA5_DEVFUND_OUTPUT_COUNT);

    for (std::size_t i = 0; i < KVANTA5_DEVFUND_OUTPUT_COUNT; ++i) {
        outputs.emplace_back(Kvanta5DevFundTxOut(i));
    }

    return outputs;
}

static inline bool Kvanta5DevFundCoinbaseIsExpected(const CTransaction& tx)
{
    if (!tx.IsCoinBase()) {
        return false;
    }

    if (tx.vout.size() != KVANTA5_DEVFUND_OUTPUT_COUNT) {
        return false;
    }

    CAmount total{0};

    for (std::size_t i = 0; i < KVANTA5_DEVFUND_OUTPUT_COUNT; ++i) {
        const CTxOut expected{Kvanta5DevFundTxOut(i)};
        const CTxOut& actual{tx.vout[i]};

        if (actual.nValue != expected.nValue) {
            return false;
        }

        if (actual.scriptPubKey != expected.scriptPubKey) {
            return false;
        }

        total += actual.nValue;
    }

    return total == KVANTA5_DEVFUND_TOTAL_AMOUNT;
}

static inline int Kvanta5DevFundUnlockHeightForCoin(const COutPoint& prevout, const Coin& coin)
{
    if (coin.IsSpent()) {
        return 0;
    }

    if (coin.nHeight != KVANTA5_DEVFUND_BLOCK_HEIGHT) {
        return 0;
    }

    if (prevout.n >= KVANTA5_DEVFUND_OUTPUT_COUNT) {
        return 0;
    }

    const std::size_t index{static_cast<std::size_t>(prevout.n)};
    const CTxOut expected{Kvanta5DevFundTxOut(index)};

    if (coin.out.nValue != expected.nValue) {
        return 0;
    }

    if (coin.out.scriptPubKey != expected.scriptPubKey) {
        return 0;
    }

    return KVANTA5_DEVFUND_OUTPUTS[index].unlock_height;
}

#endif // KVANTA5_CONSENSUS_DEVFUND_H
