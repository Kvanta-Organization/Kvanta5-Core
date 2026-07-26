// Copyright (c) 2015-2026 The Bitcoin Core developers
// Copyright (c) 2026 The Kvanta5 Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kvanta5-build-config.h> // IWYU pragma: keep

#include <common/system.h>

#include <logging.h>
#include <util/string.h>
#include <util/time.h>

#ifndef WIN32

#include <sys/stat.h>
#include <unistd.h>

#ifdef __APPLE__
#include <sys/sysctl.h>
#endif

#else

#include <compat/compat.h>

#include <codecvt>

#endif

#ifdef HAVE_MALLOPT_ARENA_MAX
#include <malloc.h>
#endif

#include <cstdlib>
#include <limits>
#include <locale>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>

using util::ReplaceAll;

// Application startup time (used for uptime calculation)
const int64_t nStartupTime = GetTime();

#ifndef WIN32

std::string ShellEscape(const std::string& arg)
{
    std::string escaped = arg;

    ReplaceAll(
        escaped,
        "'",
        "'\"'\"'"
    );

    return "'" + escaped + "'";
}

#endif

#if HAVE_SYSTEM

void runCommand(const std::string& strCommand)
{
    if (strCommand.empty()) {
        return;
    }

#ifndef WIN32

    int nErr = ::system(
        strCommand.c_str()
    );

#else

    int nErr = ::_wsystem(
        std::wstring_convert<
            std::codecvt_utf8_utf16<wchar_t>,
            wchar_t
        >()
            .from_bytes(strCommand)
            .c_str()
    );

#endif

    if (nErr) {
        LogPrintf(
            "runCommand error: system(%s) returned %d\n",
            strCommand,
            nErr
        );
    }
}

#endif

void SetupEnvironment()
{
#ifdef HAVE_MALLOPT_ARENA_MAX

    /*
     * glibc-specific:
     *
     * On 32-bit systems set the number of arenas to one.
     */
    if (sizeof(void*) == 4) {
        mallopt(
            M_ARENA_MAX,
            1
        );
    }

#endif

#if !defined(WIN32) && \
    !defined(__APPLE__) && \
    !defined(__FreeBSD__) && \
    !defined(__OpenBSD__) && \
    !defined(__NetBSD__)

    try {
        std::locale("");
    } catch (const std::runtime_error&) {
        setenv(
            "LC_ALL",
            "C.UTF-8",
            1
        );
    }

#elif defined(WIN32)

    SetConsoleCP(CP_UTF8);
    SetConsoleOutputCP(CP_UTF8);

#endif

#ifndef WIN32

    constexpr mode_t private_umask = 0077;
    umask(private_umask);

#endif
}

bool SetupNetworking()
{
#ifdef WIN32

    WSADATA wsadata;

    int ret = WSAStartup(
        MAKEWORD(2, 2),
        &wsadata
    );

    if (
        ret != NO_ERROR ||
        LOBYTE(wsadata.wVersion) != 2 ||
        HIBYTE(wsadata.wVersion) != 2
    ) {
        return false;
    }

#endif

    return true;
}

int GetNumCores()
{
    return std::thread::hardware_concurrency();
}

std::optional<uint64_t> GetTotalPhysicalMemory()
{
#ifdef WIN32

    MEMORYSTATUSEX status{};
    status.dwLength = sizeof(status);

    if (
        !GlobalMemoryStatusEx(&status) ||
        status.ullTotalPhys == 0
    ) {
        return std::nullopt;
    }

    return static_cast<uint64_t>(
        status.ullTotalPhys
    );

#elif defined(__APPLE__)

    uint64_t memory_bytes{0};

    size_t memory_size{
        sizeof(memory_bytes)
    };

    if (
        sysctlbyname(
            "hw.memsize",
            &memory_bytes,
            &memory_size,
            nullptr,
            0
        ) != 0 ||
        memory_size != sizeof(memory_bytes) ||
        memory_bytes == 0
    ) {
        return std::nullopt;
    }

    return memory_bytes;

#else

    const long physical_pages{
        sysconf(_SC_PHYS_PAGES)
    };

    const long page_size{
        sysconf(_SC_PAGESIZE)
    };

    if (
        physical_pages <= 0 ||
        page_size <= 0
    ) {
        return std::nullopt;
    }

    const uint64_t pages{
        static_cast<uint64_t>(
            physical_pages
        )
    };

    const uint64_t bytes_per_page{
        static_cast<uint64_t>(
            page_size
        )
    };

    if (
        pages >
        std::numeric_limits<uint64_t>::max() /
            bytes_per_page
    ) {
        return std::nullopt;
    }

    return pages * bytes_per_page;

#endif
}

int64_t GetStartupTime()
{
    return nStartupTime;
}
