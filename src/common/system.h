// Copyright (c) 2015-2026 The Bitcoin Core developers
// Copyright (c) 2026 The Kvanta5 Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KVANTA5_COMMON_SYSTEM_H
#define KVANTA5_COMMON_SYSTEM_H

#include <kvanta5-build-config.h> // IWYU pragma: keep

#include <cstdint>
#include <optional>
#include <string>

// Application startup time (used for uptime calculation)
int64_t GetStartupTime();

void SetupEnvironment();
[[nodiscard]] bool SetupNetworking();

#ifndef WIN32
std::string ShellEscape(const std::string& arg);
#endif

#if HAVE_SYSTEM
void runCommand(const std::string& strCommand);
#endif

/**
 * Return the number of cores available on the current system.
 *
 * This includes virtual cores such as those provided by HyperThreading.
 */
int GetNumCores();

/**
 * Return total installed physical memory in bytes when it can be detected.
 */
std::optional<uint64_t> GetTotalPhysicalMemory();

#endif // KVANTA5_COMMON_SYSTEM_H
