// Copyright (c) 2015-2026 The Bitcoin Core developers
// Copyright (c) 2026 The Kvanta5 Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <interfaces/wallet.h>

#include <common/args.h>
#include <consensus/amount.h>
#include <consensus/kvanta5p2qr.h>
#include <key_io.h>
#include <interfaces/chain.h>
#include <interfaces/handler.h>
#include <node/types.h>
#include <policy/fees.h>
#include <policy/settings.h>
#include <primitives/transaction.h>
#include <rpc/server.h>
#include <scheduler.h>
#include <support/allocators/secure.h>
#include <sync.h>
#include <uint256.h>
#include <util/check.h>
#include <util/translation.h>
#include <util/ui_change_type.h>
#include <wallet/coincontrol.h>
#include <wallet/context.h>
#include <wallet/feebumper.h>
#include <wallet/fees.h>
#include <wallet/types.h>
#include <wallet/load.h>
#include <wallet/receive.h>
#include <wallet/rpc/wallet.h>
#include <wallet/spend.h>
#include <wallet/wallet.h>
#include <script/kvanta5_p2qr.h>
#include <tinyformat.h>
#include <util/strencodings.h>
#include <consensus/validation.h>
#include <core_io.h>
#include <node/transaction.h>
#include <streams.h>
#include <univalue.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>
#include <algorithm>
#include <limits>



using common::PSBTError;
using interfaces::Chain;
using interfaces::FoundBlock;
using interfaces::Handler;
using interfaces::MakeSignalHandler;
using interfaces::Wallet;
using interfaces::WalletAddress;
using interfaces::WalletBalances;
using interfaces::WalletLoader;
using interfaces::WalletOrderForm;
using interfaces::WalletTx;
using interfaces::WalletTxOut;
using interfaces::WalletTxStatus;
using interfaces::WalletValueMap;

namespace wallet {
class CWallet;

UniValue DumpKvanta5VaultBackupToFile(
    CWallet& wallet,
    const std::string& address,
    const fs::path& filename);

UniValue ImportKvanta5VaultBackupFromFile(
    CWallet& wallet,
    const fs::path& filename,
    const std::string& label);
} // namespace wallet


namespace wallet {
// All members of the classes in this namespace are intentionally public, as the
// classes themselves are private.
namespace {
//! Construct wallet tx struct.
WalletTx MakeWalletTx(CWallet& wallet, const CWalletTx& wtx)
{
    LOCK(wallet.cs_wallet);

    WalletTx result;
    result.tx = wtx.tx;

    result.txin_is_mine.reserve(wtx.tx->vin.size());
    for (const auto& txin : wtx.tx->vin) {
        result.txin_is_mine.emplace_back(InputIsMine(wallet, txin));
    }

    result.txout_is_mine.reserve(wtx.tx->vout.size());
    result.txout_is_change.reserve(wtx.tx->vout.size());
    result.txout_address.reserve(wtx.tx->vout.size());
    result.txout_p2qr_mining_address.reserve(wtx.tx->vout.size());
    result.txout_address_is_mine.reserve(wtx.tx->vout.size());

    for (const auto& txout : wtx.tx->vout) {
        const wallet::isminetype output_mine = wallet.IsMine(txout);

        CTxDestination display_dest;
        std::string p2qr_mining_address;
        wallet::isminetype address_mine = ISMINE_NO;

        Kvanta5P2QRDestination p2qr_dest;
        if (wallet.GetKvanta5P2QRDestinationForScript(txout.scriptPubKey, p2qr_dest)) {
            // Display the canonical P2QR address as the primary wallet identity.
            display_dest = p2qr_dest;
            address_mine = wallet.IsMine(p2qr_dest);

            // Also derive/display the P2QR Mining Address tied to the same program.
            uint256 program;
            std::copy(p2qr_dest.begin(), p2qr_dest.end(), program.begin());

            const CScript redeem_script = consensus::kvanta5p2qr::MakeRedeemScript(program);
            const CScriptID mining_script_id(redeem_script);
            const CTxDestination mining_dest{ScriptHash(mining_script_id)};

            p2qr_mining_address = EncodeDestination(mining_dest);
        } else if (ExtractDestination(txout.scriptPubKey, display_dest)) {
            address_mine = wallet.IsMine(display_dest);
        }

        result.txout_is_mine.emplace_back(output_mine);
        result.txout_is_change.push_back(OutputIsChange(wallet, txout));
        result.txout_address.emplace_back(std::move(display_dest));
        result.txout_p2qr_mining_address.emplace_back(std::move(p2qr_mining_address));
        result.txout_address_is_mine.emplace_back(address_mine);
    }

    result.credit = CachedTxGetCredit(wallet, wtx, ISMINE_ALL);
    result.debit = CachedTxGetDebit(wallet, wtx, ISMINE_ALL);
    result.change = CachedTxGetChange(wallet, wtx);
    result.time = wtx.GetTxTime();
    result.value_map = wtx.mapValue;
    result.is_coinbase = wtx.IsCoinBase();

    return result;
}

//! Construct wallet tx status struct.
WalletTxStatus MakeWalletTxStatus(const CWallet& wallet, const CWalletTx& wtx)
    EXCLUSIVE_LOCKS_REQUIRED(wallet.cs_wallet)
{
    AssertLockHeld(wallet.cs_wallet);

    WalletTxStatus result;
    result.block_height =
        wtx.state<TxStateConfirmed>() ? wtx.state<TxStateConfirmed>()->confirmed_block_height :
        wtx.state<TxStateBlockConflicted>() ? wtx.state<TxStateBlockConflicted>()->conflicting_block_height :
        std::numeric_limits<int>::max();
    result.blocks_to_maturity = wallet.GetTxBlocksToMaturity(wtx);
    result.depth_in_main_chain = wallet.GetTxDepthInMainChain(wtx);
    result.time_received = wtx.nTimeReceived;
    result.lock_time = wtx.tx->nLockTime;
    result.is_trusted = CachedTxIsTrusted(wallet, wtx);
    result.is_abandoned = wtx.isAbandoned();
    result.is_coinbase = wtx.IsCoinBase();
    result.is_in_main_chain = wtx.isConfirmed();
    return result;
}

//! Construct wallet TxOut struct.
WalletTxOut MakeWalletTxOut(const CWallet& wallet,
    const CWalletTx& wtx,
    int n,
    int depth) EXCLUSIVE_LOCKS_REQUIRED(wallet.cs_wallet)
{
    WalletTxOut result;
    result.txout = wtx.tx->vout[n];
    result.time = wtx.GetTxTime();
    result.depth_in_main_chain = depth;
    result.is_spent = wallet.IsSpent(COutPoint(wtx.GetHash(), n));
    return result;
}

WalletTxOut MakeWalletTxOut(const CWallet& wallet,
    const COutput& output) EXCLUSIVE_LOCKS_REQUIRED(wallet.cs_wallet)
{
    WalletTxOut result;
    result.txout = output.txout;
    result.time = output.time;
    result.depth_in_main_chain = output.depth;
    result.is_spent = wallet.IsSpent(output.outpoint);
    return result;
}

class WalletImpl : public Wallet
{
public:
    explicit WalletImpl(WalletContext& context, const std::shared_ptr<CWallet>& wallet) : m_context(context), m_wallet(wallet) {}

    bool encryptWallet(const SecureString& wallet_passphrase) override
    {
        return m_wallet->EncryptWallet(wallet_passphrase);
    }
    bool isCrypted() override { return m_wallet->IsCrypted(); }
    bool lock() override { return m_wallet->Lock(); }
    bool unlock(const SecureString& wallet_passphrase) override { return m_wallet->Unlock(wallet_passphrase); }
    bool isLocked() override { return m_wallet->IsLocked(); }
    bool changeWalletPassphrase(const SecureString& old_wallet_passphrase,
        const SecureString& new_wallet_passphrase) override
    {
        return m_wallet->ChangeWalletPassphrase(old_wallet_passphrase, new_wallet_passphrase);
    }
    void abortRescan() override { m_wallet->AbortRescan(); }
    bool backupWallet(const std::string& filename) override { return m_wallet->BackupWallet(filename); }
    std::string getWalletName() override { return m_wallet->GetName(); }

    util::Result<CTxDestination> getNewDestination(const OutputType type, const std::string& label) override
    {
        LOCK(m_wallet->cs_wallet);

        if (type == OutputType::KVANTA5_P2QR) {
            auto p2qr_dest = m_wallet->GenerateNewKvanta5P2QRDestination(label);
            if (!p2qr_dest) {
                return util::Error{Untranslated("Error: Failed to generate KV5 P2QR address.")};
            }

            CTxDestination dest;
            dest.emplace<Kvanta5P2QRDestination>(*p2qr_dest);
            return dest;
        }

        return m_wallet->GetNewDestination(type, label);
    }

    util::Result<interfaces::Kvanta5P2QRSignerInfo> createKvanta5P2QRSigner(
        const std::string& label) override
    {
        LOCK(m_wallet->cs_wallet);

        util::Result<Kvanta5P2QRDestination> dest_res =
            m_wallet->GenerateNewKvanta5P2QRDestination(label);

        if (!dest_res) {
            return util::Error{util::ErrorString(dest_res)};
        }

        const Kvanta5P2QRDestination dest = *dest_res;

        std::vector<unsigned char> pubkey;
        if (!m_wallet->GetKvanta5P2QRPubKey(dest, pubkey)) {
            return util::Error{Untranslated("failed to retrieve generated P2QR signer pubkey")};
        }

        uint256 program;
        std::copy(dest.begin(), dest.end(), program.begin());

        const CScript redeem_script = consensus::kvanta5p2qr::MakeRedeemScript(program);
        const CScriptID script_id(redeem_script);
        const CTxDestination wrapped_dest = ScriptHash(script_id);

        interfaces::Kvanta5P2QRSignerInfo info;
        info.address = EncodeDestination(dest);
        info.wrapped_address = EncodeDestination(wrapped_dest);
        info.pubkey = HexStr(pubkey);
        return info;
    }
       
    util::Result<interfaces::Kvanta5P2QRMultisigInfo> createKvanta5P2QRMultisig(
        int required,
        const std::vector<std::string>& signer_addresses,
        const std::string& label,
        bool include_wrapped) override
    {
    if (required <= 0 || required > std::numeric_limits<uint16_t>::max()) {
        return util::Error{Untranslated("nrequired must be between 1 and 65535")};
    }

    if (signer_addresses.empty()) {
        return util::Error{Untranslated("P2QR multisig signer addresses must not be empty")};
    }

    if (signer_addresses.size() > KVANTA5_P2QR_MULTISIG_MAX_KEYS) {
        return util::Error{Untranslated(strprintf(
            "too many signer addresses: got %u, maximum is %u",
            static_cast<unsigned int>(signer_addresses.size()),
            static_cast<unsigned int>(KVANTA5_P2QR_MULTISIG_MAX_KEYS)))};
    }

    std::vector<uint256> signer_programs;
    signer_programs.reserve(signer_addresses.size());

    for (const std::string& signer_address : signer_addresses) {
        const CTxDestination signer_dest = DecodeDestination(signer_address);

        if (!std::holds_alternative<Kvanta5P2QRDestination>(signer_dest)) {
            return util::Error{Untranslated("invalid P2QR multisig signer address")};
        }

        const Kvanta5P2QRDestination p2qr_signer_dest =
            std::get<Kvanta5P2QRDestination>(signer_dest);

        uint256 signer_program;
        signer_program.SetNull();
        std::copy(p2qr_signer_dest.begin(), p2qr_signer_dest.end(), signer_program.begin());

        signer_programs.push_back(signer_program);
    }

    std::sort(signer_programs.begin(), signer_programs.end());

    std::string policy_error;
    if (!Kvanta5IsCanonicalP2QRMultisigPolicy(static_cast<uint16_t>(required), signer_programs, &policy_error)) {
        return util::Error{Untranslated(strprintf("invalid P2QR multisig signer address policy: %s", policy_error))};
    }

    LOCK(m_wallet->cs_wallet);

    util::Result<Kvanta5P2QRDestination> dest_res =
        m_wallet->AddKvanta5P2QRMultisig(static_cast<uint16_t>(required), signer_programs, label);

    if (!dest_res) {
        return util::Error{util::ErrorString(dest_res)};
    }

    const Kvanta5P2QRDestination dest = *dest_res;

    uint256 program;
    program.SetNull();
    std::copy(dest.begin(), dest.end(), program.begin());

    const CScript native_script = GetScriptForDestination(dest);
    const CScript redeem_script = consensus::kvanta5p2qr::MakeRedeemScript(program);
    const CScriptID script_id(redeem_script);
    const CTxDestination wrapped_dest = ScriptHash(script_id);
    const CScript wrapped_script = GetScriptForDestination(wrapped_dest);

    interfaces::Kvanta5P2QRMultisigInfo info;
    info.address = EncodeDestination(dest);

    /*
     * P2QR programs are raw 32-byte script/address programs.
     * Do NOT use uint256::GetHex() or uint256::ToString() here;
     * those print hash-style reversed display order.
     */
    info.program = HexStr(dest);

    info.script_pub_key = HexStr(native_script);
    info.required = required;
    info.total = static_cast<int>(signer_programs.size());

    for (const uint256& signer_program : signer_programs) {
        const Kvanta5P2QRDestination signer_dest{signer_program};
        info.signer_addresses.push_back(EncodeDestination(signer_dest));
    }

    if (include_wrapped) {
        info.wrapped_address = EncodeDestination(wrapped_dest);
        info.wrapped_redeem_script = HexStr(redeem_script);
        info.wrapped_script_pub_key = HexStr(wrapped_script);
    }

    return info;
    }

    util::Result<std::string> createKvanta5VaultTxPackage(
        const std::vector<CRecipient>& recipients,
        const CCoinControl& coin_control) override
        {
        LOCK(m_wallet->cs_wallet);

        auto package = m_wallet->CreateKvanta5VaultTxPackage(recipients, coin_control);
        if (!package) {
            return util::Error{util::ErrorString(package)};
        }

        auto encoded = m_wallet->EncodeKvanta5VaultTxPackage(*package);
        if (!encoded) {
            return util::Error{util::ErrorString(encoded)};
        }

        return *encoded;
        }

    util::Result<std::string> approveKvanta5VaultTxPackage(
        const std::string& package_json) override
        {
        LOCK(m_wallet->cs_wallet);

        auto package = m_wallet->DecodeKvanta5VaultTxPackage(package_json);
        if (!package) {
            return util::Error{util::ErrorString(package)};
        }

        auto approved = m_wallet->ApproveKvanta5VaultTxPackage(*package);
        if (!approved) {
            return util::Error{util::ErrorString(approved)};
        }

        auto encoded = m_wallet->EncodeKvanta5VaultTxPackage(*approved);
        if (!encoded) {
            return util::Error{util::ErrorString(encoded)};
        }

        return *encoded;
        }

    util::Result<std::string> finalizeKvanta5VaultTxPackage(
        const std::string& package_json) override
        {
        LOCK(m_wallet->cs_wallet);

        auto package = m_wallet->DecodeKvanta5VaultTxPackage(package_json);
        if (!package) {
            return util::Error{util::ErrorString(package)};
        }

        auto tx = m_wallet->FinalizeKvanta5VaultTxPackage(*package);
        if (!tx) {
            return util::Error{util::ErrorString(tx)};
        }

        DataStream ssTx{};
        ssTx << TX_WITH_WITNESS(**tx);
        return HexStr(ssTx);
    }
    
    util::Result<std::string> dumpKvanta5VaultBackup(
        const std::string& address,
        const std::string& filename) override
    {
        try {
            UniValue result = wallet::DumpKvanta5VaultBackupToFile(
                *m_wallet,
                address,
                fs::u8path(filename));

            return result.write(2);
        } catch (const UniValue& e) {
            return util::Error{e.write()};
        } catch (const std::exception& e) {
            return util::Error{e.what()};
        }
    }

    util::Result<std::string> importKvanta5VaultBackup(
        const std::string& filename,
        const std::string& label) override
    {
        try {
            UniValue result = wallet::ImportKvanta5VaultBackupFromFile(
                *m_wallet,
                fs::u8path(filename),
                label);

            return result.write(2);
        } catch (const UniValue& e) {
            return util::Error{e.write()};
        } catch (const std::exception& e) {
            return util::Error{e.what()};
        }
    }
    
    
    
    
    bool getPubKey(const CScript& script, const CKeyID& address, CPubKey& pub_key) override
    {
        std::unique_ptr<SigningProvider> provider = m_wallet->GetSolvingProvider(script);
        if (provider) {
            return provider->GetPubKey(address, pub_key);
        }
        return false;
    }
    SigningResult signMessage(const std::string& message, const PKHash& pkhash, std::string& str_sig) override
    {
        return m_wallet->SignMessage(message, pkhash, str_sig);
    }
    bool isSpendable(const CTxDestination& dest) override
    {
        LOCK(m_wallet->cs_wallet);
        return m_wallet->IsMine(dest) & ISMINE_SPENDABLE;
    }
    bool haveWatchOnly() override
    {
        auto spk_man = m_wallet->GetLegacyScriptPubKeyMan();
        if (spk_man) {
            return spk_man->HaveWatchOnly();
        }
        return false;
    };
    bool setAddressBook(const CTxDestination& dest, const std::string& name, const std::optional<AddressPurpose>& purpose) override
    {
        return m_wallet->SetAddressBook(dest, name, purpose);
    }
    bool delAddressBook(const CTxDestination& dest) override
    {
        return m_wallet->DelAddressBook(dest);
    }
    bool getAddress(const CTxDestination& dest,
        std::string* name,
        isminetype* is_mine,
        AddressPurpose* purpose) override
    {
        LOCK(m_wallet->cs_wallet);
        const auto& entry = m_wallet->FindAddressBookEntry(dest, /*allow_change=*/false);
        if (!entry) return false; // addr not found
        if (name) {
            *name = entry->GetLabel();
        }
        std::optional<isminetype> dest_is_mine;
        if (is_mine || purpose) {
            dest_is_mine = m_wallet->IsMine(dest);
        }
        if (is_mine) {
            *is_mine = *dest_is_mine;
        }
        if (purpose) {
            // In very old wallets, address purpose may not be recorded so we derive it from IsMine
            *purpose = entry->purpose.value_or(*dest_is_mine ? AddressPurpose::RECEIVE : AddressPurpose::SEND);
        }
        return true;
    }
    std::vector<WalletAddress> getAddresses() override
    {
        LOCK(m_wallet->cs_wallet);
        std::vector<WalletAddress> result;
        m_wallet->ForEachAddrBookEntry([&](const CTxDestination& dest, const std::string& label, bool is_change, const std::optional<AddressPurpose>& purpose) EXCLUSIVE_LOCKS_REQUIRED(m_wallet->cs_wallet) {
            if (is_change) return;
            isminetype is_mine = m_wallet->IsMine(dest);
            // In very old wallets, address purpose may not be recorded so we derive it from IsMine
            result.emplace_back(dest, is_mine, purpose.value_or(is_mine ? AddressPurpose::RECEIVE : AddressPurpose::SEND), label);
        });
        return result;
    }
    std::vector<std::string> getAddressReceiveRequests() override {
        LOCK(m_wallet->cs_wallet);
        return m_wallet->GetAddressReceiveRequests();
    }
    bool setAddressReceiveRequest(const CTxDestination& dest, const std::string& id, const std::string& value) override {
        // Note: The setAddressReceiveRequest interface used by the GUI to store
        // receive requests is a little awkward and could be improved in the
        // future:
        //
        // - The same method is used to save requests and erase them, but
        //   having separate methods could be clearer and prevent bugs.
        //
        // - Request ids are passed as strings even though they are generated as
        //   integers.
        //
        // - Multiple requests can be stored for the same address, but it might
        //   be better to only allow one request or only keep the current one.
        LOCK(m_wallet->cs_wallet);
        WalletBatch batch{m_wallet->GetDatabase()};
        return value.empty() ? m_wallet->EraseAddressReceiveRequest(batch, dest, id)
                             : m_wallet->SetAddressReceiveRequest(batch, dest, id, value);
    }
    util::Result<void> displayAddress(const CTxDestination& dest) override
    {
        LOCK(m_wallet->cs_wallet);
        return m_wallet->DisplayAddress(dest);
    }
    bool lockCoin(const COutPoint& output, const bool write_to_db) override
    {
        LOCK(m_wallet->cs_wallet);
        std::unique_ptr<WalletBatch> batch = write_to_db ? std::make_unique<WalletBatch>(m_wallet->GetDatabase()) : nullptr;
        return m_wallet->LockCoin(output, batch.get());
    }
    bool unlockCoin(const COutPoint& output) override
    {
        LOCK(m_wallet->cs_wallet);
        std::unique_ptr<WalletBatch> batch = std::make_unique<WalletBatch>(m_wallet->GetDatabase());
        return m_wallet->UnlockCoin(output, batch.get());
    }
    bool isLockedCoin(const COutPoint& output) override
    {
        LOCK(m_wallet->cs_wallet);
        return m_wallet->IsLockedCoin(output);
    }
    void listLockedCoins(std::vector<COutPoint>& outputs) override
    {
        LOCK(m_wallet->cs_wallet);
        return m_wallet->ListLockedCoins(outputs);
    }
    util::Result<CTransactionRef> createTransaction(const std::vector<CRecipient>& recipients,
        const CCoinControl& coin_control,
        bool sign,
        int& change_pos,
        CAmount& fee) override
    {
        LOCK(m_wallet->cs_wallet);
        auto res = CreateTransaction(*m_wallet, recipients, change_pos == -1 ? std::nullopt : std::make_optional(change_pos),
                                     coin_control, sign);
        if (!res) return util::Error{util::ErrorString(res)};
        const auto& txr = *res;
        fee = txr.fee;
        change_pos = txr.change_pos ? int(*txr.change_pos) : -1;

        return txr.tx;
    }
    void commitTransaction(CTransactionRef tx,
        WalletValueMap value_map,
        WalletOrderForm order_form) override
    {
        LOCK(m_wallet->cs_wallet);
        m_wallet->CommitTransaction(std::move(tx), std::move(value_map), std::move(order_form));
    }
    
    util::Result<std::string> broadcastTransactionHex(const std::string& tx_hex) override
    {
        if (!IsHex(tx_hex)) {
            return util::Error{Untranslated("transaction hex is not valid hex")};
        }

        CMutableTransaction mtx;
        const std::vector<unsigned char> tx_data = ParseHex(tx_hex);
        DataStream ss(tx_data);

        try {
            ss >> TX_WITH_WITNESS(mtx);
        } catch (const std::exception& e) {
            return util::Error{Untranslated(strprintf(
                "transaction decode failed: %s",
                e.what()))};
        }

        CTransactionRef tx = MakeTransactionRef(std::move(mtx));

        std::string err_string;
        const bool relay = true;
        const bool ok = m_wallet->chain().broadcastTransaction(
            tx,
            m_wallet->m_default_max_tx_fee,
            relay,
            err_string);

        if (!ok) {
            return util::Error{Untranslated(strprintf(
                "transaction broadcast failed: %s",
                err_string))};
        }

        return tx->GetHash().ToString();
        }
    
    
    
    
    
    bool transactionCanBeAbandoned(const uint256& txid) override { return m_wallet->TransactionCanBeAbandoned(txid); }
    bool abandonTransaction(const uint256& txid) override
    {
        LOCK(m_wallet->cs_wallet);
        return m_wallet->AbandonTransaction(txid);
    }
    bool transactionCanBeBumped(const uint256& txid) override
    {
        return feebumper::TransactionCanBeBumped(*m_wallet.get(), txid);
    }
    bool createBumpTransaction(const uint256& txid,
        const CCoinControl& coin_control,
        std::vector<bilingual_str>& errors,
        CAmount& old_fee,
        CAmount& new_fee,
        CMutableTransaction& mtx) override
    {
        std::vector<CTxOut> outputs; // just an empty list of new recipients for now
        return feebumper::CreateRateBumpTransaction(*m_wallet.get(), txid, coin_control, errors, old_fee, new_fee, mtx, /* require_mine= */ true, outputs) == feebumper::Result::OK;
    }
    bool signBumpTransaction(CMutableTransaction& mtx) override { return feebumper::SignTransaction(*m_wallet.get(), mtx); }
    bool commitBumpTransaction(const uint256& txid,
        CMutableTransaction&& mtx,
        std::vector<bilingual_str>& errors,
        uint256& bumped_txid) override
    {
        return feebumper::CommitTransaction(*m_wallet.get(), txid, std::move(mtx), errors, bumped_txid) ==
               feebumper::Result::OK;
    }
    CTransactionRef getTx(const uint256& txid) override
    {
        LOCK(m_wallet->cs_wallet);
        auto mi = m_wallet->mapWallet.find(txid);
        if (mi != m_wallet->mapWallet.end()) {
            return mi->second.tx;
        }
        return {};
    }
    WalletTx getWalletTx(const uint256& txid) override
    {
        LOCK(m_wallet->cs_wallet);
        auto mi = m_wallet->mapWallet.find(txid);
        if (mi != m_wallet->mapWallet.end()) {
            return MakeWalletTx(*m_wallet, mi->second);
        }
        return {};
    }
    std::set<WalletTx> getWalletTxs() override
    {
        LOCK(m_wallet->cs_wallet);
        std::set<WalletTx> result;
        for (const auto& entry : m_wallet->mapWallet) {
            result.emplace(MakeWalletTx(*m_wallet, entry.second));
        }
        return result;
    }
    bool tryGetTxStatus(const uint256& txid,
        interfaces::WalletTxStatus& tx_status,
        int& num_blocks,
        int64_t& block_time) override
    {
        TRY_LOCK(m_wallet->cs_wallet, locked_wallet);
        if (!locked_wallet) {
            return false;
        }
        auto mi = m_wallet->mapWallet.find(txid);
        if (mi == m_wallet->mapWallet.end()) {
            return false;
        }
        num_blocks = m_wallet->GetLastBlockHeight();
        block_time = -1;
        CHECK_NONFATAL(m_wallet->chain().findBlock(m_wallet->GetLastBlockHash(), FoundBlock().time(block_time)));
        tx_status = MakeWalletTxStatus(*m_wallet, mi->second);
        return true;
    }
    WalletTx getWalletTxDetails(const uint256& txid,
        WalletTxStatus& tx_status,
        WalletOrderForm& order_form,
        bool& in_mempool,
        int& num_blocks) override
    {
        LOCK(m_wallet->cs_wallet);
        auto mi = m_wallet->mapWallet.find(txid);
        if (mi != m_wallet->mapWallet.end()) {
            num_blocks = m_wallet->GetLastBlockHeight();
            in_mempool = mi->second.InMempool();
            order_form = mi->second.vOrderForm;
            tx_status = MakeWalletTxStatus(*m_wallet, mi->second);
            return MakeWalletTx(*m_wallet, mi->second);
        }
        return {};
    }
    std::optional<PSBTError> fillPSBT(int sighash_type,
        bool sign,
        bool bip32derivs,
        size_t* n_signed,
        PartiallySignedTransaction& psbtx,
        bool& complete) override
    {
        return m_wallet->FillPSBT(psbtx, complete, sighash_type, sign, bip32derivs, n_signed);
    }
    WalletBalances getBalances() override
    {
        const auto bal = GetBalance(*m_wallet);
        WalletBalances result;
        result.balance = bal.m_mine_trusted;
        result.unconfirmed_balance = bal.m_mine_untrusted_pending;
        result.immature_balance = bal.m_mine_immature;
        result.have_watch_only = haveWatchOnly();
        if (result.have_watch_only) {
            result.watch_only_balance = bal.m_watchonly_trusted;
            result.unconfirmed_watch_only_balance = bal.m_watchonly_untrusted_pending;
            result.immature_watch_only_balance = bal.m_watchonly_immature;
        }
        return result;
    }
    bool tryGetBalances(WalletBalances& balances, uint256& block_hash) override
    {
        TRY_LOCK(m_wallet->cs_wallet, locked_wallet);
        if (!locked_wallet) {
            return false;
        }
        block_hash = m_wallet->GetLastBlockHash();
        balances = getBalances();
        return true;
    }
    CAmount getBalance() override { return GetBalance(*m_wallet).m_mine_trusted; }
    CAmount getAvailableBalance(const CCoinControl& coin_control) override
    {
        LOCK(m_wallet->cs_wallet);
        CAmount total_amount = 0;
        // Fetch selected coins total amount
        if (coin_control.HasSelected()) {
            FastRandomContext rng{};
            CoinSelectionParams params(rng);
            // Note: for now, swallow any error.
            if (auto res = FetchSelectedInputs(*m_wallet, coin_control, params)) {
                total_amount += res->total_amount;
            }
        }

        // And fetch the wallet available coins
        if (coin_control.m_allow_other_inputs) {
            total_amount += AvailableCoins(*m_wallet, &coin_control).GetTotalAmount();
        }

        return total_amount;
    }
    isminetype txinIsMine(const CTxIn& txin) override
    {
        LOCK(m_wallet->cs_wallet);
        return InputIsMine(*m_wallet, txin);
    }
    isminetype txoutIsMine(const CTxOut& txout) override
    {
        LOCK(m_wallet->cs_wallet);
        return m_wallet->IsMine(txout);
    }
    CAmount getDebit(const CTxIn& txin, isminefilter filter) override
    {
        LOCK(m_wallet->cs_wallet);
        return m_wallet->GetDebit(txin, filter);
    }
    CAmount getCredit(const CTxOut& txout, isminefilter filter) override
    {
        LOCK(m_wallet->cs_wallet);
        return OutputGetCredit(*m_wallet, txout, filter);
    }
    CoinsList listCoins() override
    {
        LOCK(m_wallet->cs_wallet);
        CoinsList result;
        for (const auto& entry : ListCoins(*m_wallet)) {
            auto& group = result[entry.first];
            for (const auto& coin : entry.second) {
                group.emplace_back(coin.outpoint,
                    MakeWalletTxOut(*m_wallet, coin));
            }
        }
        return result;
    }
    std::vector<WalletTxOut> getCoins(const std::vector<COutPoint>& outputs) override
    {
        LOCK(m_wallet->cs_wallet);
        std::vector<WalletTxOut> result;
        result.reserve(outputs.size());
        for (const auto& output : outputs) {
            result.emplace_back();
            auto it = m_wallet->mapWallet.find(output.hash);
            if (it != m_wallet->mapWallet.end()) {
                int depth = m_wallet->GetTxDepthInMainChain(it->second);
                if (depth >= 0) {
                    result.back() = MakeWalletTxOut(*m_wallet, it->second, output.n, depth);
                }
            }
        }
        return result;
    }
    CAmount getRequiredFee(unsigned int tx_bytes) override { return GetRequiredFee(*m_wallet, tx_bytes); }
    CAmount getMinimumFee(unsigned int tx_bytes,
        const CCoinControl& coin_control,
        int* returned_target,
        FeeReason* reason) override
    {
        FeeCalculation fee_calc;
        CAmount result;
        result = GetMinimumFee(*m_wallet, tx_bytes, coin_control, &fee_calc);
        if (returned_target) *returned_target = fee_calc.returnedTarget;
        if (reason) *reason = fee_calc.reason;
        return result;
    }
    unsigned int getConfirmTarget() override { return m_wallet->m_confirm_target; }
    bool hdEnabled() override { return m_wallet->IsHDEnabled(); }
    bool canGetAddresses() override { return m_wallet->CanGetAddresses(); }
    bool hasExternalSigner() override { return m_wallet->IsWalletFlagSet(WALLET_FLAG_EXTERNAL_SIGNER); }
    bool privateKeysDisabled() override { return m_wallet->IsWalletFlagSet(WALLET_FLAG_DISABLE_PRIVATE_KEYS); }
    bool taprootEnabled() override {
        if (m_wallet->IsLegacy()) return false;
        auto spk_man = m_wallet->GetScriptPubKeyMan(OutputType::BECH32M, /*internal=*/false);
        return spk_man != nullptr;
    }
    OutputType getDefaultAddressType() override { return m_wallet->m_default_address_type; }
    CAmount getDefaultMaxTxFee() override { return m_wallet->m_default_max_tx_fee; }
    void remove() override
    {
        RemoveWallet(m_context, m_wallet, /*load_on_start=*/false);
    }
    bool isLegacy() override { return m_wallet->IsLegacy(); }
    std::unique_ptr<Handler> handleUnload(UnloadFn fn) override
    {
        return MakeSignalHandler(m_wallet->NotifyUnload.connect(fn));
    }
    std::unique_ptr<Handler> handleShowProgress(ShowProgressFn fn) override
    {
        return MakeSignalHandler(m_wallet->ShowProgress.connect(fn));
    }
    std::unique_ptr<Handler> handleStatusChanged(StatusChangedFn fn) override
    {
        return MakeSignalHandler(m_wallet->NotifyStatusChanged.connect([fn](CWallet*) { fn(); }));
    }
    std::unique_ptr<Handler> handleAddressBookChanged(AddressBookChangedFn fn) override
    {
        return MakeSignalHandler(m_wallet->NotifyAddressBookChanged.connect(
            [fn](const CTxDestination& address, const std::string& label, bool is_mine,
                 AddressPurpose purpose, ChangeType status) { fn(address, label, is_mine, purpose, status); }));
    }
    std::unique_ptr<Handler> handleTransactionChanged(TransactionChangedFn fn) override
    {
        return MakeSignalHandler(m_wallet->NotifyTransactionChanged.connect(
            [fn](const uint256& txid, ChangeType status) { fn(txid, status); }));
    }
    std::unique_ptr<Handler> handleWatchOnlyChanged(WatchOnlyChangedFn fn) override
    {
        return MakeSignalHandler(m_wallet->NotifyWatchonlyChanged.connect(fn));
    }
    std::unique_ptr<Handler> handleCanGetAddressesChanged(CanGetAddressesChangedFn fn) override
    {
        return MakeSignalHandler(m_wallet->NotifyCanGetAddressesChanged.connect(fn));
    }
    CWallet* wallet() override { return m_wallet.get(); }

    WalletContext& m_context;
    std::shared_ptr<CWallet> m_wallet;


     std::vector<interfaces::Kvanta5VaultUTXO> listKvanta5VaultUTXOs() override
     {
    LOCK(m_wallet->cs_wallet);

    std::vector<interfaces::Kvanta5VaultUTXO> result;

    for (const auto& wallet_tx_pair : m_wallet->mapWallet) {
        const wallet::CWalletTx& wtx = wallet_tx_pair.second;

        if (!wtx.tx) {
            continue;
        }

        const Txid txid = wtx.tx->GetHash();
        const int confirmations = m_wallet->GetTxDepthInMainChain(wtx);

        for (unsigned int n = 0; n < wtx.tx->vout.size(); ++n) {
            const COutPoint outpoint(txid, n);

            if (m_wallet->IsSpent(outpoint)) {
                continue;
            }

            const CTxOut& txout = wtx.tx->vout[n];

            const auto spend_info = m_wallet->GetKvanta5P2QRSpendInfoForScript(txout.scriptPubKey);
            if (!spend_info || !spend_info->multisig) {
                continue;
            }

            interfaces::Kvanta5VaultUTXO utxo;
            utxo.outpoint = outpoint;
            utxo.amount = txout.nValue;
            utxo.confirmations = confirmations;
            utxo.address = EncodeDestination(spend_info->dest);
            utxo.program = HexStr(spend_info->key_hash);
            utxo.type = spend_info->wrapped_p2sh ? "Wrapped P2SH" : "Native P2QR";

            const auto address_book_it = m_wallet->m_address_book.find(spend_info->dest);
            if (address_book_it != m_wallet->m_address_book.end()) {
                utxo.label = address_book_it->second.GetLabel();
            }

            result.push_back(std::move(utxo));
        }
    }

    std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) {
        if (a.confirmations != b.confirmations) {
            return a.confirmations > b.confirmations;
        }

        if (a.amount != b.amount) {
            return a.amount > b.amount;
        }

        if (a.outpoint.hash != b.outpoint.hash) {
            return a.outpoint.hash.ToString() < b.outpoint.hash.ToString();
        }

        return a.outpoint.n < b.outpoint.n;
    });

    return result;
    }

    std::vector<interfaces::Kvanta5VaultInfo> listKvanta5Vaults() override
{
    LOCK(m_wallet->cs_wallet);

    std::map<std::string, interfaces::Kvanta5VaultInfo> vaults_by_program;

    Kvanta5P2QRScriptPubKeyMan* qr_spkm = m_wallet->GetKvanta5P2QRScriptPubKeyMan();

    /*
     * First pass: seed from the wallet's known multisig vault policies.
     *
     * This is the important part. A vault is known because the wallet has the
     * multisig policy, not because it currently has an unspent UTXO.
     */
    if (qr_spkm) {
        const auto policies = qr_spkm->ListMultisigPolicies();

        for (const auto& policy_entry : policies) {
            const uint256& program = policy_entry.first;
            const Kvanta5P2QRMultisigRecord& record = policy_entry.second;

            const Kvanta5P2QRDestination vault_dest{program};
            const std::string program_hex = HexStr(vault_dest);

            auto [it, inserted] = vaults_by_program.emplace(program_hex, interfaces::Kvanta5VaultInfo{});
            interfaces::Kvanta5VaultInfo& vault = it->second;

            if (!inserted) {
                continue;
            }

            vault.program = program_hex;
            vault.address = EncodeDestination(vault_dest);
            vault.type = "Native P2QR";
            vault.required = record.required;
            vault.total = static_cast<int>(record.signer_programs.size());
            vault.balance = 0;
            vault.utxos = 0;
            vault.spend_count = 0;

            const auto native_book_it = m_wallet->m_address_book.find(vault_dest);
            if (native_book_it != m_wallet->m_address_book.end()) {
                vault.label = native_book_it->second.GetLabel();
            } else {
                const CScript redeem_script = consensus::kvanta5p2qr::MakeRedeemScript(program);
                const CTxDestination wrapped_dest = ScriptHash(CScriptID(redeem_script));

                const auto wrapped_book_it = m_wallet->m_address_book.find(wrapped_dest);
                if (wrapped_book_it != m_wallet->m_address_book.end()) {
                    vault.label = wrapped_book_it->second.GetLabel();
                }
            }

            for (int signer_index = 0;
                 signer_index < static_cast<int>(record.signer_programs.size());
                 ++signer_index) {
                 const uint256& signer_program = record.signer_programs[signer_index];
                 const Kvanta5P2QRDestination signer_dest{signer_program};

                 interfaces::Kvanta5VaultSignerInfo signer;
                 signer.signer_index = signer_index;
                 signer.pubkey.clear();
                 signer.address = EncodeDestination(signer_dest);
                 signer.has_local_private_key = false;

                 std::vector<unsigned char> pubkey;
                 if (qr_spkm->GetPubKeyForDestination(signer_dest, pubkey)) {
                     signer.pubkey = HexStr(pubkey);

                     CPQKey key;
                     signer.has_local_private_key = qr_spkm->GetKeyByPubKey(pubkey, key);
             }

             vault.signers.push_back(std::move(signer));
            }
        }
    }

    /*
     * Second pass: add current unspent vault balances.
     */
    for (const auto& wallet_tx_pair : m_wallet->mapWallet) {
        const wallet::CWalletTx& wtx = wallet_tx_pair.second;

        if (!wtx.tx) {
            continue;
        }

        const Txid txid = wtx.tx->GetHash();

        for (unsigned int n = 0; n < wtx.tx->vout.size(); ++n) {
            const COutPoint outpoint(txid, n);

            if (m_wallet->IsSpent(outpoint)) {
                continue;
            }

            const CTxOut& txout = wtx.tx->vout[n];

            const auto spend_info = m_wallet->GetKvanta5P2QRSpendInfoForScript(txout.scriptPubKey);
            if (!spend_info || !spend_info->multisig) {
                continue;
            }

            const Kvanta5P2QRDestination vault_dest{spend_info->key_hash};
            const std::string program_hex = HexStr(vault_dest);

            auto [it, inserted] = vaults_by_program.emplace(program_hex, interfaces::Kvanta5VaultInfo{});
            interfaces::Kvanta5VaultInfo& vault = it->second;

            if (inserted) {
                vault.program = program_hex;
                vault.address = EncodeDestination(spend_info->dest);
                vault.type = spend_info->wrapped_p2sh ? "Wrapped P2SH" : "Native P2QR";
                vault.required = spend_info->multisig_required;
                vault.total = static_cast<int>(spend_info->multisig_signer_programs.size());
                vault.balance = 0;
                vault.utxos = 0;
                vault.spend_count = 0;

                const auto address_book_it = m_wallet->m_address_book.find(spend_info->dest);
                if (address_book_it != m_wallet->m_address_book.end()) {
                    vault.label = address_book_it->second.GetLabel();
                }

                if (qr_spkm) {
                    for (int signer_index = 0;
                         signer_index < static_cast<int>(spend_info->multisig_signer_programs.size());
                         ++signer_index) {
                        const uint256& signer_program =
                           spend_info->multisig_signer_programs[signer_index];

                        const Kvanta5P2QRDestination signer_dest{signer_program};

                        interfaces::Kvanta5VaultSignerInfo signer;
                        signer.signer_index = signer_index;
                        signer.pubkey.clear();
                        signer.address = EncodeDestination(signer_dest);
                        signer.has_local_private_key = false;

                        std::vector<unsigned char> pubkey;
                        if (qr_spkm->GetPubKeyForDestination(signer_dest, pubkey)) {
                            signer.pubkey = HexStr(pubkey);

                            CPQKey key;
                            signer.has_local_private_key = qr_spkm->GetKeyByPubKey(pubkey, key);
                            }

    vault.signers.push_back(std::move(signer));

                    }
                }
            }

            vault.balance += txout.nValue;
            ++vault.utxos;

            if (spend_info->wrapped_p2sh) {
                vault.type = "Wrapped P2SH";
                vault.address = EncodeDestination(spend_info->dest);
            }
        }
    }

    /*
     * Third pass: count wallet-visible spends from each vault.
     */
    for (const auto& wallet_tx_pair : m_wallet->mapWallet) {
        const wallet::CWalletTx& spending_wtx = wallet_tx_pair.second;

        if (!spending_wtx.tx) {
            continue;
        }

        for (const CTxIn& txin : spending_wtx.tx->vin) {
            const auto prev_it = m_wallet->mapWallet.find(txin.prevout.hash);
            if (prev_it == m_wallet->mapWallet.end()) {
                continue;
            }

            const wallet::CWalletTx& prev_wtx = prev_it->second;
            if (!prev_wtx.tx) {
                continue;
            }

            if (txin.prevout.n >= prev_wtx.tx->vout.size()) {
                continue;
            }

            const CTxOut& prev_txout = prev_wtx.tx->vout[txin.prevout.n];

            const auto spend_info = m_wallet->GetKvanta5P2QRSpendInfoForScript(prev_txout.scriptPubKey);
            if (!spend_info || !spend_info->multisig) {
                continue;
            }

            const Kvanta5P2QRDestination vault_dest{spend_info->key_hash};
            const std::string program_hex = HexStr(vault_dest);

            auto vault_it = vaults_by_program.find(program_hex);
            if (vault_it != vaults_by_program.end()) {
                ++vault_it->second.spend_count;
            }
        }
    }

    std::vector<interfaces::Kvanta5VaultInfo> result;
    result.reserve(vaults_by_program.size());

    for (auto& entry : vaults_by_program) {
        result.push_back(std::move(entry.second));
    }

    std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) {
        if (a.balance != b.balance) {
            return a.balance > b.balance;
        }

        if (a.label != b.label) {
            return a.label < b.label;
        }

        return a.program < b.program;
    });

    return result;
}




};


class WalletLoaderImpl : public WalletLoader
{
public:
    WalletLoaderImpl(Chain& chain, ArgsManager& args)
    {
        m_context.chain = &chain;
        m_context.args = &args;
    }
    ~WalletLoaderImpl() override { UnloadWallets(m_context); }

    //! ChainClient methods
    void registerRpcs() override
    {
        for (const CRPCCommand& command : GetWalletRPCCommands()) {
            m_rpc_commands.emplace_back(command.category, command.name, [this, &command](const JSONRPCRequest& request, UniValue& result, bool last_handler) {
                JSONRPCRequest wallet_request = request;
                wallet_request.context = &m_context;
                return command.actor(wallet_request, result, last_handler);
            }, command.argNames, command.unique_id);
            m_rpc_handlers.emplace_back(m_context.chain->handleRpc(m_rpc_commands.back()));
        }
    }
    bool verify() override { return VerifyWallets(m_context); }
    bool load() override { return LoadWallets(m_context); }
    void start(CScheduler& scheduler) override
    {
        m_context.scheduler = &scheduler;
        return StartWallets(m_context);
    }
    void flush() override { return FlushWallets(m_context); }
    void stop() override { return StopWallets(m_context); }
    void setMockTime(int64_t time) override { return SetMockTime(time); }
    void schedulerMockForward(std::chrono::seconds delta) override { Assert(m_context.scheduler)->MockForward(delta); }

    //! WalletLoader methods
    util::Result<std::unique_ptr<Wallet>> createWallet(const std::string& name, const SecureString& passphrase, uint64_t wallet_creation_flags, std::vector<bilingual_str>& warnings) override
    {
        DatabaseOptions options;
        DatabaseStatus status;
        ReadDatabaseArgs(*m_context.args, options);
        options.require_create = true;
        options.create_flags = wallet_creation_flags;
        options.create_passphrase = passphrase;
        bilingual_str error;
        std::unique_ptr<Wallet> wallet{MakeWallet(m_context, CreateWallet(m_context, name, /*load_on_start=*/true, options, status, error, warnings))};
        if (wallet) {
            return wallet;
        } else {
            return util::Error{error};
        }
    }
    util::Result<std::unique_ptr<Wallet>> loadWallet(const std::string& name, std::vector<bilingual_str>& warnings) override
    {
        DatabaseOptions options;
        DatabaseStatus status;
        ReadDatabaseArgs(*m_context.args, options);
        options.require_existing = true;
        bilingual_str error;
        std::unique_ptr<Wallet> wallet{MakeWallet(m_context, LoadWallet(m_context, name, /*load_on_start=*/true, options, status, error, warnings))};
        if (wallet) {
            return wallet;
        } else {
            return util::Error{error};
        }
    }
    util::Result<std::unique_ptr<Wallet>> restoreWallet(const fs::path& backup_file, const std::string& wallet_name, std::vector<bilingual_str>& warnings) override
    {
        DatabaseStatus status;
        bilingual_str error;
        std::unique_ptr<Wallet> wallet{MakeWallet(m_context, RestoreWallet(m_context, backup_file, wallet_name, /*load_on_start=*/true, status, error, warnings))};
        if (wallet) {
            return wallet;
        } else {
            return util::Error{error};
        }
    }
        
    bool isEncrypted(const std::string& wallet_name) override
    {
        auto wallets{GetWallets(m_context)};
        auto it = std::find_if(wallets.begin(), wallets.end(), [&](std::shared_ptr<CWallet> w){ return w->GetName() == wallet_name; });
        if (it != wallets.end()) return (*it)->IsCrypted();

        // Unloaded wallet, read db
        DatabaseOptions options;
        options.require_existing = true;
        DatabaseStatus status;
        bilingual_str error;
        auto db = MakeWalletDatabase(wallet_name, options, status, error);
        if (!db) return false;
        return WalletBatch(*db).IsEncrypted();
    }
    std::string getWalletDir() override
    {
        return fs::PathToString(GetWalletDir());
    }
    std::vector<std::pair<std::string, std::string>> listWalletDir() override
    {
        std::vector<std::pair<std::string, std::string>> paths;
        for (auto& [path, format] : ListDatabases(GetWalletDir())) {
            paths.emplace_back(fs::PathToString(path), format);
        }
        return paths;
    }
    std::vector<std::unique_ptr<Wallet>> getWallets() override
    {
        std::vector<std::unique_ptr<Wallet>> wallets;
        for (const auto& wallet : GetWallets(m_context)) {
            wallets.emplace_back(MakeWallet(m_context, wallet));
        }
        return wallets;
    }
    std::unique_ptr<Handler> handleLoadWallet(LoadWalletFn fn) override
    {
        return HandleLoadWallet(m_context, std::move(fn));
    }
    WalletContext* context() override  { return &m_context; }

    WalletContext m_context;
    const std::vector<std::string> m_wallet_filenames;
    std::vector<std::unique_ptr<Handler>> m_rpc_handlers;
    std::list<CRPCCommand> m_rpc_commands;
};
} // namespace
} // namespace wallet

namespace interfaces {
std::unique_ptr<Wallet> MakeWallet(wallet::WalletContext& context, const std::shared_ptr<wallet::CWallet>& wallet) { return wallet ? std::make_unique<wallet::WalletImpl>(context, wallet) : nullptr; }

std::unique_ptr<WalletLoader> MakeWalletLoader(Chain& chain, ArgsManager& args)
{
    return std::make_unique<wallet::WalletLoaderImpl>(chain, args);
}

} // namespace interfaces
