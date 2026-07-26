// Copyright (c) 2012-2026 The Bitcoin Core developers
// Copyright (c) 2026 The Kvanta5 Core developers
// Uses RocksDB, Copyright (c) 2011-present, Facebook, Inc. All rights reserved.
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/license/mit/.

#include <dbwrapper.h>

#include <common/system.h>
#include <logging.h>
#include <random.h>
#include <serialize.h>
#include <span.h>
#include <streams.h>
#include <util/fs.h>
#include <util/fs_helpers.h>
#include <util/strencodings.h>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <utility>

#include <rocksdb/cache.h>
#include <rocksdb/db.h>
#include <rocksdb/filter_policy.h>
#include <rocksdb/iterator.h>
#include <rocksdb/options.h>
#include <rocksdb/slice.h>
#include <rocksdb/status.h>
#include <rocksdb/table.h>
#include <rocksdb/write_batch.h>

static auto CharCast(const std::byte* data)
{
    return reinterpret_cast<const char*>(data);
}

bool DestroyDB(const std::string& path_str)
{
    const rocksdb::Status status{
        rocksdb::DestroyDB(
            path_str,
            rocksdb::Options{}
        )
    };

    return status.ok() || status.IsNotFound();
}

/** Handle a database error by throwing dbwrapper_error. */
static void HandleError(const rocksdb::Status& status)
{
    if (status.ok()) {
        return;
    }

    const std::string errmsg{
        "Fatal RocksDB error: " + status.ToString()
    };

    LogPrintf("%s\n", errmsg);

    throw dbwrapper_error(errmsg);
}

static void SetMaxOpenFiles(
    rocksdb::Options& options
)
{
    options.max_open_files =
        sizeof(void*) < 8
            ? 64
            : 1000;
}

struct RocksDBTuning {
    const char* profile_name{
        "default"
    };

    const char* hardware_class{
        "standard"
    };

    size_t effective_cache{0};
    size_t block_cache_size{0};
    size_t write_buffer_size{0};

    size_t block_size{
        8U << 10
    };

    double bloom_bits_per_key{
        8.0
    };

    bool use_bloom_filter{
        true
    };

    int max_write_buffers{
        2
    };

    int merge_write_buffers{
        1
    };

    int background_jobs{
        2
    };

    uint32_t subcompactions{
        1
    };
};

static bool IsHighEndRocksDBHardware(
    int cores
)
{
    constexpr uint64_t HIGH_END_MEMORY_BYTES{
        48ULL * 1024ULL * 1024ULL * 1024ULL
    };

    const std::optional<uint64_t>
        physical_memory{
            GetTotalPhysicalMemory()
        };

    return
        cores >= 24 &&
        physical_memory &&
        *physical_memory >=
            HIGH_END_MEMORY_BYTES;
}

static bool HasHighEndRocksDBBudget(
    DBProfile profile,
    size_t cache_size_bytes
)
{
    constexpr size_t MiB{
        1024U * 1024U
    };

    switch (profile) {
    case DBProfile::BLOCK_INDEX:
        return cache_size_bytes >=
            128U * MiB;

    case DBProfile::CHAINSTATE:
        return cache_size_bytes >=
            768U * MiB;

    case DBProfile::TX_INDEX:
        return cache_size_bytes >=
            1536U * MiB;

    case DBProfile::BLOCK_FILTER_INDEX:
        return cache_size_bytes >=
            512U * MiB;

    case DBProfile::COINSTATS_INDEX:
        return cache_size_bytes >=
            192U * MiB;

    case DBProfile::DEFAULT:
        return false;
    }

    return false;
}

static RocksDBTuning GetTuning(
    size_t cache_size_bytes,
    DBProfile profile
)
{
    RocksDBTuning tuning;

    tuning.effective_cache =
        std::max<size_t>(
            cache_size_bytes,
            1U << 20
        );

    const int cores{
        std::max(
            1,
            GetNumCores()
        )
    };

    const bool high_end{
        IsHighEndRocksDBHardware(
            cores
        ) &&
        HasHighEndRocksDBBudget(
            profile,
            cache_size_bytes
        )
    };

    if (high_end) {
        tuning.hardware_class =
            "high-end";
    }

    size_t write_buffer_divisor{
        4
    };

    switch (profile) {
    case DBProfile::CHAINSTATE:
        tuning.profile_name =
            "chainstate";

        tuning.block_size =
            4U << 10;

        tuning.bloom_bits_per_key =
            10.0;

        tuning.max_write_buffers =
            4;

        tuning.merge_write_buffers =
            2;

        tuning.background_jobs =
            std::clamp(
                cores / 2,
                2,
                4
            );

        tuning.subcompactions =
            cores >= 8
                ? 2
                : 1;

        write_buffer_divisor =
            8;

        if (high_end) {
            tuning.max_write_buffers =
                8;

            tuning.merge_write_buffers =
                2;

            tuning.background_jobs =
                std::clamp(
                    cores / 4,
                    6,
                    8
                );

            tuning.subcompactions =
                static_cast<uint32_t>(
                    std::clamp(
                        cores / 8,
                        2,
                        4
                    )
                );

            write_buffer_divisor =
                16;
        }

        break;

    case DBProfile::TX_INDEX:
        tuning.profile_name =
            "txindex";

        tuning.block_size =
            8U << 10;

        tuning.bloom_bits_per_key =
            10.0;

        tuning.max_write_buffers =
            4;

        tuning.merge_write_buffers =
            2;

        tuning.background_jobs =
            std::clamp(
                cores / 3,
                2,
                3
            );

        tuning.subcompactions =
            1;

        write_buffer_divisor =
            8;

        if (high_end) {
            tuning.max_write_buffers =
                8;

            tuning.merge_write_buffers =
                2;

            tuning.background_jobs =
                std::clamp(
                    cores / 6,
                    4,
                    6
                );

            tuning.subcompactions =
                cores >= 32
                    ? 2
                    : 1;

            write_buffer_divisor =
                16;
        }

        break;

    case DBProfile::BLOCK_FILTER_INDEX:
        tuning.profile_name =
            "block-filter-index";

        tuning.block_size =
            16U << 10;

        tuning.bloom_bits_per_key =
            8.0;

        tuning.max_write_buffers =
            3;

        tuning.merge_write_buffers =
            1;

        tuning.background_jobs =
            2;

        tuning.subcompactions =
            1;

        write_buffer_divisor =
            8;

        if (high_end) {
            tuning.max_write_buffers =
                6;

            tuning.merge_write_buffers =
                2;

            tuning.background_jobs =
                std::clamp(
                    cores / 8,
                    3,
                    4
                );

            tuning.subcompactions =
                2;

            write_buffer_divisor =
                16;
        }

        break;

    case DBProfile::COINSTATS_INDEX:
        tuning.profile_name =
            "coinstats-index";

        tuning.block_size =
            8U << 10;

        tuning.bloom_bits_per_key =
            8.0;

        tuning.max_write_buffers =
            3;

        tuning.merge_write_buffers =
            1;

        tuning.background_jobs =
            2;

        tuning.subcompactions =
            1;

        write_buffer_divisor =
            8;

        if (high_end) {
            tuning.max_write_buffers =
                6;

            tuning.merge_write_buffers =
                2;

            tuning.background_jobs =
                std::clamp(
                    cores / 8,
                    3,
                    4
                );

            tuning.subcompactions =
                2;

            write_buffer_divisor =
                16;
        }

        break;

    case DBProfile::BLOCK_INDEX:
        tuning.profile_name =
            "block-index";

        if (high_end) {
            tuning.max_write_buffers =
                4;

            tuning.merge_write_buffers =
                1;

            tuning.background_jobs =
                3;

            tuning.subcompactions =
                1;

            write_buffer_divisor =
                8;
        }

        break;

    case DBProfile::DEFAULT:
        break;
    }

    tuning.block_cache_size =
        std::max<size_t>(
            tuning.effective_cache / 2,
            256U << 10
        );

    tuning.write_buffer_size =
        std::max<size_t>(
            tuning.effective_cache /
                write_buffer_divisor,
            64U << 10
        );

    return tuning;
}

static rocksdb::Options GetOptions(
    const RocksDBTuning& tuning
)
{
    rocksdb::BlockBasedTableOptions
        table_options;

    table_options.block_cache =
        rocksdb::NewLRUCache(
            tuning.block_cache_size,
            /*num_shard_bits=*/-1,
            /*strict_capacity_limit=*/false,
            /*high_pri_pool_ratio=*/0.20
        );

    if (tuning.use_bloom_filter) {
        table_options
            .filter_policy
            .reset(
                rocksdb::
                    NewBloomFilterPolicy(
                        tuning
                            .bloom_bits_per_key,
                        false
                    )
            );
    }

    table_options.block_size =
        tuning.block_size;

    table_options
        .cache_index_and_filter_blocks =
            true;

    table_options
        .cache_index_and_filter_blocks_with_high_priority =
            true;

    table_options
        .pin_l0_filter_and_index_blocks_in_cache =
            true;

    table_options
        .optimize_filters_for_memory =
            true;

    table_options.checksum =
        rocksdb::kCRC32c;

    rocksdb::Options options;

    options.create_if_missing =
        true;

    options.paranoid_checks =
        true;

    options.compression =
        rocksdb::kLZ4Compression;

    options.bottommost_compression =
        rocksdb::kLZ4Compression;

    options.compaction_style =
        rocksdb::kCompactionStyleLevel;

    options
        .level_compaction_dynamic_level_bytes =
            true;

    options.write_buffer_size =
        tuning.write_buffer_size;

    options.max_write_buffer_number =
        tuning.max_write_buffers;

    options
        .min_write_buffer_number_to_merge =
            tuning.merge_write_buffers;

    options.target_file_size_base =
        std::max<uint64_t>(
            options.target_file_size_base,
            DBWRAPPER_MAX_FILE_SIZE
        );

    options.max_background_jobs =
        tuning.background_jobs;

    options.max_subcompactions =
        tuning.subcompactions;

    options.bytes_per_sync =
        1U << 20;

    options.wal_bytes_per_sync =
        1U << 20;

    options.keep_log_file_num =
        2;

    options.max_log_file_size =
        1U << 20;

    SetMaxOpenFiles(options);

    options.table_factory.reset(
        rocksdb::
            NewBlockBasedTableFactory(
                table_options
            )
    );

    return options;
}

struct CDBBatch::WriteBatchImpl {
    explicit WriteBatchImpl(size_t reserved_bytes)
        : batch{reserved_bytes}
    {
    }

    rocksdb::WriteBatch batch;
};

CDBBatch::CDBBatch(
    const CDBWrapper& parent_in,
    size_t reserved_bytes)
    : parent{parent_in},
      m_impl_batch{
          std::make_unique<CDBBatch::WriteBatchImpl>(reserved_bytes)
      }
{
}

CDBBatch::~CDBBatch() = default;

void CDBBatch::Clear()
{
    m_impl_batch->batch.Clear();
    size_estimate = 0;
    operation_count = 0;
    put_count = 0;
    erase_count = 0;
}

void CDBBatch::WriteImpl(
    Span<const std::byte> key,
    DataStream& value)
{
    const rocksdb::Slice db_key{
        CharCast(key.data()),
        key.size()
    };

    value.Xor(
        dbwrapper_private::GetObfuscateKey(parent)
    );

    const rocksdb::Slice db_value{
        CharCast(value.data()),
        value.size()
    };

    HandleError(
        m_impl_batch->batch.Put(
            db_key,
            db_value
        )
    );

    ++operation_count;
    ++put_count;
    size_estimate =
        m_impl_batch->batch.GetDataSize();
}

void CDBBatch::EraseImpl(
    Span<const std::byte> key)
{
    const rocksdb::Slice db_key{
        CharCast(key.data()),
        key.size()
    };

    HandleError(
        m_impl_batch->batch.Delete(db_key)
    );

    ++operation_count;
    ++erase_count;
    size_estimate =
        m_impl_batch->batch.GetDataSize();
}

/*
 * The name remains RocksDBContext temporarily because dbwrapper.h is
 * otherwise unchanged for this first compile. Its contents are entirely
 * RocksDB-specific.
 */
struct RocksDBContext {
    rocksdb::Options options;

    rocksdb::ReadOptions read_options;
    rocksdb::ReadOptions iterator_options;

    rocksdb::WriteOptions write_options;
    rocksdb::WriteOptions sync_options;

    std::unique_ptr<rocksdb::DB> db;

    bool delete_on_close{false};
};

CDBWrapper::CDBWrapper(const DBParams& params)
    : m_db_context{
          std::make_unique<RocksDBContext>()
      },
      m_name{
          fs::PathToString(
              params.path.stem()
          )
      },
      m_path{params.path},
      m_is_memory{params.memory_only}
{
    DBContext().read_options.verify_checksums = true;

    DBContext().iterator_options = DBContext().read_options;
    DBContext().iterator_options.fill_cache = false;

    /*
     * WAL remains enabled for both normal and synchronous writes.
     *
     * fSync controls whether RocksDB waits for the WAL to reach durable
     * storage before returning.
     */
    DBContext().write_options.disableWAL = false;

    DBContext().sync_options.disableWAL = false;
    DBContext().sync_options.sync = true;

    const RocksDBTuning tuning{
        GetTuning(
            params.cache_bytes,
            params.profile
        )
    };

    DBContext().options =
        GetOptions(tuning);

    LogDebug(
        BCLog::ROCKSDB,
        "RocksDB profile: "
        "db=%s profile=%s class=%s "
        "budget=%.1fMiB "
        "block_cache=%.1fMiB "
        "write_buffer=%.1fMiB "
        "write_buffers=%d "
        "merge_buffers=%d "
        "background_jobs=%d "
        "subcompactions=%u "
        "block_size=%uKiB "
        "bloom=%s "
        "compression=LZ4 "
        "bottommost=ZSTD "
        "max_open_files=%d\n",
        m_name,
        tuning.profile_name,
        tuning.hardware_class,
        tuning.effective_cache /
            1048576.0,
        tuning.block_cache_size /
            1048576.0,
        tuning.write_buffer_size /
            1048576.0,
        tuning.max_write_buffers,
        tuning.merge_write_buffers,
        tuning.background_jobs,
        tuning.subcompactions,
        static_cast<unsigned>(
            tuning.block_size >> 10
        ),
        tuning.use_bloom_filter
            ? "enabled"
            : "disabled",
        DBContext()
            .options
            .max_open_files
    );

    DBContext().delete_on_close =
        params.memory_only;

    const std::string path{
        fs::PathToString(params.path)
    };

    /*
     * RocksDB 10.10.1 does not install its optional in-memory Env header
     * as part of the selected static build.
     *
     * For this first port, memory_only uses an ephemeral on-disk RocksDB
     * database which is destroyed when the wrapper closes. This preserves
     * isolation and empty-on-open semantics for tests without affecting
     * normal node databases.
     */
    if (
        (params.memory_only || params.wipe_data) &&
        fs::exists(params.path)
    ) {
        LogPrintf(
            "Wiping RocksDB in %s\n",
            path
        );

        HandleError(
            rocksdb::DestroyDB(
                path,
                DBContext().options
            )
        );
    }

    TryCreateDirectories(params.path);

    LogPrintf(
        "Opening RocksDB in %s%s\n",
        path,
        params.memory_only
            ? " (ephemeral)"
            : ""
    );

    rocksdb::DB* raw_db{nullptr};

    HandleError(
        rocksdb::DB::Open(
            DBContext().options,
            path,
            &raw_db
        )
    );

    DBContext().db.reset(raw_db);

    LogPrintf(
        "Opened RocksDB successfully\n"
    );

    if (params.options.force_compact) {
        LogPrintf(
            "Starting database compaction of %s\n",
            path
        );

        rocksdb::CompactRangeOptions
            compact_options;

        HandleError(
            DBContext().db->CompactRange(
                compact_options,
                nullptr,
                nullptr
            )
        );

        LogPrintf(
            "Finished database compaction of %s\n",
            path
        );
    }

    /*
     * The base-case obfuscation key is a no-op.
     */
    obfuscate_key =
        std::vector<unsigned char>(
            OBFUSCATE_KEY_NUM_BYTES,
            '\000'
        );

    const bool key_exists{
        Read(
            OBFUSCATE_KEY_KEY,
            obfuscate_key
        )
    };

    if (
        !key_exists &&
        params.obfuscate &&
        IsEmpty()
    ) {
        std::vector<unsigned char> new_key{
            CreateObfuscateKey()
        };

        /*
         * Write the key before enabling it so the key is not XORed
         * with itself.
         */
        Write(
            OBFUSCATE_KEY_KEY,
            new_key
        );

        obfuscate_key = new_key;

        LogPrintf(
            "Wrote new obfuscate key for %s: %s\n",
            path,
            HexStr(obfuscate_key)
        );
    }

    LogPrintf(
        "Using obfuscation key for %s: %s\n",
        path,
        HexStr(obfuscate_key)
    );
}

CDBWrapper::~CDBWrapper()
{
    if (!m_db_context) {
        return;
    }

    const bool delete_on_close{
        DBContext().delete_on_close
    };

    const std::string path{
        fs::PathToString(m_path)
    };

    /*
     * Close the DB before attempting DestroyDB so RocksDB releases
     * its LOCK file and file handles.
     */
    DBContext().db.reset();

    if (delete_on_close) {
        const rocksdb::Status status{
            rocksdb::DestroyDB(
                path,
                DBContext().options
            )
        };

        /*
         * Destructors must not throw.
         */
        if (
            !status.ok() &&
            !status.IsNotFound()
        ) {
            LogPrintf(
                "Failed to destroy ephemeral RocksDB at %s: %s\n",
                path,
                status.ToString()
            );
        }
    }
}

bool CDBWrapper::WriteBatch(
    CDBBatch& batch,
    bool sync
)
{
    /*
     * Retain the RocksDB logging category until the logging
     * category enum and user-facing debug option are migrated separately.
     */
    const bool log_memory{
        LogAcceptCategory(
            BCLog::ROCKSDB,
            BCLog::Level::Debug
        )
    };

    double memory_before{0};

    if (log_memory) {
        memory_before =
            DynamicMemoryUsage() /
            1024.0 /
            1024.0;
    }

    HandleError(
        DBContext().db->Write(
            sync
                ? DBContext().sync_options
                : DBContext().write_options,
            &batch.m_impl_batch->batch
        )
    );

    if (log_memory) {
        const double memory_after{
            DynamicMemoryUsage() /
            1024.0 /
            1024.0
        };

        LogDebug(
            BCLog::ROCKSDB,
            "WriteBatch: "
            "db=%s bytes=%u operations=%u puts=%u erases=%u "
            "memory_before=%.1fMiB memory_after=%.1fMiB\n",
            m_name,
            static_cast<unsigned>(batch.SizeEstimate()),
            static_cast<unsigned>(batch.OperationCount()),
            static_cast<unsigned>(batch.PutCount()),
            static_cast<unsigned>(batch.EraseCount()),
            memory_before,
            memory_after
        );
    }

    return true;
}

size_t CDBWrapper::DynamicMemoryUsage() const
{
    static constexpr const char*
        MEMORY_PROPERTIES[] = {
            "rocksdb.block-cache-usage",
            "rocksdb.cur-size-all-mem-tables",
            "rocksdb.estimate-table-readers-mem",
        };

    uint64_t total{0};
    bool found_property{false};

    for (
        const char* property_name :
        MEMORY_PROPERTIES
    ) {
        uint64_t property_value{0};

        if (
            DBContext().db->GetIntProperty(
                property_name,
                &property_value
            )
        ) {
            total += property_value;
            found_property = true;
        }
    }

    if (!found_property) {
        LogDebug(
            BCLog::ROCKSDB,
            "Failed to obtain RocksDB "
            "memory-usage properties\n"
        );

        return 0;
    }

    return static_cast<size_t>(
        std::min<uint64_t>(
            total,
            std::numeric_limits<size_t>::max()
        )
    );
}

/*
 * Prefixed with a null character to avoid collisions with other keys.
 *
 * The explicit string length preserves bytes after the null terminator.
 */
const std::string CDBWrapper::OBFUSCATE_KEY_KEY(
    "\000obfuscate_key",
    14
);

const unsigned int
    CDBWrapper::OBFUSCATE_KEY_NUM_BYTES = 8;

std::vector<unsigned char>
CDBWrapper::CreateObfuscateKey() const
{
    std::vector<uint8_t> key(
        OBFUSCATE_KEY_NUM_BYTES
    );

    GetRandBytes(key);

    return key;
}

bool CDBWrapper::ReadImpl(
    Span<const std::byte> key,
    rocksdb::PinnableSlice& value
) const
{
    const rocksdb::Slice db_key{
        CharCast(key.data()),
        key.size()
    };

    value.Reset();

    const rocksdb::Status status{
        DBContext().db->Get(
            DBContext().read_options,
            DBContext().db->DefaultColumnFamily(),
            db_key,
            &value
        )
    };

    if (status.IsNotFound()) {
        return false;
    }

    if (!status.ok()) {
        LogPrintf(
            "RocksDB read failure: %s\n",
            status.ToString()
        );
        HandleError(status);
    }

    return true;
}

std::vector<bool> CDBWrapper::MultiReadImpl(
    const std::vector<DataStream>& serialized_keys,
    std::vector<rocksdb::PinnableSlice>& values,
    bool sorted_input
) const
{
    const size_t count{serialized_keys.size()};

    std::vector<rocksdb::Slice> keys;
    keys.reserve(count);

    for (const DataStream& key : serialized_keys) {
        keys.emplace_back(
            CharCast(key.data()),
            key.size()
        );
    }

    if (values.size() != count) {
        throw std::logic_error(
            "RocksDB MultiRead value array has an invalid size");
    }

    std::vector<rocksdb::Status> statuses(count);

    DBContext().db->MultiGet(
        DBContext().read_options,
        DBContext().db->DefaultColumnFamily(),
        count,
        keys.data(),
        values.data(),
        statuses.data(),
        sorted_input
    );

    std::vector<bool> found(count, false);

    for (size_t i = 0; i < count; ++i) {
        if (statuses[i].IsNotFound()) {
            continue;
        }

        if (!statuses[i].ok()) {
            LogPrintf(
                "RocksDB MultiGet failure: %s\n",
                statuses[i].ToString()
            );
            HandleError(statuses[i]);
        }

        found[i] = true;
    }

    return found;
}

bool CDBWrapper::ExistsImpl(
    Span<const std::byte> key
) const
{
    const rocksdb::Slice db_key{
        CharCast(key.data()),
        key.size()
    };

    rocksdb::PinnableSlice value;

    const rocksdb::Status status{
        DBContext().db->Get(
            DBContext().read_options,
            DBContext().db->DefaultColumnFamily(),
            db_key,
            &value
        )
    };

    if (status.IsNotFound()) {
        return false;
    }

    if (!status.ok()) {
        LogPrintf(
            "RocksDB existence-check failure: %s\n",
            status.ToString()
        );
        HandleError(status);
    }

    return true;
}

size_t CDBWrapper::EstimateSizeImpl(
    Span<const std::byte> key_begin,
    Span<const std::byte> key_end
) const
{
    const rocksdb::Slice begin{
        CharCast(key_begin.data()),
        key_begin.size()
    };

    const rocksdb::Slice end{
        CharCast(key_end.data()),
        key_end.size()
    };

    const rocksdb::Range range{
        begin,
        end
    };

    uint64_t size{0};

    DBContext().db->GetApproximateSizes(
        &range,
        1,
        &size
    );

    return static_cast<size_t>(
        std::min<uint64_t>(
            size,
            std::numeric_limits<size_t>::max()
        )
    );
}

bool CDBWrapper::IsEmpty()
{
    std::unique_ptr<CDBIterator> iterator{
        NewIterator()
    };

    iterator->SeekToFirst();

    return !iterator->Valid();
}

struct CDBIterator::IteratorImpl {
    std::optional<std::string> lower_bound_storage;
    std::optional<std::string> upper_bound_storage;
    std::optional<rocksdb::Slice> lower_bound;
    std::optional<rocksdb::Slice> upper_bound;
    rocksdb::ReadOptions read_options;
    std::unique_ptr<rocksdb::Iterator> iterator;

    IteratorImpl(
        rocksdb::DB& db,
        const rocksdb::ReadOptions& base_options,
        std::optional<Span<const std::byte>> lower,
        std::optional<Span<const std::byte>> upper,
        const DBIteratorOptions& options)
        : read_options{base_options}
    {
        read_options.fill_cache = options.fill_cache;
        read_options.readahead_size = options.readahead_size;
        read_options.adaptive_readahead = options.adaptive_readahead;

        if (lower) {
            lower_bound_storage.emplace(
                CharCast(lower->data()),
                lower->size()
            );
            lower_bound.emplace(*lower_bound_storage);
            read_options.iterate_lower_bound = &*lower_bound;
        }

        if (upper) {
            upper_bound_storage.emplace(
                CharCast(upper->data()),
                upper->size()
            );
            upper_bound.emplace(*upper_bound_storage);
            read_options.iterate_upper_bound = &*upper_bound;
        }

        iterator.reset(
            db.NewIterator(read_options)
        );
    }
};

CDBIterator::CDBIterator(
    const CDBWrapper& parent_in,
    std::unique_ptr<IteratorImpl> iterator_in
)
    : parent{parent_in},
      m_impl_iter{std::move(iterator_in)}
{
}

CDBIterator* CDBWrapper::NewIterator(
    const DBIteratorOptions& options) const
{
    return NewIteratorImpl(
        std::nullopt,
        std::nullopt,
        options
    );
}

CDBIterator* CDBWrapper::NewIteratorImpl(
    std::optional<Span<const std::byte>> lower_bound,
    std::optional<Span<const std::byte>> upper_bound,
    const DBIteratorOptions& options) const
{
    return new CDBIterator{
        *this,
        std::make_unique<CDBIterator::IteratorImpl>(
            *DBContext().db,
            DBContext().iterator_options,
            lower_bound,
            upper_bound,
            options
        )
    };
}

void CDBIterator::SeekImpl(
    Span<const std::byte> key
)
{
    const rocksdb::Slice db_key{
        CharCast(key.data()),
        key.size()
    };

    m_impl_iter->iterator->Seek(db_key);
}

Span<const std::byte>
CDBIterator::GetKeyImpl() const
{
    const rocksdb::Slice key{
        m_impl_iter->iterator->key()
    };

    return {
        reinterpret_cast<const std::byte*>(
            key.data()
        ),
        key.size()
    };
}

Span<const std::byte>
CDBIterator::GetValueImpl() const
{
    const rocksdb::Slice value{
        m_impl_iter->iterator->value()
    };

    return {
        reinterpret_cast<const std::byte*>(
            value.data()
        ),
        value.size()
    };
}

CDBIterator::~CDBIterator() = default;

bool CDBIterator::Valid() const
{
    if (m_impl_iter->iterator->Valid()) {
        return true;
    }

    HandleError(
        m_impl_iter->iterator->status()
    );

    return false;
}

void CDBIterator::SeekToFirst()
{
    if (m_impl_iter->lower_bound) {
        m_impl_iter->iterator->Seek(
            *m_impl_iter->lower_bound
        );
    } else {
        m_impl_iter->iterator->SeekToFirst();
    }
}

void CDBIterator::Next()
{
    m_impl_iter->iterator->Next();
}

namespace dbwrapper_private {

const std::vector<unsigned char>&
GetObfuscateKey(
    const CDBWrapper& wrapper
)
{
    return wrapper.obfuscate_key;
}

} // namespace dbwrapper_private
