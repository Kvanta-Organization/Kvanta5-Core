// Copyright (c) 2015-2026 The Bitcoin Core developers
// Copyright (c) 2026 The Kvanta5 Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KVANTA5_HEADERSSYNC_H
#define KVANTA5_HEADERSSYNC_H

#include <arith_uint256.h>
#include <chain.h>
#include <consensus/params.h>
#include <net.h> // For NodeId
#include <primitives/block.h>
#include <uint256.h>
#include <util/bitdeque.h>
#include <util/hasher.h>

#include <deque>
#include <memory>
#include <vector>

// A compressed CBlockHeader, which leaves out the prevhash
struct CompressedHeader {
    // header
    int32_t nVersion{0};
    uint256 hashMerkleRoot;
    uint32_t nTime{0};
    uint32_t nBits{0};
    uint32_t nNonce{0};

    CompressedHeader()
    {
        hashMerkleRoot.SetNull();
    }

    CompressedHeader(const CBlockHeader& header)
    {
        nVersion = header.nVersion;
        hashMerkleRoot = header.hashMerkleRoot;
        nTime = header.nTime;
        nBits = header.nBits;
        nNonce = header.nNonce;
    }

    CBlockHeader GetFullHeader(const uint256& hash_prev_block)
    {
        CBlockHeader ret;
        ret.nVersion = nVersion;
        ret.hashPrevBlock = hash_prev_block;
        ret.hashMerkleRoot = hashMerkleRoot;
        ret.nTime = nTime;
        ret.nBits = nBits;
        ret.nNonce = nNonce;
        return ret;
    }
};

/** HeadersSyncState:
 *
 * We wish to download a peer's headers chain in a DoS-resistant way.
 *
 * The Kvanta5 protocol does not offer an easy way to determine the work on a
 * peer's chain. Currently, we can query a peer's headers by using a GETHEADERS
 * message, and our peer can return a set of up to 2000 headers that connect to
 * something we know. If a peer's chain has more than 2000 blocks, then we need
 * a way to verify that the chain actually has enough work on it to be useful to
 * us -- by being above our anti-DoS minimum-chain-work threshold -- before we
 * commit to storing those headers in memory. Otherwise, it would be cheap for
 * an attacker to waste all our memory by serving us low-work headers
 * (particularly for a new node coming online for the first time).
 *
 * To prevent memory-DoS with low-work headers, while still always being
 * able to reorg to whatever the most-work chain is, we require that a chain
 * meet a work threshold before committing it to memory. We can do this by
 * downloading a peer's headers twice, whenever we are not sure that the chain
 * has sufficient work:
 *
 * - In the first download phase, called pre-synchronization, we can calculate
 * the work on the chain as we go (just by checking the nBits value on each
 * header, and validating the proof-of-work).
 *
 * - Once we have reached a header where the cumulative chain work is
 * sufficient, we switch to downloading the headers a second time, this time
 * processing them fully, and possibly storing them in memory.
 *
 * To prevent an attacker from using (eg) the honest chain to convince us that
 * they have a high-work chain, but then feeding us an alternate set of
 * low-difficulty headers in the second phase, we store commitments to the
 * chain we see in the first download phase that we check in the second phase,
 * as follows:
 *
 * - In phase 1 (presync), store 1 bit (using a salted hash function) for every
 * N headers that we see. With a reasonable choice of N, this uses relatively
 * little memory even for a very long chain.
 *
 * - In phase 2 (redownload), keep a lookahead buffer and only accept headers
 * from that buffer into the block index (permanent memory usage) once they
 * have some target number of verified commitments on top of them. With this
 * parametrization, we can achieve a given security target for potential
 * permanent memory usage, while choosing N to minimize memory use during the
 * sync (temporary, per-peer storage).
 */

class HeadersSyncState {
public:
    ~HeadersSyncState() = default;

    enum class State {
        /** PRESYNC means the peer has not yet demonstrated their chain has
         * sufficient work and we're only building commitments to the chain they
         * serve us. */
        PRESYNC,

        /** REDOWNLOAD means the peer has given us a high-enough-work chain,
         * and now we're redownloading the headers we saw before and trying to
         * accept them */
        REDOWNLOAD,

        /** We're done syncing with this peer and can discard any remaining state */
        FINAL
    };

    /** Return the current state of our download */
    State GetState() const
    {
        return m_download_state;
    }

    /** Return the height reached during the PRESYNC phase */
    int64_t GetPresyncHeight() const
    {
        return m_current_height;
    }

    /** Return the block timestamp of the last header received during PRESYNC. */
    uint32_t GetPresyncTime() const
    {
        return m_last_header_received.nTime;
    }

    /** Return the amount of work in the chain received during PRESYNC. */
    arith_uint256 GetPresyncWork() const
    {
        return m_current_chain_work;
    }

    /** Construct a HeadersSyncState object representing a headers sync via this
     * download-twice mechanism.
     *
     * id: node id for logging
     * consensus_params: parameters needed for difficulty validation
     * chain_start: best known fork point that the peer's branch builds from
     * minimum_required_work: work required before accepting the chain
     */
    HeadersSyncState(
        NodeId id,
        const Consensus::Params& consensus_params,
        const CBlockIndex* chain_start,
        const arith_uint256& minimum_required_work
    );

    /** Result data structure for ProcessNextHeaders. */
    struct ProcessingResult {
        std::vector<CBlockHeader> pow_validated_headers;
        bool success{false};
        bool request_more{false};
    };

    /** Process a batch of headers after sync through this mechanism begins.
     *
     * received_headers: headers received over the network.
     *                   The caller has already checked continuity and verified
     *                   that each header satisfies its claimed PoW target, but
     *                   has not necessarily verified that the target itself is
     *                   correct under consensus rules.
     *
     * full_headers_message: true when the message was at maximum capacity.
     *
     * ProcessingResult.pow_validated_headers: headers now ready for complete
     *                   validation because the branch has sufficient work.
     *
     * ProcessingResult.success: false when an error aborts synchronization.
     *
     * ProcessingResult.request_more: true when another GETHEADERS is suggested.
     */
    ProcessingResult ProcessNextHeaders(
        const std::vector<CBlockHeader>& received_headers,
        bool full_headers_message
    );

    /** Return a locator appropriate for the current synchronization phase. */
    CBlockLocator NextHeadersRequestLocator() const;

protected:
    /** Secret offset selecting heights at which commitments are created.
     *
     * m_header_commitments entries are created at every height h for which:
     *
     *     h % HEADER_COMMITMENT_PERIOD == m_commit_offset
     */
    const unsigned m_commit_offset;

private:
    /** Rolling temporary CBlockIndex history used to reproduce KV5 DGW exactly
     * without inserting low-work headers into the global block index. */
    using DgwHistory = std::deque<std::unique_ptr<CBlockIndex>>;

    /** Clear all in-progress download state and mark this object unusable. */
    void Finalize();

    /** Reset a DGW history to m_chain_start and the ancestors required for
     * the 24-block DGW window and 11-block MTP calculations. */
    void ResetDgwHistory(DgwHistory& history) const;

    /** Verify that a candidate header has the exact nBits required by the
     * production GetNextWorkRequired() calculation and obeys the MTP rule. */
    bool ValidateDgwHeader(
        const CBlockHeader& header,
        int64_t next_height,
        const DgwHistory& history,
        const char* phase
    ) const;

    /** Append a validated header to a bounded rolling DGW history. */
    void AppendDgwHeader(
        DgwHistory& history,
        const CBlockHeader& header,
        int64_t height
    );

    /** Validate PRESYNC headers and store commitments for REDOWNLOAD. */
    bool ValidateAndStoreHeadersCommitments(
        const std::vector<CBlockHeader>& headers
    );

    /** Process one header during PRESYNC. */
    bool ValidateAndProcessSingleHeader(const CBlockHeader& current);

    /** Validate and buffer one header during REDOWNLOAD. */
    bool ValidateAndStoreRedownloadedHeader(const CBlockHeader& header);

    /** Return headers that satisfy the work and commitment requirements. */
    std::vector<CBlockHeader> PopHeadersReadyForAcceptance();

    /** NodeId of the peer, used for log messages. */
    const NodeId m_id;

    /** Consensus parameters used by anti-DoS and exact DGW validation. */
    const Consensus::Params& m_consensus_params;

    /** Last indexed block from which the peer's branch builds. */
    const CBlockIndex* m_chain_start{nullptr};

    /** Minimum work required before accepting the peer's chain. */
    const arith_uint256 m_minimum_required_work;

    /** Work observed during PRESYNC. */
    arith_uint256 m_current_chain_work;

    /** Salted hasher used for one-bit header commitments. */
    const SaltedTxidHasher m_hasher;

    /** Commitments created during PRESYNC and checked during REDOWNLOAD. */
    bitdeque<> m_header_commitments;

    /** Maximum commitment count permitted for this synchronization attempt. */
    uint64_t m_max_commitments{0};

    /** Latest header received during PRESYNC. */
    CBlockHeader m_last_header_received;

    /** Height of m_last_header_received. */
    int64_t m_current_height{0};

    /** Exact DGW/MTP history for PRESYNC. */
    DgwHistory m_presync_dgw_history;

    /** Exact DGW/MTP history for REDOWNLOAD. */
    DgwHistory m_redownload_dgw_history;

    /** Headers buffered during REDOWNLOAD until sufficient commitments have
     * been verified above them. */
    std::deque<CompressedHeader> m_redownloaded_headers;

    /** Height of the last buffered REDOWNLOAD header. */
    int64_t m_redownload_buffer_last_height{0};

    /** Hash of the last buffered REDOWNLOAD header. */
    uint256 m_redownload_buffer_last_hash;

    /** Previous hash used to reconstruct the first compressed header. */
    uint256 m_redownload_buffer_first_prev_hash;

    /** Work accumulated during REDOWNLOAD. */
    arith_uint256 m_redownload_chain_work;

    /** True after REDOWNLOAD reaches the minimum required work. */
    bool m_process_all_remaining_headers{false};

    /** Current headers synchronization state. */
    State m_download_state{State::PRESYNC};
};

#endif // KVANTA5_HEADERSSYNC_H
