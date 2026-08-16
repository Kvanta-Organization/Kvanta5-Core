// Copyright (c) 2026 The Kvanta5 Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.

#include <wallet/pqkey.h>

#include <hash.h>
#include <test/util/setup_common.h>

#include <boost/test/unit_test.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iterator>
#include <limits>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

BOOST_FIXTURE_TEST_SUITE(pqkey_entropy_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(cpqkey_production_rng_concurrent_smoke)
{
    static constexpr size_t THREADS = 8;
    static constexpr size_t KEYS_PER_THREAD = 128;
    static constexpr size_t TOTAL_KEYS = THREADS * KEYS_PER_THREAD;

    std::mutex merge_mutex;
    std::set<std::array<unsigned char, MLDSA87_SEED_SIZE>> all_seeds;
    std::array<uint64_t, MLDSA87_SEED_SIZE * 8> total_ones{};
    std::atomic<size_t> failures{0};

    auto worker = [&]() {
        std::vector<std::array<unsigned char, MLDSA87_SEED_SIZE>> local_seeds;
        local_seeds.reserve(KEYS_PER_THREAD);
        std::array<uint64_t, MLDSA87_SEED_SIZE * 8> local_ones{};

        for (size_t n = 0; n < KEYS_PER_THREAD; ++n) {
            CPQKey key;

            if (!key.MakeNewKey()) {
                ++failures;
                continue;
            }

            const auto& seed = key.GetSeedBytes();

            if (seed.size() != MLDSA87_SEED_SIZE) {
                ++failures;
                continue;
            }

            std::array<unsigned char, MLDSA87_SEED_SIZE> copy{};
            std::copy(seed.begin(), seed.end(), copy.begin());
            local_seeds.push_back(copy);

            for (size_t byte = 0; byte < MLDSA87_SEED_SIZE; ++byte) {
                for (size_t bit = 0; bit < 8; ++bit) {
                    if ((seed[byte] >> bit) & 1U) {
                        ++local_ones[byte * 8 + bit];
                    }
                }
            }
        }

        std::lock_guard<std::mutex> lock(merge_mutex);

        for (const auto& seed : local_seeds) {
            all_seeds.insert(seed);
        }

        for (size_t bit = 0; bit < total_ones.size(); ++bit) {
            total_ones[bit] += local_ones[bit];
        }
    };

    std::vector<std::thread> threads;
    threads.reserve(THREADS);

    for (size_t i = 0; i < THREADS; ++i) {
        threads.emplace_back(worker);
    }

    for (auto& thread : threads) {
        thread.join();
    }

    BOOST_CHECK_EQUAL(failures.load(), 0);
    BOOST_CHECK_EQUAL(all_seeds.size(), TOTAL_KEYS);

    const uint64_t lower = TOTAL_KEYS * 35 / 100;
    const uint64_t upper = TOTAL_KEYS * 65 / 100;

    for (size_t bit = 0; bit < total_ones.size(); ++bit) {
        BOOST_CHECK_MESSAGE(
            total_ones[bit] >= lower && total_ones[bit] <= upper,
            "Seed bit " << bit
            << " suspicious: ones=" << total_ones[bit]
            << "/" << TOTAL_KEYS);
    }

    BOOST_TEST_MESSAGE(
        "Generated " << TOTAL_KEYS
        << " production P2QR keys across "
        << THREADS << " concurrent threads with zero duplicate seeds.");
}


BOOST_AUTO_TEST_CASE(cpqkey_production_rng_10m_soak)
{
    /*
     * Heavyweight security-audit test. It is deliberately gated so the
     * normal unit-test suite remains fast.
     *
     * Run with:
     *   KV5_PQKEY_ENTROPY_AUDIT=1 \
     *   ./test_kvanta5 \
     *     --run_test=pqkey_entropy_tests/cpqkey_production_rng_10m_soak \
     *     --log_level=test_suite
     */
    const char* audit_enabled = std::getenv("KV5_PQKEY_ENTROPY_AUDIT");
    if (audit_enabled == nullptr || std::string{audit_enabled} != "1") {
        BOOST_TEST_MESSAGE(
            "Skipping 10,000,000-key P2QR entropy soak. "
            "Set KV5_PQKEY_ENTROPY_AUDIT=1 to run it.");
        return;
    }

    static constexpr uint64_t TOTAL_KEYS = 10'000'000;
    static constexpr uint64_t PROGRESS_INTERVAL = 1'000'000;
    using Fingerprint = std::array<unsigned char, 16>; // 128-bit collision detector.

    const unsigned int hw_threads = std::thread::hardware_concurrency();
    const size_t THREADS = std::max<size_t>(8, hw_threads == 0 ? 8 : hw_threads);

    struct ThreadStats {
        std::array<uint64_t, MLDSA87_SEED_SIZE * 8> bit_ones{};
        std::array<uint64_t, MLDSA87_SEED_SIZE * 8> bit_flips{};
        std::array<uint64_t, 256> byte_counts{};
        std::array<std::array<uint64_t, 256>, MLDSA87_SEED_SIZE> byte_position_counts{};

        uint64_t valid_keys{0};
        uint64_t transitions{0};
        uint64_t seed_weight_sum{0};
        uint64_t seed_weight_sq_sum{0};
        uint64_t consecutive_hd_sum{0};
        uint64_t consecutive_hd_sq_sum{0};

        std::vector<Fingerprint> fingerprints;
        std::vector<uint64_t> prefixes64;
        std::vector<uint64_t> suffixes64;
    };

    std::vector<ThreadStats> thread_stats(THREADS);
    std::atomic<uint64_t> failures{0};
    std::atomic<uint64_t> completed{0};
    std::atomic<size_t> workers_done{0};

    auto popcount_byte = [](unsigned char value) -> unsigned int {
        unsigned int count = 0;
        while (value != 0) {
            count += value & 1U;
            value >>= 1;
        }
        return count;
    };

    auto worker = [&](size_t thread_index, uint64_t count) {
        ThreadStats& stats = thread_stats[thread_index];

        stats.fingerprints.reserve(count);
        stats.prefixes64.reserve(count);
        stats.suffixes64.reserve(count);

        std::array<unsigned char, MLDSA87_SEED_SIZE> previous_seed{};
        bool have_previous = false;
        uint64_t pending_progress = 0;

        for (uint64_t n = 0; n < count; ++n) {
            CPQKey key;

            if (!key.MakeNewKey()) {
                ++failures;
                ++pending_progress;

                if (pending_progress >= 4096) {
                    completed.fetch_add(pending_progress, std::memory_order_relaxed);
                    pending_progress = 0;
                }
                continue;
            }

            const auto& seed = key.GetSeedBytes();
            if (seed.size() != MLDSA87_SEED_SIZE) {
                ++failures;
                ++pending_progress;

                if (pending_progress >= 4096) {
                    completed.fetch_add(pending_progress, std::memory_order_relaxed);
                    pending_progress = 0;
                }
                continue;
            }

            ++stats.valid_keys;

            std::array<unsigned char, MLDSA87_SEED_SIZE> seed_copy{};
            std::copy(seed.begin(), seed.end(), seed_copy.begin());

            uint64_t seed_weight = 0;

            for (size_t byte = 0; byte < MLDSA87_SEED_SIZE; ++byte) {
                const unsigned char value = seed[byte];
                ++stats.byte_counts[value];
                ++stats.byte_position_counts[byte][value];

                const unsigned int weight = popcount_byte(value);
                seed_weight += weight;

                for (size_t bit = 0; bit < 8; ++bit) {
                    if ((value >> bit) & 1U) {
                        ++stats.bit_ones[byte * 8 + bit];
                    }
                }
            }

            stats.seed_weight_sum += seed_weight;
            stats.seed_weight_sq_sum += seed_weight * seed_weight;

            if (have_previous) {
                uint64_t hd = 0;

                for (size_t byte = 0; byte < MLDSA87_SEED_SIZE; ++byte) {
                    const unsigned char x =
                        static_cast<unsigned char>(seed_copy[byte] ^ previous_seed[byte]);

                    hd += popcount_byte(x);

                    for (size_t bit = 0; bit < 8; ++bit) {
                        if ((x >> bit) & 1U) {
                            ++stats.bit_flips[byte * 8 + bit];
                        }
                    }
                }

                ++stats.transitions;
                stats.consecutive_hd_sum += hd;
                stats.consecutive_hd_sq_sum += hd * hd;
            }

            previous_seed = seed_copy;
            have_previous = true;

            /*
             * Retain only a 128-bit cryptographic fingerprint for full-seed
             * duplicate detection. The raw generated seeds are never logged
             * or retained beyond the immediately previous sample.
             */
            const uint256 digest = Hash(seed);
            Fingerprint fingerprint{};
            std::copy_n(digest.begin(), fingerprint.size(), fingerprint.begin());
            stats.fingerprints.push_back(fingerprint);

            uint64_t prefix = 0;
            uint64_t suffix = 0;

            for (size_t i = 0; i < 8; ++i) {
                prefix = (prefix << 8) | static_cast<uint64_t>(seed[i]);

                /*
                 * Reverse the terminal 8 bytes so that suffix >> 32 groups
                 * equality of the actual final four bytes.
                 */
                suffix = (suffix << 8) |
                    static_cast<uint64_t>(seed[MLDSA87_SEED_SIZE - 1 - i]);
            }

            stats.prefixes64.push_back(prefix);
            stats.suffixes64.push_back(suffix);

            ++pending_progress;
            if (pending_progress >= 4096) {
                completed.fetch_add(pending_progress, std::memory_order_relaxed);
                pending_progress = 0;
            }
        }

        if (pending_progress != 0) {
            completed.fetch_add(pending_progress, std::memory_order_relaxed);
        }

        ++workers_done;
    };

    BOOST_TEST_MESSAGE(
        "Starting 10,000,000-key production P2QR entropy soak using "
        << THREADS << " worker threads.");

    std::vector<std::thread> threads;
    threads.reserve(THREADS);

    const uint64_t base_count = TOTAL_KEYS / THREADS;
    const uint64_t remainder = TOTAL_KEYS % THREADS;

    for (size_t i = 0; i < THREADS; ++i) {
        const uint64_t count = base_count + (i < remainder ? 1 : 0);
        threads.emplace_back(worker, i, count);
    }

    uint64_t next_report = PROGRESS_INTERVAL;

    while (workers_done.load(std::memory_order_relaxed) < THREADS) {
        std::this_thread::sleep_for(std::chrono::seconds(1));

        const uint64_t done = completed.load(std::memory_order_relaxed);
        while (done >= next_report && next_report <= TOTAL_KEYS) {
            BOOST_TEST_MESSAGE(
                "P2QR entropy soak progress: "
                << next_report << " / " << TOTAL_KEYS << " keys");
            next_report += PROGRESS_INTERVAL;
        }
    }

    for (auto& thread : threads) {
        thread.join();
    }

    const uint64_t final_completed = completed.load(std::memory_order_relaxed);
    BOOST_REQUIRE_EQUAL(final_completed, TOTAL_KEYS);
    BOOST_REQUIRE_EQUAL(failures.load(), 0);

    std::array<uint64_t, MLDSA87_SEED_SIZE * 8> bit_ones{};
    std::array<uint64_t, MLDSA87_SEED_SIZE * 8> bit_flips{};
    std::array<uint64_t, 256> byte_counts{};
    std::array<std::array<uint64_t, 256>, MLDSA87_SEED_SIZE> byte_position_counts{};

    uint64_t valid_keys = 0;
    uint64_t transitions = 0;
    uint64_t seed_weight_sum = 0;
    uint64_t seed_weight_sq_sum = 0;
    uint64_t consecutive_hd_sum = 0;
    uint64_t consecutive_hd_sq_sum = 0;

    std::vector<Fingerprint> fingerprints;
    std::vector<uint64_t> prefixes64;
    std::vector<uint64_t> suffixes64;

    fingerprints.reserve(TOTAL_KEYS);
    prefixes64.reserve(TOTAL_KEYS);
    suffixes64.reserve(TOTAL_KEYS);

    for (auto& stats : thread_stats) {
        valid_keys += stats.valid_keys;
        transitions += stats.transitions;
        seed_weight_sum += stats.seed_weight_sum;
        seed_weight_sq_sum += stats.seed_weight_sq_sum;
        consecutive_hd_sum += stats.consecutive_hd_sum;
        consecutive_hd_sq_sum += stats.consecutive_hd_sq_sum;

        for (size_t i = 0; i < bit_ones.size(); ++i) {
            bit_ones[i] += stats.bit_ones[i];
            bit_flips[i] += stats.bit_flips[i];
        }

        for (size_t i = 0; i < byte_counts.size(); ++i) {
            byte_counts[i] += stats.byte_counts[i];
        }

        for (size_t byte = 0; byte < MLDSA87_SEED_SIZE; ++byte) {
            for (size_t value = 0; value < 256; ++value) {
                byte_position_counts[byte][value] +=
                    stats.byte_position_counts[byte][value];
            }
        }

        fingerprints.insert(
            fingerprints.end(),
            std::make_move_iterator(stats.fingerprints.begin()),
            std::make_move_iterator(stats.fingerprints.end()));

        prefixes64.insert(
            prefixes64.end(),
            stats.prefixes64.begin(),
            stats.prefixes64.end());

        suffixes64.insert(
            suffixes64.end(),
            stats.suffixes64.begin(),
            stats.suffixes64.end());
    }

    BOOST_REQUIRE_EQUAL(valid_keys, TOTAL_KEYS);
    BOOST_REQUIRE_EQUAL(fingerprints.size(), TOTAL_KEYS);
    BOOST_REQUIRE_EQUAL(prefixes64.size(), TOTAL_KEYS);
    BOOST_REQUIRE_EQUAL(suffixes64.size(), TOTAL_KEYS);

    auto count_equal_pair_collisions = [](auto& values) -> uint64_t {
        std::sort(values.begin(), values.end());

        uint64_t pairs = 0;
        size_t begin = 0;

        while (begin < values.size()) {
            size_t end = begin + 1;
            while (end < values.size() && values[end] == values[begin]) {
                ++end;
            }

            const uint64_t group = static_cast<uint64_t>(end - begin);
            pairs += group * (group - 1) / 2;
            begin = end;
        }

        return pairs;
    };

    auto count_high32_pair_collisions_sorted64 =
        [](const std::vector<uint64_t>& values) -> uint64_t {
            uint64_t pairs = 0;
            size_t begin = 0;

            while (begin < values.size()) {
                const uint32_t key = static_cast<uint32_t>(values[begin] >> 32);
                size_t end = begin + 1;

                while (end < values.size() &&
                       static_cast<uint32_t>(values[end] >> 32) == key) {
                    ++end;
                }

                const uint64_t group = static_cast<uint64_t>(end - begin);
                pairs += group * (group - 1) / 2;
                begin = end;
            }

            return pairs;
        };

    BOOST_TEST_MESSAGE("Sorting 128-bit fingerprints for duplicate detection...");
    const uint64_t fingerprint_collision_pairs =
        count_equal_pair_collisions(fingerprints);

    BOOST_TEST_MESSAGE("Sorting 64-bit prefixes...");
    std::sort(prefixes64.begin(), prefixes64.end());

    uint64_t prefix64_collision_pairs = 0;
    {
        size_t begin = 0;
        while (begin < prefixes64.size()) {
            size_t end = begin + 1;
            while (end < prefixes64.size() && prefixes64[end] == prefixes64[begin]) {
                ++end;
            }
            const uint64_t group = static_cast<uint64_t>(end - begin);
            prefix64_collision_pairs += group * (group - 1) / 2;
            begin = end;
        }
    }

    const uint64_t prefix32_collision_pairs =
        count_high32_pair_collisions_sorted64(prefixes64);

    BOOST_TEST_MESSAGE("Sorting 64-bit suffixes...");
    std::sort(suffixes64.begin(), suffixes64.end());

    uint64_t suffix64_collision_pairs = 0;
    {
        size_t begin = 0;
        while (begin < suffixes64.size()) {
            size_t end = begin + 1;
            while (end < suffixes64.size() && suffixes64[end] == suffixes64[begin]) {
                ++end;
            }
            const uint64_t group = static_cast<uint64_t>(end - begin);
            suffix64_collision_pairs += group * (group - 1) / 2;
            begin = end;
        }
    }

    const uint64_t suffix32_collision_pairs =
        count_high32_pair_collisions_sorted64(suffixes64);

    const double expected_bit_ones = static_cast<double>(valid_keys) / 2.0;
    const double bit_sigma = std::sqrt(static_cast<double>(valid_keys) / 4.0);

    double maximum_bit_z = 0.0;
    size_t maximum_bit_z_index = 0;

    for (size_t bit = 0; bit < bit_ones.size(); ++bit) {
        const double z =
            std::abs(static_cast<double>(bit_ones[bit]) - expected_bit_ones) /
            bit_sigma;

        if (z > maximum_bit_z) {
            maximum_bit_z = z;
            maximum_bit_z_index = bit;
        }
    }

    const double expected_flips = static_cast<double>(transitions) / 2.0;
    const double flip_sigma = std::sqrt(static_cast<double>(transitions) / 4.0);

    double maximum_flip_z = 0.0;
    size_t maximum_flip_z_index = 0;

    for (size_t bit = 0; bit < bit_flips.size(); ++bit) {
        const double z =
            std::abs(static_cast<double>(bit_flips[bit]) - expected_flips) /
            flip_sigma;

        if (z > maximum_flip_z) {
            maximum_flip_z = z;
            maximum_flip_z_index = bit;
        }
    }

    const double total_bytes =
        static_cast<double>(valid_keys) * static_cast<double>(MLDSA87_SEED_SIZE);
    const double expected_byte_count = total_bytes / 256.0;

    double byte_chi_square = 0.0;
    for (const uint64_t observed : byte_counts) {
        const double delta = static_cast<double>(observed) - expected_byte_count;
        byte_chi_square += (delta * delta) / expected_byte_count;
    }

    const double expected_position_byte_count =
        static_cast<double>(valid_keys) / 256.0;

    double minimum_position_byte_chi_square =
        std::numeric_limits<double>::max();
    double maximum_position_byte_chi_square = 0.0;
    size_t minimum_position_byte_index = 0;
    size_t maximum_position_byte_index = 0;

    for (size_t byte = 0; byte < MLDSA87_SEED_SIZE; ++byte) {
        double chi_square = 0.0;

        for (size_t value = 0; value < 256; ++value) {
            const double delta =
                static_cast<double>(byte_position_counts[byte][value]) -
                expected_position_byte_count;

            chi_square +=
                (delta * delta) / expected_position_byte_count;
        }

        if (chi_square < minimum_position_byte_chi_square) {
            minimum_position_byte_chi_square = chi_square;
            minimum_position_byte_index = byte;
        }

        if (chi_square > maximum_position_byte_chi_square) {
            maximum_position_byte_chi_square = chi_square;
            maximum_position_byte_index = byte;
        }
    }

    const double seed_weight_mean =
        static_cast<double>(seed_weight_sum) / static_cast<double>(valid_keys);
    const double seed_weight_variance =
        static_cast<double>(seed_weight_sq_sum) / static_cast<double>(valid_keys) -
        seed_weight_mean * seed_weight_mean;

    const double hd_mean =
        static_cast<double>(consecutive_hd_sum) / static_cast<double>(transitions);
    const double hd_variance =
        static_cast<double>(consecutive_hd_sq_sum) / static_cast<double>(transitions) -
        hd_mean * hd_mean;

    const long double n = static_cast<long double>(valid_keys);
    const long double expected_32_collision_pairs =
        (n * (n - 1.0L)) /
        (2.0L * 4294967296.0L);

    const double prefix32_z =
        std::abs(
            static_cast<double>(prefix32_collision_pairs) -
            static_cast<double>(expected_32_collision_pairs)) /
        std::sqrt(static_cast<double>(expected_32_collision_pairs));

    const double suffix32_z =
        std::abs(
            static_cast<double>(suffix32_collision_pairs) -
            static_cast<double>(expected_32_collision_pairs)) /
        std::sqrt(static_cast<double>(expected_32_collision_pairs));

    BOOST_TEST_MESSAGE(
        "===== P2QR 10M ENTROPY SOAK RESULTS =====\n"
        << "Keys generated: " << valid_keys << "\n"
        << "Worker threads: " << THREADS << "\n"
        << "128-bit fingerprint collision pairs: "
        << fingerprint_collision_pairs << "\n"
        << "64-bit prefix collision pairs: "
        << prefix64_collision_pairs << "\n"
        << "64-bit suffix collision pairs: "
        << suffix64_collision_pairs << "\n"
        << "32-bit prefix collision pairs: "
        << prefix32_collision_pairs
        << " (expected ~" << static_cast<double>(expected_32_collision_pairs)
        << ", |z|=" << prefix32_z << ")\n"
        << "32-bit suffix collision pairs: "
        << suffix32_collision_pairs
        << " (expected ~" << static_cast<double>(expected_32_collision_pairs)
        << ", |z|=" << suffix32_z << ")\n"
        << "Maximum seed-bit frequency |z|: "
        << maximum_bit_z << " at bit " << maximum_bit_z_index << "\n"
        << "Maximum lag-1 bit-flip |z|: "
        << maximum_flip_z << " at bit " << maximum_flip_z_index << "\n"
        << "Aggregate byte-frequency chi-square (df=255): "
        << byte_chi_square << "\n"
        << "Per-position byte chi-square range (df=255 each): min="
        << minimum_position_byte_chi_square
        << " at byte " << minimum_position_byte_index
        << ", max=" << maximum_position_byte_chi_square
        << " at byte " << maximum_position_byte_index << "\n"
        << "Seed Hamming weight: mean="
        << seed_weight_mean << " variance=" << seed_weight_variance << "\n"
        << "Consecutive-seed Hamming distance: mean="
        << hd_mean << " variance=" << hd_variance);

    /*
     * These are intentionally conservative "gross defect" thresholds,
     * not claims of a formal entropy proof. With ten million samples they
     * are strong tripwires for stuck/bias/correlation failures while keeping
     * the chance of a healthy generator failing the audit extremely small.
     */
    BOOST_CHECK_EQUAL(fingerprint_collision_pairs, 0);
    BOOST_CHECK_EQUAL(prefix64_collision_pairs, 0);
    BOOST_CHECK_EQUAL(suffix64_collision_pairs, 0);

    BOOST_CHECK(maximum_bit_z < 8.0);
    BOOST_CHECK(maximum_flip_z < 8.0);

    BOOST_CHECK(byte_chi_square > 120.0);
    BOOST_CHECK(byte_chi_square < 400.0);

    BOOST_CHECK(minimum_position_byte_chi_square > 100.0);
    BOOST_CHECK(maximum_position_byte_chi_square < 450.0);

    BOOST_CHECK(std::abs(seed_weight_mean - 128.0) < 0.05);
    BOOST_CHECK(seed_weight_variance > 60.0);
    BOOST_CHECK(seed_weight_variance < 68.0);

    BOOST_CHECK(std::abs(hd_mean - 128.0) < 0.05);
    BOOST_CHECK(hd_variance > 60.0);
    BOOST_CHECK(hd_variance < 68.0);

    BOOST_CHECK(prefix32_z < 8.0);
    BOOST_CHECK(suffix32_z < 8.0);
}

BOOST_AUTO_TEST_SUITE_END()
