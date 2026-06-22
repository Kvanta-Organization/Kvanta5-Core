// Copyright (c) 2015-2026 The Bitcoin Core developers
// Copyright (c) 2026 The Kvanta5 Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "nontrivial-threadlocal.h"

#include <clang-tidy/ClangTidyModule.h>
#include <clang-tidy/ClangTidyModuleRegistry.h>

class Kvanta5Module final : public clang::tidy::ClangTidyModule
{
public:
    void addCheckFactories(clang::tidy::ClangTidyCheckFactories& CheckFactories) override
    {
        CheckFactories.registerCheck<kvanta5::NonTrivialThreadLocal>("kvanta5-nontrivial-threadlocal");
    }
};

static clang::tidy::ClangTidyModuleRegistry::Add<Kvanta5Module>
    X("kvanta5-module", "Adds kvanta5 checks.");

volatile int Kvanta5ModuleAnchorSource = 0;
