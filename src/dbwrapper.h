// Copyright (c) 2012-2026 The Bitcoin Core developers
// Copyright (c) 2026 The Kvanta5 Core developers
// Uses RocksDB, Copyright (c) 2011-present, Facebook, Inc. All rights reserved.
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/license/mit/.

#ifndef KVANTA5_DBWRAPPER_H
#define KVANTA5_DBWRAPPER_H

#include <attributes.h>
#include <serialize.h>
#include <span.h>
#include <streams.h>
#include <util/check.h>
#include <util/fs.h>

#include <rocksdb/slice.h>

#include <cstddef>
#include <exception>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

static constexpr size_t DBWRAPPER_PREALLOC_KEY_SIZE{64};
static constexpr size_t DBWRAPPER_PREALLOC_VALUE_SIZE{1024};
static constexpr size_t DBWRAPPER_MAX_FILE_SIZE{32U << 20};

//! RocksDB workload profile selected internally by the database caller.
enum class DBProfile {
    DEFAULT,
    BLOCK_INDEX,
    CHAINSTATE,
    TX_INDEX,
    BLOCK_FILTER_INDEX,
    COINSTATS_INDEX,
};

//! User-controlled performance and debug options.
struct DBOptions {
    //! Compact database on startup.
    bool force_compact{false};
};

//! Application-specific storage settings.
struct DBParams {
    //! Location in the filesystem where RocksDB data will be stored.
    fs::path path;
    //! Configures various RocksDB cache settings.
    size_t cache_bytes;
    //! Workload-specific RocksDB tuning selected by the caller.
    DBProfile profile{DBProfile::DEFAULT};
    //! If true, use an ephemeral RocksDB database.
    bool memory_only{false};
    //! If true, remove all existing data.
    bool wipe_data{false};
    //! If true, store data obfuscated via simple XOR. If false, XOR with a
    //! zero-filled byte array.
    bool obfuscate{false};
    //! Passed-through options.
    DBOptions options{};
};

//! Per-iterator RocksDB read behavior.
struct DBIteratorOptions {
    //! Prevent large scans from displacing useful point-read cache entries.
    bool fill_cache{false};
    //! Explicit forward readahead. Zero uses RocksDB automatic readahead.
    size_t readahead_size{0};
    //! Allow RocksDB to retain and adapt sequential-read history across files.
    bool adaptive_readahead{false};
};

class dbwrapper_error : public std::runtime_error
{
public:
    explicit dbwrapper_error(const std::string& msg) : std::runtime_error(msg) {}
};

class CDBWrapper;

/** These should be considered an implementation detail of the specific database. */
namespace dbwrapper_private {

/** Work around circular dependency, as well as for testing in dbwrapper_tests. */
const std::vector<unsigned char>& GetObfuscateKey(const CDBWrapper& wrapper);

/**
 * Non-owning deserializer for values pinned in RocksDB's block cache.
 *
 * Database obfuscation is applied while bytes are consumed, avoiding the
 * allocation, full-value copy, and second XOR pass required by DataStream.
 * The referenced RocksDB value must remain valid for this reader's lifetime.
 */
class XorSpanReader
{
private:
    Span<const unsigned char> m_data;
    Span<const unsigned char> m_key;
    size_t m_offset{0};
    bool m_xor_enabled{false};

public:
    XorSpanReader(
        Span<const unsigned char> data,
        Span<const unsigned char> key)
        : m_data{data},
          m_key{key}
    {
        for (const unsigned char byte : m_key) {
            if (byte != 0) {
                m_xor_enabled = true;
                break;
            }
        }
    }

    template <typename T>
    XorSpanReader& operator>>(T&& object)
    {
        ::Unserialize(*this, object);
        return *this;
    }

    size_t size() const
    {
        return m_data.size();
    }

    bool empty() const
    {
        return m_data.empty();
    }

    bool eof() const
    {
        return m_data.empty();
    }

    void read(
        Span<std::byte> destination)
    {
        if (destination.empty()) {
            return;
        }

        if (
            destination.size()
            > m_data.size()
        ) {
            throw std::ios_base::failure(
                "XorSpanReader::read(): "
                "end of data"
            );
        }

        if (!m_xor_enabled) {
            for (
                size_t i = 0;
                i < destination.size();
                ++i
            ) {
                destination[i] =
                    static_cast<std::byte>(
                        m_data[i]
                    );
            }
        } else {
            Assume(
                !m_key.empty()
            );

            for (
                size_t i = 0;
                i < destination.size();
                ++i
            ) {
                destination[i] =
                    static_cast<std::byte>(
                        m_data[i]
                        ^
                        m_key[
                            (
                                m_offset
                                + i
                            )
                            %
                            m_key.size()
                        ]
                    );
            }
        }

        m_data = m_data.subspan(
            destination.size()
        );

        m_offset +=
            destination.size();
    }

    void ignore(
        size_t count)
    {
        if (
            count
            > m_data.size()
        ) {
            throw std::ios_base::failure(
                "XorSpanReader::ignore(): "
                "end of data"
            );
        }

        m_data = m_data.subspan(
            count
        );

        m_offset += count;
    }
};

} // namespace dbwrapper_private

bool DestroyDB(const std::string& path_str);

/** Batch of changes queued to be written to a CDBWrapper. */
class CDBBatch
{
    friend class CDBWrapper;

private:
    const CDBWrapper& parent;

    struct WriteBatchImpl;
    const std::unique_ptr<WriteBatchImpl> m_impl_batch;

    DataStream ssKey{};
    DataStream ssValue{};

    size_t size_estimate{0};
    size_t operation_count{0};
    size_t put_count{0};
    size_t erase_count{0};

    void WriteImpl(Span<const std::byte> key, DataStream& value);
    void EraseImpl(Span<const std::byte> key);

public:
    /**
     * @param[in] parent_in       CDBWrapper that this batch is submitted to.
     * @param[in] reserved_bytes  Initial native RocksDB WriteBatch capacity.
     */
    explicit CDBBatch(const CDBWrapper& parent_in, size_t reserved_bytes = 0);
    ~CDBBatch();

    void Clear();

    template <typename K, typename V>
    void Write(const K& key, const V& value)
    {
        ssKey.reserve(DBWRAPPER_PREALLOC_KEY_SIZE);
        ssValue.reserve(DBWRAPPER_PREALLOC_VALUE_SIZE);
        ssKey << key;
        ssValue << value;
        WriteImpl(ssKey, ssValue);
        ssKey.clear();
        ssValue.clear();
    }

    template <typename K>
    void Erase(const K& key)
    {
        ssKey.reserve(DBWRAPPER_PREALLOC_KEY_SIZE);
        ssKey << key;
        EraseImpl(ssKey);
        ssKey.clear();
    }

    size_t SizeEstimate() const { return size_estimate; }
    size_t OperationCount() const { return operation_count; }
    size_t PutCount() const { return put_count; }
    size_t EraseCount() const { return erase_count; }
};

class CDBIterator
{
public:
    struct IteratorImpl;

private:
    const CDBWrapper& parent;
    const std::unique_ptr<IteratorImpl> m_impl_iter;

    void SeekImpl(Span<const std::byte> key);
    Span<const std::byte> GetKeyImpl() const;
    Span<const std::byte> GetValueImpl() const;

public:
    CDBIterator(const CDBWrapper& parent_in, std::unique_ptr<IteratorImpl> iterator_in);
    ~CDBIterator();

    bool Valid() const;
    void SeekToFirst();

    template <typename K>
    void Seek(const K& key)
    {
        DataStream ssKey{};
        ssKey.reserve(DBWRAPPER_PREALLOC_KEY_SIZE);
        ssKey << key;
        SeekImpl(ssKey);
    }

    void Next();

    template <typename K>
    bool GetKey(K& key)
    {
        try {
            const Span<const std::byte> raw_key{
                GetKeyImpl()
            };

            const Span<const unsigned char> key_bytes{
                reinterpret_cast<const unsigned char*>(
                    raw_key.data()
                ),
                raw_key.size(),
            };

            dbwrapper_private::XorSpanReader reader{
                key_bytes,
                Span<const unsigned char>{},
            };

            reader >> key;
        } catch (const std::exception&) {
            return false;
        }

        return true;
    }

    template <typename V>
    bool GetValue(V& value)
    {
        try {
            const Span<const std::byte> raw_value{
                GetValueImpl()
            };

            const Span<const unsigned char> value_bytes{
                reinterpret_cast<const unsigned char*>(
                    raw_value.data()
                ),
                raw_value.size(),
            };

            const auto& obfuscation_key{
                dbwrapper_private::GetObfuscateKey(
                    parent
                )
            };

            dbwrapper_private::XorSpanReader reader{
                value_bytes,
                Span<const unsigned char>{
                    obfuscation_key
                },
            };

            reader >> value;
        } catch (const std::exception&) {
            return false;
        }

        return true;
    }
};

struct RocksDBContext;

class CDBWrapper
{
    friend const std::vector<unsigned char>& dbwrapper_private::GetObfuscateKey(const CDBWrapper& wrapper);

private:
    //! Holds all RocksDB-specific fields of this class.
    std::unique_ptr<RocksDBContext> m_db_context;

    //! The name of this database.
    std::string m_name;

    //! A key used for optional XOR-obfuscation of the database.
    std::vector<unsigned char> obfuscate_key;

    //! The key under which the obfuscation key is stored.
    static const std::string OBFUSCATE_KEY_KEY;

    //! The length of the obfuscation key in bytes.
    static const unsigned int OBFUSCATE_KEY_NUM_BYTES;

    std::vector<unsigned char> CreateObfuscateKey() const;

    //! Path to filesystem storage.
    const fs::path m_path;

    //! Whether or not the database resides in memory.
    bool m_is_memory;

    bool ReadImpl(
        Span<const std::byte> key,
        rocksdb::PinnableSlice& value) const;
    std::vector<bool> MultiReadImpl(
        const std::vector<DataStream>& serialized_keys,
        std::vector<rocksdb::PinnableSlice>& values,
        bool sorted_input) const;
    bool ExistsImpl(Span<const std::byte> key) const;
    size_t EstimateSizeImpl(Span<const std::byte> key1, Span<const std::byte> key2) const;
    CDBIterator* NewIteratorImpl(
        std::optional<Span<const std::byte>> lower_bound,
        std::optional<Span<const std::byte>> upper_bound,
        const DBIteratorOptions& options) const;

    auto& DBContext() const LIFETIMEBOUND { return *Assert(m_db_context); }

public:
    explicit CDBWrapper(const DBParams& params);
    ~CDBWrapper();

    CDBWrapper(const CDBWrapper&) = delete;
    CDBWrapper& operator=(const CDBWrapper&) = delete;

    template <typename K, typename V>
    bool Read(const K& key, V& value) const
    {
        DataStream ssKey{};
        ssKey.reserve(DBWRAPPER_PREALLOC_KEY_SIZE);
        ssKey << key;

        rocksdb::PinnableSlice pinned_value;
        if (!ReadImpl(ssKey, pinned_value)) {
            return false;
        }

        try {
            const Span<const unsigned char> bytes{
                reinterpret_cast<const unsigned char*>(
                    pinned_value.data()
                ),
                pinned_value.size(),
            };

            dbwrapper_private::XorSpanReader reader{
                bytes,
                Span<const unsigned char>{
                    obfuscate_key
                },
            };

            reader >> value;
        } catch (const std::exception&) {
            return false;
        }

        return true;
    }

    /**
     * Read many serialized keys using RocksDB's native batched MultiGet path.
     * Missing or undecodable entries are returned as std::nullopt.
     */
    template <typename K, typename V>
    std::vector<std::optional<V>> MultiRead(
        Span<const K> keys,
        bool sorted_input = false) const
    {
        if (keys.empty()) {
            return {};
        }

        std::vector<DataStream> serialized_keys;
        serialized_keys.reserve(keys.size());

        for (const K& key : keys) {
            DataStream serialized{};
            serialized.reserve(DBWRAPPER_PREALLOC_KEY_SIZE);
            serialized << key;
            serialized_keys.emplace_back(std::move(serialized));
        }

        std::vector<rocksdb::PinnableSlice> pinned_values(keys.size());
        const std::vector<bool> found{
            MultiReadImpl(serialized_keys, pinned_values, sorted_input)
        };
        std::vector<std::optional<V>> values(keys.size());

        for (size_t i = 0; i < pinned_values.size(); ++i) {
            if (!found[i]) {
                continue;
            }

            try {
                const Span<const unsigned char> bytes{
                    reinterpret_cast<const unsigned char*>(
                        pinned_values[i].data()
                    ),
                    pinned_values[i].size(),
                };

                dbwrapper_private::XorSpanReader reader{
                    bytes,
                    Span<const unsigned char>{
                        obfuscate_key
                    },
                };

                V value{};
                reader >> value;

                values[i].emplace(
                    std::move(value)
                );
            } catch (const std::exception&) {
                values[i].reset();
            }
        }

        return values;
    }

    template <typename K, typename V>
    std::vector<std::optional<V>> MultiRead(
        const std::vector<K>& keys,
        bool sorted_input = false) const
    {
        return MultiRead<K, V>(Span<const K>{keys}, sorted_input);
    }

    template <typename K, typename V>
    bool Write(const K& key, const V& value, bool sync = false)
    {
        CDBBatch batch(*this);
        batch.Write(key, value);
        return WriteBatch(batch, sync);
    }

    //! @returns filesystem path to the on-disk data.
    std::optional<fs::path> StoragePath()
    {
        if (m_is_memory) {
            return {};
        }
        return m_path;
    }

    template <typename K>
    bool Exists(const K& key) const
    {
        DataStream ssKey{};
        ssKey.reserve(DBWRAPPER_PREALLOC_KEY_SIZE);
        ssKey << key;
        return ExistsImpl(ssKey);
    }

    template <typename K>
    bool Erase(const K& key, bool sync = false)
    {
        CDBBatch batch(*this);
        batch.Erase(key);
        return WriteBatch(batch, sync);
    }

    bool WriteBatch(CDBBatch& batch, bool sync = false);

    //! Get an estimate of RocksDB memory usage in bytes.
    size_t DynamicMemoryUsage() const;

    CDBIterator* NewIterator(const DBIteratorOptions& options = {}) const;

    template <typename K>
    CDBIterator* NewIterator(
        const K& lower_bound,
        const K& upper_bound,
        const DBIteratorOptions& options = {}) const
    {
        DataStream lower{};
        DataStream upper{};
        lower.reserve(DBWRAPPER_PREALLOC_KEY_SIZE);
        upper.reserve(DBWRAPPER_PREALLOC_KEY_SIZE);
        lower << lower_bound;
        upper << upper_bound;

        return NewIteratorImpl(
            Span<const std::byte>{lower},
            Span<const std::byte>{upper},
            options);
    }

    /** Return true if the database contains no entries. */
    bool IsEmpty();

    template <typename K>
    size_t EstimateSize(const K& key_begin, const K& key_end) const
    {
        DataStream ssKey1{};
        DataStream ssKey2{};
        ssKey1.reserve(DBWRAPPER_PREALLOC_KEY_SIZE);
        ssKey2.reserve(DBWRAPPER_PREALLOC_KEY_SIZE);
        ssKey1 << key_begin;
        ssKey2 << key_end;
        return EstimateSizeImpl(ssKey1, ssKey2);
    }
};

#endif // KVANTA5_DBWRAPPER_H
