// Copyright (c) 2015-2026 The Bitcoin Core developers
// Copyright (c) 2026 The Kvanta5 Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <headerssync.h>

#include <logging.h>
#include <pow.h>
#include <util/check.h>
#include <util/time.h>
#include <util/vector.h>

#include <algorithm>
#include <memory>
#include <vector>

// The two constants below are computed using the simulation script in
// contrib/devtools/headerssync-params.py.

//! Store one header commitment per HEADER_COMMITMENT_PERIOD blocks.
constexpr size_t HEADER_COMMITMENT_PERIOD{624};

//! Only feed headers to validation once this many headers on top have been
//! received and validated against commitments.
constexpr size_t REDOWNLOAD_BUFFER_SIZE{14827}; // 14827/624 = ~23.8 commitments

//! KV5 DGW examines 24 prior targets. The MTP of the oldest target can depend
//! on ten additional predecessors, so exact reproduction requires 34 indexes.
constexpr size_t DGW_LOOKBACK_BLOCKS{24};
constexpr size_t DGW_MTP_EXTRA_BLOCKS{
    CBlockIndex::nMedianTimeSpan - 1
};
constexpr size_t DGW_HISTORY_SIZE{
    DGW_LOOKBACK_BLOCKS + DGW_MTP_EXTRA_BLOCKS
};

// Our memory analysis assumes 48 bytes for a CompressedHeader, so parameters
// should be recalculated if it is compressed further.
static_assert(sizeof(CompressedHeader) == 48);

HeadersSyncState::HeadersSyncState(
    NodeId id,
    const Consensus::Params& consensus_params,
    const CBlockIndex* chain_start,
    const arith_uint256& minimum_required_work
) :
    m_commit_offset{
        FastRandomContext().randrange<unsigned>(
            HEADER_COMMITMENT_PERIOD
        )
    },
    m_id{id},
    m_consensus_params{consensus_params},
    m_chain_start{chain_start},
    m_minimum_required_work{minimum_required_work},
    m_current_chain_work{chain_start->nChainWork},
    m_last_header_received{chain_start->GetBlockHeader()},
    m_current_height{chain_start->nHeight}
{
    ResetDgwHistory(m_presync_dgw_history);

    // Estimate the number of blocks that could possibly exist on the peer's
    // chain right now using 6 blocks/second, the fastest block rate permitted
    // by the MTP rule, multiplied by the number of seconds from the last
    // allowed block until today.
    //
    // This bounds the number of commitments stored from a peer. A chain longer
    // than this cannot currently be consensus-valid.
    m_max_commitments =
        6 * (
            Ticks<std::chrono::seconds>(
                NodeClock::now() -
                NodeSeconds{
                    std::chrono::seconds{
                        chain_start->GetMedianTimePast()
                    }
                }
            ) +
            MAX_FUTURE_BLOCK_TIME
        ) /
        HEADER_COMMITMENT_PERIOD;

    LogDebug(
        BCLog::NET,
        "Initial headers sync started with peer=%d: "
        "height=%i, max_commitments=%i, min_work=%s\n",
        m_id,
        m_current_height,
        m_max_commitments,
        m_minimum_required_work.ToString()
    );
}

void HeadersSyncState::Finalize()
{
    Assume(m_download_state != State::FINAL);

    ClearShrink(m_header_commitments);
    ClearShrink(m_redownloaded_headers);

    m_last_header_received.SetNull();

    m_presync_dgw_history.clear();
    m_redownload_dgw_history.clear();

    m_redownload_buffer_last_hash.SetNull();
    m_redownload_buffer_first_prev_hash.SetNull();

    m_process_all_remaining_headers = false;
    m_current_height = 0;
    m_download_state = State::FINAL;
}

void HeadersSyncState::ResetDgwHistory(
    DgwHistory& history
) const
{
    history.clear();

    std::vector<const CBlockIndex*> ancestors;
    ancestors.reserve(DGW_HISTORY_SIZE);

    const CBlockIndex* pindex = m_chain_start;

    while (
        pindex != nullptr &&
        ancestors.size() < DGW_HISTORY_SIZE
    ) {
        ancestors.push_back(pindex);
        pindex = pindex->pprev;
    }

    std::reverse(
        ancestors.begin(),
        ancestors.end()
    );

    for (const CBlockIndex* ancestor : ancestors) {
        auto index = std::make_unique<CBlockIndex>(
            ancestor->GetBlockHeader()
        );

        index->nHeight = ancestor->nHeight;
        index->pprev =
            history.empty()
                ? nullptr
                : history.back().get();

        history.push_back(
            std::move(index)
        );
    }
}

bool HeadersSyncState::ValidateDgwHeader(
    const CBlockHeader& header,
    const int64_t next_height,
    const DgwHistory& history,
    const char* phase
) const
{
    if (history.empty()) {
        LogDebug(
            BCLog::NET,
            "Initial headers sync aborted with peer=%d: "
            "missing DGW history at height=%i (%s phase)\n",
            m_id,
            next_height,
            phase
        );

        return false;
    }

    const CBlockIndex* const previous{
        history.back().get()
    };

    if (previous->nHeight + 1 != next_height) {
        LogDebug(
            BCLog::NET,
            "Initial headers sync aborted with peer=%d: "
            "inconsistent DGW history at height=%i (%s phase)\n",
            m_id,
            next_height,
            phase
        );

        return false;
    }

    const int64_t previous_mtp{
        previous->GetMedianTimePast()
    };

    if (
        static_cast<int64_t>(header.nTime) <=
        previous_mtp
    ) {
        LogDebug(
            BCLog::NET,
            "Initial headers sync aborted with peer=%d: "
            "timestamp %u is not greater than previous MTP %i "
            "at height=%i (%s phase)\n",
            m_id,
            header.nTime,
            previous_mtp,
            next_height,
            phase
        );

        return false;
    }

    const uint32_t expected_nbits{
        GetNextWorkRequired(
            previous,
            &header,
            m_consensus_params
        )
    };

    if (header.nBits != expected_nbits) {
        LogDebug(
            BCLog::NET,
            "Initial headers sync aborted with peer=%d: "
            "invalid DGW target at height=%i, "
            "expected=%08x received=%08x (%s phase)\n",
            m_id,
            next_height,
            expected_nbits,
            header.nBits,
            phase
        );

        return false;
    }

    return true;
}

void HeadersSyncState::AppendDgwHeader(
    DgwHistory& history,
    const CBlockHeader& header,
    const int64_t height
)
{
    auto index{
        std::make_unique<CBlockIndex>(header)
    };

    index->nHeight = height;
    index->pprev =
        history.empty()
            ? nullptr
            : history.back().get();

    history.push_back(
        std::move(index)
    );

    while (
        history.size() >
        DGW_HISTORY_SIZE
    ) {
        history.pop_front();
    }

    // The oldest retained index is never required to walk farther backward
    // than the retained 34-index window.
    if (!history.empty()) {
        history.front()->pprev = nullptr;
    }
}

HeadersSyncState::ProcessingResult
HeadersSyncState::ProcessNextHeaders(
    const std::vector<CBlockHeader>& received_headers,
    const bool full_headers_message
)
{
    ProcessingResult ret;

    Assume(!received_headers.empty());

    if (received_headers.empty()) {
        return ret;
    }

    Assume(
        m_download_state != State::FINAL
    );

    if (
        m_download_state ==
        State::FINAL
    ) {
        return ret;
    }

    if (
        m_download_state ==
        State::PRESYNC
    ) {
        ret.success =
            ValidateAndStoreHeadersCommitments(
                received_headers
            );

        if (ret.success) {
            if (
                full_headers_message ||
                m_download_state ==
                    State::REDOWNLOAD
            ) {
                ret.request_more = true;
            } else {
                Assume(
                    m_download_state ==
                    State::PRESYNC
                );

                LogDebug(
                    BCLog::NET,
                    "Initial headers sync aborted with peer=%d: "
                    "incomplete headers message at height=%i "
                    "(presync phase)\n",
                    m_id,
                    m_current_height
                );
            }
        }
    } else if (
        m_download_state ==
        State::REDOWNLOAD
    ) {
        ret.success = true;

        for (
            const auto& header :
            received_headers
        ) {
            if (
                !ValidateAndStoreRedownloadedHeader(
                    header
                )
            ) {
                ret.success = false;
                break;
            }
        }

        if (ret.success) {
            ret.pow_validated_headers =
                PopHeadersReadyForAcceptance();

            if (
                m_redownloaded_headers.empty() &&
                m_process_all_remaining_headers
            ) {
                LogDebug(
                    BCLog::NET,
                    "Initial headers sync complete with peer=%d: "
                    "releasing all at height=%i "
                    "(redownload phase)\n",
                    m_id,
                    m_redownload_buffer_last_height
                );
            } else if (
                full_headers_message
            ) {
                ret.request_more = true;
            } else {
                LogDebug(
                    BCLog::NET,
                    "Initial headers sync aborted with peer=%d: "
                    "incomplete headers message at height=%i "
                    "(redownload phase)\n",
                    m_id,
                    m_redownload_buffer_last_height
                );
            }
        }
    }

    if (
        !(
            ret.success &&
            ret.request_more
        )
    ) {
        Finalize();
    }

    return ret;
}

bool HeadersSyncState::ValidateAndStoreHeadersCommitments(
    const std::vector<CBlockHeader>& headers
)
{
    Assume(headers.size() > 0);

    if (headers.empty()) {
        return true;
    }

    Assume(
        m_download_state ==
        State::PRESYNC
    );

    if (
        m_download_state !=
        State::PRESYNC
    ) {
        return false;
    }

    if (
        headers[0].hashPrevBlock !=
        m_last_header_received.GetHash()
    ) {
        LogDebug(
            BCLog::NET,
            "Initial headers sync aborted with peer=%d: "
            "non-continuous headers at height=%i "
            "(presync phase)\n",
            m_id,
            m_current_height
        );

        return false;
    }

    for (
        const auto& header :
        headers
    ) {
        if (
            !ValidateAndProcessSingleHeader(
                header
            )
        ) {
            return false;
        }
    }

    if (
        m_current_chain_work >=
        m_minimum_required_work
    ) {
        m_redownloaded_headers.clear();

        m_redownload_buffer_last_height =
            m_chain_start->nHeight;

        m_redownload_buffer_first_prev_hash =
            m_chain_start->GetBlockHash();

        m_redownload_buffer_last_hash =
            m_chain_start->GetBlockHash();

        m_redownload_chain_work =
            m_chain_start->nChainWork;

        ResetDgwHistory(
            m_redownload_dgw_history
        );

        m_download_state =
            State::REDOWNLOAD;

        LogDebug(
            BCLog::NET,
            "Initial headers sync transition with peer=%d: "
            "reached sufficient work at height=%i, "
            "redownloading from height=%i\n",
            m_id,
            m_current_height,
            m_redownload_buffer_last_height
        );
    }

    return true;
}

bool HeadersSyncState::ValidateAndProcessSingleHeader(
    const CBlockHeader& current
)
{
    Assume(
        m_download_state ==
        State::PRESYNC
    );

    if (
        m_download_state !=
        State::PRESYNC
    ) {
        return false;
    }

    const int64_t next_height{
        m_current_height + 1
    };

    // Reproduce the exact KV5 DGW calculation from a bounded rolling header
    // history. A two-target ratio check cannot validate a 24-block
    // moving-average difficulty algorithm.
    if (
        !ValidateDgwHeader(
            current,
            next_height,
            m_presync_dgw_history,
            "presync"
        )
    ) {
        return false;
    }

    if (
        next_height %
            HEADER_COMMITMENT_PERIOD ==
        m_commit_offset
    ) {
        m_header_commitments.push_back(
            m_hasher(current.GetHash()) &
            1
        );

        if (
            m_header_commitments.size() >
            m_max_commitments
        ) {
            LogDebug(
                BCLog::NET,
                "Initial headers sync aborted with peer=%d: "
                "exceeded max commitments at height=%i "
                "(presync phase)\n",
                m_id,
                next_height
            );

            return false;
        }
    }

    m_current_chain_work +=
        GetBlockProof(
            CBlockIndex{current}
        );

    AppendDgwHeader(
        m_presync_dgw_history,
        current,
        next_height
    );

    m_last_header_received = current;
    m_current_height = next_height;

    return true;
}

bool HeadersSyncState::ValidateAndStoreRedownloadedHeader(
    const CBlockHeader& header
)
{
    Assume(
        m_download_state ==
        State::REDOWNLOAD
    );

    if (
        m_download_state !=
        State::REDOWNLOAD
    ) {
        return false;
    }

    const int64_t next_height{
        m_redownload_buffer_last_height +
        1
    };

    if (
        header.hashPrevBlock !=
        m_redownload_buffer_last_hash
    ) {
        LogDebug(
            BCLog::NET,
            "Initial headers sync aborted with peer=%d: "
            "non-continuous headers at height=%i "
            "(redownload phase)\n",
            m_id,
            next_height
        );

        return false;
    }

    if (
        !ValidateDgwHeader(
            header,
            next_height,
            m_redownload_dgw_history,
            "redownload"
        )
    ) {
        return false;
    }

    m_redownload_chain_work +=
        GetBlockProof(
            CBlockIndex{header}
        );

    if (
        m_redownload_chain_work >=
        m_minimum_required_work
    ) {
        m_process_all_remaining_headers =
            true;
    }

    if (
        !m_process_all_remaining_headers &&
        next_height %
            HEADER_COMMITMENT_PERIOD ==
        m_commit_offset
    ) {
        if (
            m_header_commitments.empty()
        ) {
            LogDebug(
                BCLog::NET,
                "Initial headers sync aborted with peer=%d: "
                "commitment overrun at height=%i "
                "(redownload phase)\n",
                m_id,
                next_height
            );

            return false;
        }

        const bool commitment{
            static_cast<bool>(
                m_hasher(header.GetHash()) &
                1
            )
        };

        const bool expected_commitment{
            m_header_commitments.front()
        };

        m_header_commitments.pop_front();

        if (
            commitment !=
            expected_commitment
        ) {
            LogDebug(
                BCLog::NET,
                "Initial headers sync aborted with peer=%d: "
                "commitment mismatch at height=%i "
                "(redownload phase)\n",
                m_id,
                next_height
            );

            return false;
        }
    }

    AppendDgwHeader(
        m_redownload_dgw_history,
        header,
        next_height
    );

    m_redownloaded_headers.emplace_back(
        header
    );

    m_redownload_buffer_last_height =
        next_height;

    m_redownload_buffer_last_hash =
        header.GetHash();

    return true;
}

std::vector<CBlockHeader>
HeadersSyncState::PopHeadersReadyForAcceptance()
{
    std::vector<CBlockHeader> ret;

    Assume(
        m_download_state ==
        State::REDOWNLOAD
    );

    if (
        m_download_state !=
        State::REDOWNLOAD
    ) {
        return ret;
    }

    while (
        m_redownloaded_headers.size() >
            REDOWNLOAD_BUFFER_SIZE ||
        (
            !m_redownloaded_headers.empty() &&
            m_process_all_remaining_headers
        )
    ) {
        ret.emplace_back(
            m_redownloaded_headers
                .front()
                .GetFullHeader(
                    m_redownload_buffer_first_prev_hash
                )
        );

        m_redownloaded_headers.pop_front();

        m_redownload_buffer_first_prev_hash =
            ret.back().GetHash();
    }

    return ret;
}

CBlockLocator
HeadersSyncState::NextHeadersRequestLocator() const
{
    Assume(
        m_download_state !=
        State::FINAL
    );

    if (
        m_download_state ==
        State::FINAL
    ) {
        return {};
    }

    auto chain_start_locator{
        LocatorEntries(
            m_chain_start
        )
    };

    std::vector<uint256> locator;

    if (
        m_download_state ==
        State::PRESYNC
    ) {
        locator.push_back(
            m_last_header_received.GetHash()
        );
    }

    if (
        m_download_state ==
        State::REDOWNLOAD
    ) {
        locator.push_back(
            m_redownload_buffer_last_hash
        );
    }

    locator.insert(
        locator.end(),
        chain_start_locator.begin(),
        chain_start_locator.end()
    );

    return CBlockLocator{
        std::move(locator)
    };
}
