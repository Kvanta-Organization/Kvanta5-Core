// Copyright (c) 2015-2026 The Bitcoin Core developers
// Copyright (c) 2026 The Kvanta5 Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <node/caches.h>

#include <common/args.h>
#include <common/system.h>
#include <index/coinstatsindex.h>
#include <index/txindex.h>
#include <kernel/caches.h>
#include <util/byte_units.h>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <optional>

static constexpr size_t STANDARD_MAX_TX_INDEX_CACHE{
    1024_MiB
};

static constexpr size_t HIGH_END_MAX_TX_INDEX_CACHE{
    4096_MiB
};

static constexpr size_t STANDARD_MAX_FILTER_INDEX_CACHE{
    512_MiB
};

static constexpr size_t HIGH_END_MAX_FILTER_INDEX_CACHE{
    2048_MiB
};

static constexpr size_t STANDARD_MAX_COINSTATS_INDEX_CACHE{
    128_MiB
};

static constexpr size_t HIGH_END_MAX_COINSTATS_INDEX_CACHE{
    512_MiB
};

namespace {

bool IsHighEndDatabaseHardware(
    const std::optional<uint64_t>& physical_memory,
    int hardware_threads
)
{
    return
        hardware_threads >=
            HIGH_END_DATABASE_MIN_THREADS &&
        physical_memory &&
        *physical_memory >=
            HIGH_END_DATABASE_MIN_MEMORY;
}

size_t AutomaticDbCacheFromMemory(
    const std::optional<uint64_t>& physical_memory,
    int hardware_threads
)
{
    if (!physical_memory) {
        return DEFAULT_DB_CACHE;
    }

    const bool high_end{
        IsHighEndDatabaseHardware(
            physical_memory,
            hardware_threads
        )
    };

    const uint64_t maximum_cache{
        high_end
            ? MAX_HIGH_END_AUTOMATIC_DB_CACHE
            : MAX_AUTOMATIC_DB_CACHE
    };

    /*
     * Stock policy:
     *
     *   25% of installed physical memory
     *   minimum 512 MiB
     *   standard ceiling 12 GiB
     *   high-end ceiling 24 GiB
     */
    const uint64_t selected{
        std::clamp<uint64_t>(
            *physical_memory / 4,
            MIN_AUTOMATIC_DB_CACHE,
            maximum_cache
        )
    };

    return static_cast<size_t>(
        std::min<uint64_t>(
            selected,
            std::numeric_limits<
                size_t
            >::max()
        )
    );
}

size_t AllocateBounded(
    size_t& remaining,
    size_t target,
    size_t maximum_fraction_denominator
)
{
    if (remaining == 0) {
        return 0;
    }

    const size_t allocation{
        std::min(
            target,
            remaining /
                maximum_fraction_denominator
        )
    };

    remaining -= allocation;

    return allocation;
}

} // namespace

namespace node {

size_t GetAutomaticDbCache()
{
    return AutomaticDbCacheFromMemory(
        GetTotalPhysicalMemory(),
        std::max(
            1,
            GetNumCores()
        )
    );
}

CacheSizes CalculateCacheSizes(
    const ArgsManager& args,
    size_t n_indexes
)
{
    const std::optional<uint64_t>
        physical_memory{
            GetTotalPhysicalMemory()
        };

    const int hardware_threads{
        std::max(
            1,
            GetNumCores()
        )
    };

    const bool high_end_hardware{
        IsHighEndDatabaseHardware(
            physical_memory,
            hardware_threads
        )
    };

    bool automatic{true};

    size_t total_cache{
        AutomaticDbCacheFromMemory(
            physical_memory,
            hardware_threads
        )
    };

    if (
        std::optional<int64_t> db_cache =
            args.GetIntArg("-dbcache")
    ) {
        automatic = false;

        if (*db_cache < 0) {
            db_cache = 0;
        }

        const uint64_t db_cache_bytes{
            SaturatingLeftShift<uint64_t>(
                *db_cache,
                20
            )
        };

        total_cache = std::max<size_t>(
            MIN_DB_CACHE,
            std::min<uint64_t>(
                db_cache_bytes,
                std::numeric_limits<
                    size_t
                >::max()
            )
        );
    }

    const bool high_end{
        high_end_hardware &&
        total_cache >=
            HIGH_END_PROFILE_MIN_DB_CACHE
    };

    const size_t original_total{
        total_cache
    };

    size_t remaining{
        total_cache
    };

    IndexCacheSizes index_sizes;

    if (
        args.GetBoolArg(
            "-txindex",
            DEFAULT_TXINDEX
        )
    ) {
        const size_t maximum{
            high_end
                ? HIGH_END_MAX_TX_INDEX_CACHE
                : STANDARD_MAX_TX_INDEX_CACHE
        };

        const size_t target{
            std::clamp(
                original_total / 8,
                64_MiB,
                maximum
            )
        };

        index_sizes.tx_index =
            AllocateBounded(
                remaining,
                target,
                4
            );
    }

    if (n_indexes > 0) {
        const size_t maximum{
            high_end
                ? HIGH_END_MAX_FILTER_INDEX_CACHE
                : STANDARD_MAX_FILTER_INDEX_CACHE
        };

        const size_t combined_target{
            std::clamp(
                original_total / 16,
                32_MiB,
                maximum
            )
        };

        const size_t combined_allocation{
            std::min(
                combined_target,
                remaining / 4
            )
        };

        index_sizes.filter_index =
            combined_allocation /
            n_indexes;

        remaining -=
            index_sizes.filter_index *
            n_indexes;
    }

    if (
        args.GetBoolArg(
            "-coinstatsindex",
            DEFAULT_COINSTATSINDEX
        )
    ) {
        const size_t maximum{
            high_end
                ? HIGH_END_MAX_COINSTATS_INDEX_CACHE
                : STANDARD_MAX_COINSTATS_INDEX_CACHE
        };

        const size_t target{
            std::clamp(
                original_total / 64,
                16_MiB,
                maximum
            )
        };

        index_sizes.coin_stats_index =
            AllocateBounded(
                remaining,
                target,
                8
            );
    }

    return {
        .index = index_sizes,
        .kernel =
            kernel::CacheSizes{
                remaining,
                high_end
            },
        .total = original_total,
        .automatic = automatic,
        .physical_memory =
            physical_memory,
        .hardware_threads =
            hardware_threads,
        .high_end = high_end,
    };
}

} // namespace node
