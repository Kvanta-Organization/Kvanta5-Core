// Copyright (c) 2015-2026 The Bitcoin Core developers
// Copyright (c) 2026 The Kvanta5 Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kvanta5-build-config.h> // IWYU pragma: keep

#include <core_io.h>
#include <key_io.h>
#include <consensus/kvanta5p2qr.h>
#include <script/kvanta5_p2qr.h>
#include <rpc/util.h>
#include <script/script.h>
#include <script/solver.h>
#include <util/bip32.h>
#include <util/translation.h>
#include <util/strencodings.h>
#include <wallet/receive.h>
#include <wallet/rpc/util.h>
#include <wallet/wallet.h>

#include <univalue.h>

namespace wallet {
RPCHelpMan getnewaddress()
{
    return RPCHelpMan{"getnewaddress",
                "\nReturns a new Kvanta5 address for receiving payments.\n"
                "By default this returns a native P2QR address.\n"
                "Use address_type \"wrapped-p2sh\" to return a BTC-style P2SH wrapped P2QR mining compatibility address.\n"
                "Legacy, P2SH-SegWit, Bech32, and Bech32m address creation is disabled in Kvanta5.\n"
                "If 'label' is specified, it is added to the address book so payments received with the address will be associated with 'label'.\n",
                {
                    {"label", RPCArg::Type::STR, RPCArg::Default{""}, "The label name for the address to be linked to. It can also be set to the empty string \"\" to represent the default label. The label does not need to exist; it will be created if there is no label by the given name."},
                    {"address_type", RPCArg::Type::STR, RPCArg::Default{"p2qr"}, "The Kvanta5 address type to use. Options are \"p2qr\" and \"wrapped-p2sh\". Legacy address types are refused."},
                },
                RPCResult{
                    RPCResult::Type::STR, "address", "The new Kvanta5 address"
                },
                RPCExamples{
                    HelpExampleCli("getnewaddress", "")
            + HelpExampleCli("getnewaddress", "\"\" \"p2qr\"")
            + HelpExampleCli("getnewaddress", "\"\" \"wrapped-p2sh\"")
            + HelpExampleRpc("getnewaddress", "")
                },
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
{
    std::shared_ptr<CWallet> const pwallet = GetWalletForJSONRPCRequest(request);
    if (!pwallet) return UniValue::VNULL;

    LOCK(pwallet->cs_wallet);

    if (!pwallet->CanGetAddresses()) {
        throw JSONRPCError(RPC_WALLET_ERROR, "Error: This wallet has no available keys");
    }

    // Parse the label first so we don't generate a key if there's an error.
    const std::string label{LabelFromValue(request.params[0])};

    bool wrapped_p2sh{false};

    if (!request.params[1].isNull()) {
        const std::string requested_type = request.params[1].get_str();

        if (requested_type == "p2qr" ||
            requested_type == "kvanta5_p2qr" ||
            requested_type == "kv5_p2qr") {
            wrapped_p2sh = false;
        } else if (requested_type == "wrapped-p2sh" ||
                   requested_type == "wrapped_p2sh" ||
                   requested_type == "p2sh-wrapped" ||
                   requested_type == "p2sh_wrapped" ||
                   requested_type == "mining" ||
                   requested_type == "mining-address" ||
                   requested_type == "mining_address") {
            wrapped_p2sh = true;
        } else {
            std::optional<OutputType> parsed = ParseOutputType(requested_type);
            if (parsed && *parsed == OutputType::KVANTA5_P2QR) {
                wrapped_p2sh = false;
            } else {
                throw JSONRPCError(
                    RPC_INVALID_ADDRESS_OR_KEY,
                    strprintf("Kvanta5 refuses to create non-P2QR address type '%s'. Use \"p2qr\" or \"wrapped-p2sh\".", requested_type)
                );
            }
        }
    }

    if (!wrapped_p2sh) {
    if (pwallet->IsWalletFlagSet(WALLET_FLAG_DISABLE_PRIVATE_KEYS)) {
        throw JSONRPCError(RPC_WALLET_ERROR, "Cannot generate KV5 P2QR addresses in a wallet with private keys disabled");
    }

    EnsureWalletIsUnlocked(*pwallet);

    auto op_dest = pwallet->GenerateNewKvanta5P2QRDestination(label);
    if (!op_dest) {
        throw JSONRPCError(RPC_WALLET_ERROR, util::ErrorString(op_dest).original);
    }

    return EncodeDestination(*op_dest);
    }

    if (pwallet->IsWalletFlagSet(WALLET_FLAG_DISABLE_PRIVATE_KEYS)) {
        throw JSONRPCError(RPC_WALLET_ERROR, "Cannot generate wrapped P2SH P2QR addresses in a wallet with private keys disabled");
    }

    EnsureWalletIsUnlocked(*pwallet);

    auto op_dest = pwallet->GenerateNewKvanta5P2QRDestination(label);
    if (!op_dest) {
        throw JSONRPCError(RPC_WALLET_ERROR, util::ErrorString(op_dest).original);
    }

    const CScript redeem_script = GetScriptForDestination(*op_dest);
    const CScriptID script_id(redeem_script);
    const CTxDestination wrapped_dest = ScriptHash(script_id);

    pwallet->SetAddressBook(wrapped_dest, label, AddressPurpose::RECEIVE);

    return EncodeDestination(wrapped_dest);
},
    };
}

RPCHelpMan getnewkvanta5p2qraddress()
{
    return RPCHelpMan{"getnewkvanta5p2qraddress",
                "\nReturns a new native Kvanta5 quantum-resistant P2QR address using ML-DSA-87.\n"
                "If 'label' is specified, it is added to the address book so payments received with the address will be associated with 'label'.\n",
                {
                    {"label", RPCArg::Type::STR, RPCArg::Default{""}, "The label name for the address to be linked to. It can also be set to the empty string \"\" to represent the default label."},
                },
                RPCResult{
                    RPCResult::Type::STR, "address", "The new native KV5 P2QR address"
                },
                RPCExamples{
                    HelpExampleCli("getnewkvanta5p2qraddress", "")
            + HelpExampleRpc("getnewkvanta5p2qraddress", "")
                },
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
{
    std::shared_ptr<CWallet> const pwallet = GetWalletForJSONRPCRequest(request);
    if (!pwallet) return UniValue::VNULL;

    if (pwallet->IsWalletFlagSet(WALLET_FLAG_DISABLE_PRIVATE_KEYS)) {
        throw JSONRPCError(RPC_WALLET_ERROR, "Cannot generate KV5 P2QR keys in a wallet with private keys disabled");
    }

    LOCK(pwallet->cs_wallet);

    EnsureWalletIsUnlocked(*pwallet);

    const std::string label{LabelFromValue(request.params[0])};

    auto op_dest = pwallet->GenerateNewKvanta5P2QRDestination(label);
    if (!op_dest) {
        throw JSONRPCError(RPC_WALLET_ERROR, util::ErrorString(op_dest).original);
    }

    return EncodeDestination(*op_dest);
},
    };
}

RPCHelpMan getnewkvanta5p2qrminingaddress()
{
    return RPCHelpMan{"getnewkvanta5p2qrminingaddress",
                "\nReturns a new wrapped P2SH Kvanta5 P2QR mining compatibility address.\n"
                "The returned address is BTC-style P2SH for pool/exchange compatibility, but its redeemScript is native Kvanta5 P2QR:\n"
                "    OP_RESERVED <32-byte P2QR program>\n"
                "Funds sent to this address are spendable only through the Kvanta5 P2QR wallet path.\n",
                {
                    {"label", RPCArg::Type::STR, RPCArg::Default{""}, "The label name for the address to be linked to. It can also be set to the empty string \"\" to represent the default label."},
                },
                RPCResult{
                    RPCResult::Type::OBJ, "", "",
                    {
                        {RPCResult::Type::STR, "address", "The wrapped P2SH mining compatibility address"},
                        {RPCResult::Type::STR, "native_address", "The paired native Kvanta5 P2QR address"},
                        {RPCResult::Type::STR_HEX, "redeemScript", "The native P2QR redeemScript carried by the P2SH wrapper"},
                        {RPCResult::Type::STR_HEX, "scriptPubKey", "The P2SH scriptPubKey for the wrapped address"},
                    }
                },
                RPCExamples{
                    HelpExampleCli("getnewkvanta5p2qrminingaddress", "")
            + HelpExampleRpc("getnewkvanta5p2qrminingaddress", "")
                },
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
{
    std::shared_ptr<CWallet> const pwallet = GetWalletForJSONRPCRequest(request);
    if (!pwallet) return UniValue::VNULL;

    if (pwallet->IsWalletFlagSet(WALLET_FLAG_DISABLE_PRIVATE_KEYS)) {
        throw JSONRPCError(RPC_WALLET_ERROR, "Cannot generate KV5 P2QR keys in a wallet with private keys disabled");
    }

    LOCK(pwallet->cs_wallet);

    EnsureWalletIsUnlocked(*pwallet);

    const std::string label{LabelFromValue(request.params[0])};

    auto op_dest = pwallet->GenerateNewKvanta5P2QRDestination(label);
    if (!op_dest) {
        throw JSONRPCError(RPC_WALLET_ERROR, util::ErrorString(op_dest).original);
    }

    const CScript redeem_script = GetScriptForDestination(*op_dest);
    const CScriptID script_id(redeem_script);
    const CTxDestination wrapped_dest = ScriptHash(script_id);
    const CScript wrapped_script_pub_key = GetScriptForDestination(wrapped_dest);

    /*
     * Add the wrapped address to the address book too, so pool/exchange users
     * see the wrapped address as an owned receive address.
     */
    pwallet->SetAddressBook(wrapped_dest, label, AddressPurpose::RECEIVE);

    UniValue result(UniValue::VOBJ);
    result.pushKV("address", EncodeDestination(wrapped_dest));
    result.pushKV("native_address", EncodeDestination(*op_dest));
    result.pushKV("redeemScript", HexStr(redeem_script));
    result.pushKV("scriptPubKey", HexStr(wrapped_script_pub_key));
    return result;
},
    };
}

RPCHelpMan getrawchangeaddress()
{
    return RPCHelpMan{"getrawchangeaddress",
                "\nReturns a new Kvanta5 P2QR address for receiving change.\n"
                "By default this returns a native P2QR address.\n"
                "Use address_type \"wrapped-p2sh\" to return a P2SH-carried "
                "P2QR compatibility address.\n"
                "Legacy, P2SH-SegWit, Bech32, and Bech32m change-address "
                "creation is disabled in Kvanta5.\n",
                {
                    {
                        "address_type",
                        RPCArg::Type::STR,
                        RPCArg::Default{"p2qr"},
                        "The Kvanta5 change-address type. Options are "
                        "\"p2qr\" and \"wrapped-p2sh\". Legacy Bitcoin "
                        "address types are refused."
                    },
                },
                RPCResult{
                    RPCResult::Type::STR,
                    "address",
                    "The new Kvanta5 P2QR change address"
                },
                RPCExamples{
                    HelpExampleCli("getrawchangeaddress", "")
                    + HelpExampleCli(
                        "getrawchangeaddress",
                        "\"p2qr\""
                    )
                    + HelpExampleCli(
                        "getrawchangeaddress",
                        "\"wrapped-p2sh\""
                    )
                    + HelpExampleRpc("getrawchangeaddress", "")
                },
        [&](const RPCHelpMan& self,
            const JSONRPCRequest& request) -> UniValue
{
    std::shared_ptr<CWallet> const pwallet =
        GetWalletForJSONRPCRequest(request);

    if (!pwallet) {
        return UniValue::VNULL;
    }

    LOCK(pwallet->cs_wallet);

    if (pwallet->IsWalletFlagSet(
            WALLET_FLAG_DISABLE_PRIVATE_KEYS)) {
        throw JSONRPCError(
            RPC_WALLET_ERROR,
            "Cannot generate Kvanta5 P2QR change addresses "
            "in a wallet with private keys disabled"
        );
    }

    EnsureWalletIsUnlocked(*pwallet);

    const std::string requested_type =
        request.params[0].isNull()
            ? "p2qr"
            : request.params[0].get_str();

    bool wrapped_p2sh{false};

    if (requested_type == "p2qr" ||
        requested_type == "kvanta5-p2qr" ||
        requested_type == "kvanta5_p2qr" ||
        requested_type == "kv5-p2qr" ||
        requested_type == "kv5_p2qr") {
        wrapped_p2sh = false;
    } else if (
        requested_type == "wrapped-p2sh" ||
        requested_type == "wrapped_p2sh" ||
        requested_type == "p2sh-wrapped" ||
        requested_type == "p2sh_wrapped" ||
        requested_type == "mining" ||
        requested_type == "mining-address" ||
        requested_type == "mining_address") {
        wrapped_p2sh = true;
    } else {
        throw JSONRPCError(
            RPC_INVALID_ADDRESS_OR_KEY,
            strprintf(
                "Kvanta5 refuses to create non-P2QR change "
                "address type '%s'. Use \"p2qr\" or "
                "\"wrapped-p2sh\".",
                requested_type
            )
        );
    }

    auto op_script =
        wrapped_p2sh
            ? pwallet
                  ->GetNewKvanta5P2QRCompatibilityChangeScript()
            : pwallet->GetNewKvanta5P2QRChangeScript();

    if (!op_script) {
        throw JSONRPCError(
            RPC_WALLET_ERROR,
            util::ErrorString(op_script).original
        );
    }

    CTxDestination destination;

    if (!ExtractDestination(*op_script, destination)) {
        throw JSONRPCError(
            RPC_WALLET_ERROR,
            "Generated Kvanta5 P2QR change script could not "
            "be converted to an address"
        );
    }

    return EncodeDestination(destination);
},
    };
}


RPCHelpMan setlabel()
{
    return RPCHelpMan{"setlabel",
                "\nSets the label associated with the given address.\n",
                {
                    {"address", RPCArg::Type::STR, RPCArg::Optional::NO, "The kvanta5 address to be associated with a label."},
                    {"label", RPCArg::Type::STR, RPCArg::Optional::NO, "The label to assign to the address."},
                },
                RPCResult{RPCResult::Type::NONE, "", ""},
                RPCExamples{
                    HelpExampleCli("setlabel", "\"" + EXAMPLE_ADDRESS[0] + "\" \"tabby\"")
            + HelpExampleRpc("setlabel", "\"" + EXAMPLE_ADDRESS[0] + "\", \"tabby\"")
                },
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
{
    std::shared_ptr<CWallet> const pwallet = GetWalletForJSONRPCRequest(request);
    if (!pwallet) return UniValue::VNULL;

    LOCK(pwallet->cs_wallet);

    CTxDestination dest = DecodeDestination(request.params[0].get_str());
    if (!IsValidDestination(dest)) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid Kvanta5 address");
    }

    const std::string label{LabelFromValue(request.params[1])};

    if (pwallet->IsMine(dest)) {
        pwallet->SetAddressBook(dest, label, AddressPurpose::RECEIVE);
    } else {
        pwallet->SetAddressBook(dest, label, AddressPurpose::SEND);
    }

    return UniValue::VNULL;
},
    };
}

RPCHelpMan listaddressgroupings()
{
    return RPCHelpMan{"listaddressgroupings",
                "\nLists groups of addresses which have had their common ownership\n"
                "made public by common use as inputs or as the resulting change\n"
                "in past transactions\n",
                {},
                RPCResult{
                    RPCResult::Type::ARR, "", "",
                    {
                        {RPCResult::Type::ARR, "", "",
                        {
                            {RPCResult::Type::ARR_FIXED, "", "",
                            {
                                {RPCResult::Type::STR, "address", "The kvanta5 address"},
                                {RPCResult::Type::STR_AMOUNT, "amount", "The amount in " + CURRENCY_UNIT},
                                {RPCResult::Type::STR, "label", /*optional=*/true, "The label"},
                            }},
                        }},
                    }
                },
                RPCExamples{
                    HelpExampleCli("listaddressgroupings", "")
            + HelpExampleRpc("listaddressgroupings", "")
                },
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
{
    const std::shared_ptr<const CWallet> pwallet = GetWalletForJSONRPCRequest(request);
    if (!pwallet) return UniValue::VNULL;

    // Make sure the results are valid at least up to the most recent block
    // the user could have gotten from another RPC command prior to now
    pwallet->BlockUntilSyncedToCurrentChain();

    LOCK(pwallet->cs_wallet);

    UniValue jsonGroupings(UniValue::VARR);
    std::map<CTxDestination, CAmount> balances = GetAddressBalances(*pwallet);
    for (const std::set<CTxDestination>& grouping : GetAddressGroupings(*pwallet)) {
        UniValue jsonGrouping(UniValue::VARR);
        for (const CTxDestination& address : grouping)
        {
            UniValue addressInfo(UniValue::VARR);
            addressInfo.push_back(EncodeDestination(address));
            addressInfo.push_back(ValueFromAmount(balances[address]));
            {
                const auto* address_book_entry = pwallet->FindAddressBookEntry(address);
                if (address_book_entry) {
                    addressInfo.push_back(address_book_entry->GetLabel());
                }
            }
            jsonGrouping.push_back(std::move(addressInfo));
        }
        jsonGroupings.push_back(std::move(jsonGrouping));
    }
    return jsonGroupings;
},
    };
}

static std::vector<unsigned char> Kvanta5ParseP2QRPubKeyHex(const UniValue& value, const std::string& field_name)
{
    if (!value.isStr()) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("%s must be a hex string", field_name));
    }

    const std::string hex = value.get_str();
    if (!IsHex(hex)) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("%s must be hex", field_name));
    }

    std::vector<unsigned char> pubkey = ParseHex(hex);
    if (pubkey.size() != KVANTA5_P2QR_PUBKEY_SIZE) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf(
            "%s has invalid ML-DSA-87 public key size: got %u bytes, expected %u bytes",
            field_name,
            static_cast<unsigned int>(pubkey.size()),
            static_cast<unsigned int>(KVANTA5_P2QR_PUBKEY_SIZE)));
    }

    return pubkey;
}

static UniValue Kvanta5P2QRPubkeysToJSON(const std::vector<std::vector<unsigned char>>& pubkeys)
{
    UniValue arr(UniValue::VARR);
    for (const auto& pubkey : pubkeys) {
        arr.push_back(HexStr(pubkey));
    }
    return arr;
}

RPCHelpMan createp2qrmultisig()
{
    return RPCHelpMan{"createp2qrmultisig",
        "\nCreate and store a wallet-owned Kvanta5 P2QR multisig policy.\n"
        "\nThe returned native address is a Kvanta5 P2QR address whose 32-byte program commits to\n"
        "the threshold and sorted signer P2QR address/program set. The wallet stores the policy so it can\n"
        "recognize, sign, and spend funds sent to this multisig address.\n",
        {
            {"nrequired", RPCArg::Type::NUM, RPCArg::Optional::NO, "The number of required ML-DSA-87 signatures."},
            {"signer_addresses", RPCArg::Type::ARR, RPCArg::Optional::NO, "The Kvanta5 P2QR signer addresses.",
            {
                {"signer_address", RPCArg::Type::STR, RPCArg::Optional::OMITTED, "A Kvanta5 P2QR signer address."},
            }},
            {"label", RPCArg::Type::STR, RPCArg::Default{""}, "An optional label for the native multisig address."},
            {"include_wrapped", RPCArg::Type::BOOL, RPCArg::Default{true}, "Also return the P2SH-carried mining compatibility wrapper."},
        },
        RPCResult{
            RPCResult::Type::OBJ, "", "",
            {
                {RPCResult::Type::STR, "address", "The native Kvanta5 P2QR multisig address."},
                {RPCResult::Type::STR_HEX, "program", "The 32-byte P2QR multisig program."},
                {RPCResult::Type::STR_HEX, "scriptPubKey", "The native P2QR scriptPubKey."},
                {RPCResult::Type::NUM, "required", "Required signatures."},
                {RPCResult::Type::NUM, "total", "Total signer addresses."},
                {RPCResult::Type::ARR, "signer_addresses", "The sorted Kvanta5 P2QR signer addresses committed by the P2QR program.",
                {
                    {RPCResult::Type::STR, "signer_address", "A Kvanta5 P2QR signer address."},
                }},
                {RPCResult::Type::ARR, "signer_programs", "The sorted 32-byte P2QR signer programs committed by the P2QR program.",
                {
                    {RPCResult::Type::STR_HEX, "signer_program", "A 32-byte P2QR signer program."},
                }},
                {RPCResult::Type::OBJ, "wrapped", /*optional=*/true, "The P2SH-carried P2QR mining compatibility wrapper.",
                {
                    {RPCResult::Type::STR, "address", "The P2SH-carried compatibility address."},
                    {RPCResult::Type::STR_HEX, "redeemScript", "The P2QR redeemScript."},
                    {RPCResult::Type::STR_HEX, "scriptPubKey", "The P2SH scriptPubKey."},
                }},
            }
        },
        RPCExamples{
            HelpExampleCli("createp2qrmultisig", "2 '[\"kvqr1...\",\"kvqr1...\",\"kvqr1...\"]'") +
            HelpExampleRpc("createp2qrmultisig", "2, [\"kvqr1...\",\"kvqr1...\",\"kvqr1...\"]")
        },
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
{
    std::shared_ptr<CWallet> const pwallet = GetWalletForJSONRPCRequest(request);
    if (!pwallet) return UniValue::VNULL;

    const int required_int = request.params[0].getInt<int>();
    if (required_int <= 0 || required_int > std::numeric_limits<uint16_t>::max()) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "nrequired must be between 1 and 65535");
    }

    const UniValue& signer_addresses_value = request.params[1].get_array();
    if (signer_addresses_value.empty()) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "signer_addresses must not be empty");
    }

    if (signer_addresses_value.size() > KVANTA5_P2QR_MULTISIG_MAX_KEYS) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf(
            "too many signer addresses: got %u, maximum is %u",
            signer_addresses_value.size(),
            KVANTA5_P2QR_MULTISIG_MAX_KEYS));
    }

    std::vector<uint256> signer_programs;
    signer_programs.reserve(signer_addresses_value.size());

    for (unsigned int i = 0; i < signer_addresses_value.size(); ++i) {
        if (!signer_addresses_value[i].isStr()) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("signer_addresses[%u] must be a string", i));
        }

        const std::string signer_address = signer_addresses_value[i].get_str();
        const CTxDestination signer_dest = DecodeDestination(signer_address);

        if (!std::holds_alternative<Kvanta5P2QRDestination>(signer_dest)) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("signer_addresses[%u] is not a Kvanta5 P2QR address", i));
        }

        const Kvanta5P2QRDestination p2qr_signer_dest = std::get<Kvanta5P2QRDestination>(signer_dest);

        uint256 signer_program;
        signer_program.SetNull();
        std::copy(p2qr_signer_dest.begin(), p2qr_signer_dest.end(), signer_program.begin());

        signer_programs.push_back(signer_program);
    }

    std::sort(signer_programs.begin(), signer_programs.end());

    const uint16_t required = static_cast<uint16_t>(required_int);

    std::string policy_error;
    if (!Kvanta5IsCanonicalP2QRMultisigPolicy(required, signer_programs, &policy_error)) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("invalid P2QR multisig signer address policy: %s", policy_error));
    }

    const std::string label = request.params[2].isNull() ? "" : LabelFromValue(request.params[2]);
    const bool include_wrapped = request.params[3].isNull() ? true : request.params[3].get_bool();

    LOCK(pwallet->cs_wallet);

    util::Result<Kvanta5P2QRDestination> op_dest = pwallet->AddKvanta5P2QRMultisig(required, signer_programs, label);
    if (!op_dest) {
        throw JSONRPCError(RPC_WALLET_ERROR, util::ErrorString(op_dest).original);
    }

    const Kvanta5P2QRDestination dest = *op_dest;

    uint256 program;
    program.SetNull();
    std::copy(dest.begin(), dest.end(), program.begin());

    const CScript native_script = GetScriptForDestination(dest);
    const CScript redeem_script = consensus::kvanta5p2qr::MakeRedeemScript(program);
    const CScriptID script_id(redeem_script);
    const CTxDestination wrapped_dest = ScriptHash(script_id);
    const CScript wrapped_script = GetScriptForDestination(wrapped_dest);

    UniValue signer_addresses(UniValue::VARR);
    UniValue signer_programs_json(UniValue::VARR);

    for (const uint256& signer_program : signer_programs) {
        const Kvanta5P2QRDestination signer_dest{signer_program};

        signer_addresses.push_back(EncodeDestination(signer_dest));
        signer_programs_json.push_back(HexStr(signer_program));
    }

    UniValue ret(UniValue::VOBJ);
    ret.pushKV("address", EncodeDestination(dest));
    ret.pushKV("program", HexStr(dest));
    ret.pushKV("scriptPubKey", HexStr(native_script));
    ret.pushKV("required", required);
    ret.pushKV("total", static_cast<int>(signer_programs.size()));
    ret.pushKV("signer_addresses", std::move(signer_addresses));
    ret.pushKV("signer_programs", std::move(signer_programs_json));

    if (include_wrapped) {
        UniValue wrapped(UniValue::VOBJ);
        wrapped.pushKV("address", EncodeDestination(wrapped_dest));
        wrapped.pushKV("redeemScript", HexStr(redeem_script));
        wrapped.pushKV("scriptPubKey", HexStr(wrapped_script));
        ret.pushKV("wrapped", std::move(wrapped));
    }

    return ret;
},
    };
}

RPCHelpMan addmultisigaddress()
{
    return RPCHelpMan{"addmultisigaddress",
                "\nAdd an nrequired-to-sign multisignature address to the wallet. Requires a new wallet backup.\n"
                "Each key is a Kvanta5 address or hex-encoded public key.\n"
                "This functionality is only intended for use with non-watchonly addresses.\n"
                "See `importaddress` for watchonly p2sh address support.\n"
                "If 'label' is specified, assign address to that label.\n"
                "Note: This command is only compatible with legacy wallets.\n",
                {
                    {"nrequired", RPCArg::Type::NUM, RPCArg::Optional::NO, "The number of required signatures out of the n keys or addresses."},
                    {"keys", RPCArg::Type::ARR, RPCArg::Optional::NO, "The kvanta5 addresses or hex-encoded public keys",
                        {
                            {"key", RPCArg::Type::STR, RPCArg::Optional::OMITTED, "kvanta5 address or hex-encoded public key"},
                        },
                        },
                    {"label", RPCArg::Type::STR, RPCArg::Optional::OMITTED, "A label to assign the addresses to."},
                    {"address_type", RPCArg::Type::STR, RPCArg::DefaultHint{"set by -addresstype"}, "The address type to use. Options are \"legacy\", \"p2sh-segwit\", and \"bech32\"."},
                },
                RPCResult{
                    RPCResult::Type::OBJ, "", "",
                    {
                        {RPCResult::Type::STR, "address", "The value of the new multisig address"},
                        {RPCResult::Type::STR_HEX, "redeemScript", "The string value of the hex-encoded redemption script"},
                        {RPCResult::Type::STR, "descriptor", "The descriptor for this multisig"},
                        {RPCResult::Type::ARR, "warnings", /*optional=*/true, "Any warnings resulting from the creation of this multisig",
                        {
                            {RPCResult::Type::STR, "", ""},
                        }},
                    }
                },
                RPCExamples{
            "\nAdd a multisig address from 2 addresses\n"
            + HelpExampleCli("addmultisigaddress", "2 \"[\\\"" + EXAMPLE_ADDRESS[0] + "\\\",\\\"" + EXAMPLE_ADDRESS[1] + "\\\"]\"") +
            "\nAs a JSON-RPC call\n"
            + HelpExampleRpc("addmultisigaddress", "2, \"[\\\"" + EXAMPLE_ADDRESS[0] + "\\\",\\\"" + EXAMPLE_ADDRESS[1] + "\\\"]\"")
                },
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
{
    std::shared_ptr<CWallet> const pwallet = GetWalletForJSONRPCRequest(request);
    if (!pwallet) return UniValue::VNULL;

    LegacyScriptPubKeyMan& spk_man = EnsureLegacyScriptPubKeyMan(*pwallet);

    LOCK2(pwallet->cs_wallet, spk_man.cs_KeyStore);

    const std::string label{LabelFromValue(request.params[2])};

    int required = request.params[0].getInt<int>();

    // Get the public keys
    const UniValue& keys_or_addrs = request.params[1].get_array();
    std::vector<CPubKey> pubkeys;
    for (unsigned int i = 0; i < keys_or_addrs.size(); ++i) {
        if (IsHex(keys_or_addrs[i].get_str()) && (keys_or_addrs[i].get_str().length() == 66 || keys_or_addrs[i].get_str().length() == 130)) {
            pubkeys.push_back(HexToPubKey(keys_or_addrs[i].get_str()));
        } else {
            pubkeys.push_back(AddrToPubKey(spk_man, keys_or_addrs[i].get_str()));
        }
    }

    OutputType output_type = pwallet->m_default_address_type;
    if (!request.params[3].isNull()) {
        std::optional<OutputType> parsed = ParseOutputType(request.params[3].get_str());
        if (!parsed) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, strprintf("Unknown address type '%s'", request.params[3].get_str()));
        } else if (parsed.value() == OutputType::BECH32M) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Bech32m multisig addresses cannot be created with legacy wallets");
        }
        output_type = parsed.value();
    }

    // Construct multisig scripts
    FlatSigningProvider provider;
    CScript inner;
    CTxDestination dest = AddAndGetMultisigDestination(required, pubkeys, output_type, provider, inner);

    // Import scripts into the wallet
    for (const auto& [id, script] : provider.scripts) {
        // Due to a bug in the legacy wallet, the p2sh maximum script size limit is also imposed on 'p2sh-segwit' and 'bech32' redeem scripts.
        // Even when redeem scripts over MAX_SCRIPT_ELEMENT_SIZE bytes are valid for segwit output types, we don't want to
        // enable it because:
        // 1) It introduces a compatibility-breaking change requiring downgrade protection; older wallets would be unable to interact with these "new" legacy wallets.
        // 2) Considering the ongoing deprecation of the legacy spkm, this issue adds another good reason to transition towards descriptors.
        if (script.size() > MAX_SCRIPT_ELEMENT_SIZE) throw JSONRPCError(RPC_WALLET_ERROR, "Unsupported multisig script size for legacy wallet. Upgrade to descriptors to overcome this limitation for p2sh-segwit or bech32 scripts");

        if (!spk_man.AddCScript(script)) {
            if (CScript inner_script; spk_man.GetCScript(CScriptID(script), inner_script)) {
                CHECK_NONFATAL(inner_script == script); // Nothing to add, script already contained by the wallet
                continue;
            }
            throw JSONRPCError(RPC_WALLET_ERROR, strprintf("Error importing script into the wallet"));
        }
    }

    // Store destination in the addressbook
    pwallet->SetAddressBook(dest, label, AddressPurpose::SEND);

    // Make the descriptor
    std::unique_ptr<Descriptor> descriptor = InferDescriptor(GetScriptForDestination(dest), spk_man);

    UniValue result(UniValue::VOBJ);
    result.pushKV("address", EncodeDestination(dest));
    result.pushKV("redeemScript", HexStr(inner));
    result.pushKV("descriptor", descriptor->ToString());

    UniValue warnings(UniValue::VARR);
    if (descriptor->GetOutputType() != output_type) {
        // Only warns if the user has explicitly chosen an address type we cannot generate
        warnings.push_back("Unable to make chosen address type, please ensure no uncompressed public keys are present.");
    }
    PushWarnings(warnings, result);

    return result;
},
    };
}

RPCHelpMan keypoolrefill()
{
    return RPCHelpMan{"keypoolrefill",
                "\nFills the keypool."+
        HELP_REQUIRING_PASSPHRASE,
                {
                    {"newsize", RPCArg::Type::NUM, RPCArg::DefaultHint{strprintf("%u, or as set by -keypool", DEFAULT_KEYPOOL_SIZE)}, "The new keypool size"},
                },
                RPCResult{RPCResult::Type::NONE, "", ""},
                RPCExamples{
                    HelpExampleCli("keypoolrefill", "")
            + HelpExampleRpc("keypoolrefill", "")
                },
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
{
    std::shared_ptr<CWallet> const pwallet = GetWalletForJSONRPCRequest(request);
    if (!pwallet) return UniValue::VNULL;

    if (pwallet->IsLegacy() && pwallet->IsWalletFlagSet(WALLET_FLAG_DISABLE_PRIVATE_KEYS)) {
        throw JSONRPCError(RPC_WALLET_ERROR, "Error: Private keys are disabled for this wallet");
    }

    LOCK(pwallet->cs_wallet);

    // 0 is interpreted by TopUpKeyPool() as the default keypool size given by -keypool
    unsigned int kpSize = 0;
    if (!request.params[0].isNull()) {
        if (request.params[0].getInt<int>() < 0)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, expected valid size.");
        kpSize = (unsigned int)request.params[0].getInt<int>();
    }

    EnsureWalletIsUnlocked(*pwallet);
    pwallet->TopUpKeyPool(kpSize);

    if (pwallet->GetKeyPoolSize() < kpSize) {
        throw JSONRPCError(RPC_WALLET_ERROR, "Error refreshing keypool.");
    }

    return UniValue::VNULL;
},
    };
}

RPCHelpMan newkeypool()
{
    return RPCHelpMan{"newkeypool",
                "\nEntirely clears and refills the keypool.\n"
                "WARNING: On non-HD wallets, this will require a new backup immediately, to include the new keys.\n"
                "When restoring a backup of an HD wallet created before the newkeypool command is run, funds received to\n"
                "new addresses may not appear automatically. They have not been lost, but the wallet may not find them.\n"
                "This can be fixed by running the newkeypool command on the backup and then rescanning, so the wallet\n"
                "re-generates the required keys." +
            HELP_REQUIRING_PASSPHRASE,
                {},
                RPCResult{RPCResult::Type::NONE, "", ""},
                RPCExamples{
            HelpExampleCli("newkeypool", "")
            + HelpExampleRpc("newkeypool", "")
                },
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
{
    std::shared_ptr<CWallet> const pwallet = GetWalletForJSONRPCRequest(request);
    if (!pwallet) return UniValue::VNULL;

    LOCK(pwallet->cs_wallet);

    LegacyScriptPubKeyMan& spk_man = EnsureLegacyScriptPubKeyMan(*pwallet, true);
    spk_man.NewKeyPool();

    return UniValue::VNULL;
},
    };
}


class DescribeWalletAddressVisitor
{
public:
    const SigningProvider * const provider;

    // NOLINTNEXTLINE(misc-no-recursion)
    void ProcessSubScript(const CScript& subscript, UniValue& obj) const
    {
        // Always present: script type and redeemscript
        std::vector<std::vector<unsigned char>> solutions_data;
        TxoutType which_type = Solver(subscript, solutions_data);
        obj.pushKV("script", GetTxnOutputType(which_type));
        obj.pushKV("hex", HexStr(subscript));

        CTxDestination embedded;
        if (ExtractDestination(subscript, embedded)) {
            // Only when the script corresponds to an address.
            UniValue subobj(UniValue::VOBJ);
            UniValue detail = DescribeAddress(embedded);
            subobj.pushKVs(std::move(detail));
            UniValue wallet_detail = std::visit(*this, embedded);
            subobj.pushKVs(std::move(wallet_detail));
            subobj.pushKV("address", EncodeDestination(embedded));
            subobj.pushKV("scriptPubKey", HexStr(subscript));
            // Always report the pubkey at the top level, so that `getnewaddress()['pubkey']` always works.
            if (subobj.exists("pubkey")) obj.pushKV("pubkey", subobj["pubkey"]);
            obj.pushKV("embedded", std::move(subobj));
        } else if (which_type == TxoutType::MULTISIG) {
            // Also report some information on multisig scripts (which do not have a corresponding address).
            obj.pushKV("sigsrequired", solutions_data[0][0]);
            UniValue pubkeys(UniValue::VARR);
            for (size_t i = 1; i < solutions_data.size() - 1; ++i) {
                CPubKey key(solutions_data[i].begin(), solutions_data[i].end());
                pubkeys.push_back(HexStr(key));
            }
            obj.pushKV("pubkeys", std::move(pubkeys));
        }
    }

    explicit DescribeWalletAddressVisitor(const SigningProvider* _provider) : provider(_provider) {}

    UniValue operator()(const CNoDestination& dest) const { return UniValue(UniValue::VOBJ); }
    UniValue operator()(const PubKeyDestination& dest) const { return UniValue(UniValue::VOBJ); }

    UniValue operator()(const PKHash& pkhash) const
    {
        CKeyID keyID{ToKeyID(pkhash)};
        UniValue obj(UniValue::VOBJ);
        CPubKey vchPubKey;
        if (provider && provider->GetPubKey(keyID, vchPubKey)) {
            obj.pushKV("pubkey", HexStr(vchPubKey));
            obj.pushKV("iscompressed", vchPubKey.IsCompressed());
        }
        return obj;
    }

    // NOLINTNEXTLINE(misc-no-recursion)
    UniValue operator()(const ScriptHash& scripthash) const
    {
        UniValue obj(UniValue::VOBJ);
        CScript subscript;
        if (provider && provider->GetCScript(ToScriptID(scripthash), subscript)) {
            ProcessSubScript(subscript, obj);
        }
        return obj;
    }

    UniValue operator()(const WitnessV0KeyHash& id) const
    {
        UniValue obj(UniValue::VOBJ);
        CPubKey pubkey;
        if (provider && provider->GetPubKey(ToKeyID(id), pubkey)) {
            obj.pushKV("pubkey", HexStr(pubkey));
        }
        return obj;
    }

    // NOLINTNEXTLINE(misc-no-recursion)
    UniValue operator()(const WitnessV0ScriptHash& id) const
    {
        UniValue obj(UniValue::VOBJ);
        CScript subscript;
        CRIPEMD160 hasher;
        uint160 hash;
        hasher.Write(id.begin(), 32).Finalize(hash.begin());
        if (provider && provider->GetCScript(CScriptID(hash), subscript)) {
            ProcessSubScript(subscript, obj);
        }
        return obj;
    }
    
    UniValue operator()(const Kvanta5P2QRDestination& id) const
    {
    return UniValue(UniValue::VOBJ);
    }

    UniValue operator()(const WitnessV1Taproot& id) const { return UniValue(UniValue::VOBJ); }
    UniValue operator()(const PayToAnchor& id) const { return UniValue(UniValue::VOBJ); }
    UniValue operator()(const WitnessUnknown& id) const { return UniValue(UniValue::VOBJ); }
};

static UniValue DescribeWalletAddress(const CWallet& wallet, const CTxDestination& dest)
{
    UniValue ret(UniValue::VOBJ);
    UniValue detail = DescribeAddress(dest);
    CScript script = GetScriptForDestination(dest);
    std::unique_ptr<SigningProvider> provider = nullptr;
    provider = wallet.GetSolvingProvider(script);
    ret.pushKVs(std::move(detail));
    ret.pushKVs(std::visit(DescribeWalletAddressVisitor(provider.get()), dest));
    return ret;
}


RPCHelpMan getaddressinfo()
{
    return RPCHelpMan{"getaddressinfo",
                "\nReturn information about the given kvanta5 address.\n"
                "Some of the information will only be present if the address is in the active wallet.\n",
                {
                    {"address", RPCArg::Type::STR, RPCArg::Optional::NO, "The kvanta5 address for which to get information."},
                },
                RPCResult{
                    RPCResult::Type::OBJ, "", "",
                    {
                        {RPCResult::Type::STR, "address", "The kvanta5 address validated."},
                        {RPCResult::Type::STR_HEX, "scriptPubKey", "The hex-encoded output script generated by the address."},
                        {RPCResult::Type::BOOL, "ismine", "If the address is yours."},
                        {RPCResult::Type::BOOL, "iswatchonly", "If the address is watchonly."},
                        {RPCResult::Type::BOOL, "solvable", "If we know how to spend coins sent to this address, ignoring the possible lack of private keys."},
                        {RPCResult::Type::STR, "desc", /*optional=*/true, "A descriptor for spending coins sent to this address (only when solvable)."},
                        {RPCResult::Type::STR, "parent_desc", /*optional=*/true, "The descriptor used to derive this address if this is a descriptor wallet"},
                        {RPCResult::Type::BOOL, "isscript", /*optional=*/true, "If the key is a script."},
                        {RPCResult::Type::BOOL, "ischange", "If the address was used for change output."},
                        {RPCResult::Type::BOOL, "iswitness", "If the address is a witness address."},
                        {RPCResult::Type::NUM, "witness_version", /*optional=*/true, "The version number of the witness program."},
                        {RPCResult::Type::STR_HEX, "witness_program", /*optional=*/true, "The hex value of the witness program."},
                        {RPCResult::Type::STR, "script", /*optional=*/true, "The output script type. Only if isscript is true and the redeemscript is known. Possible\n"
                                                                     "types: nonstandard, pubkey, pubkeyhash, scripthash, multisig, nulldata, witness_v0_keyhash,\n"
                                                                     "witness_v0_scripthash, witness_unknown."},
                        {RPCResult::Type::STR_HEX, "hex", /*optional=*/true, "The redeemscript for the p2sh address."},
                        {RPCResult::Type::ARR, "pubkeys", /*optional=*/true, "Array of pubkeys associated with the known redeemscript (only if script is multisig).",
                        {
                            {RPCResult::Type::STR, "pubkey", ""},
                        }},
                        {RPCResult::Type::NUM, "sigsrequired", /*optional=*/true, "The number of signatures required to spend multisig output (only if script is multisig)."},
                        {RPCResult::Type::STR_HEX, "pubkey", /*optional=*/true, "The hex value of the raw public key for single-key addresses (possibly embedded in P2SH or P2WSH)."},
                        {RPCResult::Type::OBJ, "embedded", /*optional=*/true, "Information about the address embedded in P2SH or P2WSH, if relevant and known.",
                        {
                            {RPCResult::Type::ELISION, "", "Includes all getaddressinfo output fields for the embedded address, excluding metadata (timestamp, hdkeypath, hdseedid)\n"
                                                           "and relation to the wallet (ismine, iswatchonly)."},
                        }},
                        {RPCResult::Type::BOOL, "iscompressed", /*optional=*/true, "If the pubkey is compressed."},

                        {RPCResult::Type::BOOL, "iskvanta5p2qr", /*optional=*/true, "If this is a wallet-solvable Kvanta5 P2QR output."},
                        {RPCResult::Type::STR, "p2qr_type", /*optional=*/true, "Kvanta5 P2QR type: single or multisig."},
                        {RPCResult::Type::STR_HEX, "p2qr_program", /*optional=*/true, "The 32-byte Kvanta5 P2QR program."},
                        {RPCResult::Type::STR_HEX, "p2qr_pubkey", /*optional=*/true, "The ML-DSA-87 public key for a wallet-owned single-key P2QR address."},
                        {RPCResult::Type::NUM, "p2qr_sigsrequired", /*optional=*/true, "The number of required ML-DSA-87 signatures for P2QR multisig."},
                        {RPCResult::Type::NUM, "p2qr_total_signers", /*optional=*/true, "The total number of committed P2QR signer addresses for P2QR multisig."},
                        {RPCResult::Type::ARR, "p2qr_signer_addresses", /*optional=*/true, "The committed P2QR signer addresses for P2QR multisig.",
                        {
                            {RPCResult::Type::STR, "address", "The signer P2QR address"},
                        }},
                        {RPCResult::Type::ARR, "p2qr_signer_programs", /*optional=*/true, "The committed 32-byte P2QR signer programs for P2QR multisig.",
                        {
                        {RPCResult::Type::STR_HEX, "program", "The signer P2QR program"},
                        }},
                        {RPCResult::Type::STR_HEX, "native_scriptPubKey", /*optional=*/true, "The native P2QR scriptPubKey."},
                        {RPCResult::Type::STR, "mining_type", /*optional=*/true, "The mining compatibility wrapper type, if any."},
                        {RPCResult::Type::STR_HEX, "mining_redeemScript", /*optional=*/true, "The P2SH-carried P2QR redeemScript, if any."},
                        {RPCResult::Type::STR_HEX, "mining_scriptPubKey", /*optional=*/true, "The P2SH-carried P2QR scriptPubKey, if any."},

                        {RPCResult::Type::NUM_TIME, "timestamp", /*optional=*/true, "The creation time of the key, if available, expressed in " + UNIX_EPOCH_TIME + "."},
                        {RPCResult::Type::STR, "hdkeypath", /*optional=*/true, "The HD keypath, if the key is HD and available."},
                        {RPCResult::Type::STR_HEX, "hdseedid", /*optional=*/true, "The Hash160 of the HD seed."},
                        {RPCResult::Type::STR_HEX, "hdmasterfingerprint", /*optional=*/true, "The fingerprint of the master key."},
                        {RPCResult::Type::ARR, "labels", "Array of labels associated with the address. Currently limited to one label but returned\n"
                                                         "as an array to keep the API stable if multiple labels are enabled in the future.",
                        {
                            {RPCResult::Type::STR, "label name", "Label name (defaults to \"\")."},
                        }},
                    }
                },
                RPCExamples{
                    HelpExampleCli("getaddressinfo", "\"" + EXAMPLE_ADDRESS[0] + "\"") +
                    HelpExampleRpc("getaddressinfo", "\"" + EXAMPLE_ADDRESS[0] + "\"")
                },
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
{
    const std::shared_ptr<const CWallet> pwallet = GetWalletForJSONRPCRequest(request);
    if (!pwallet) return UniValue::VNULL;

    LOCK(pwallet->cs_wallet);

    std::string error_msg;
    CTxDestination dest = DecodeDestination(request.params[0].get_str(), error_msg);

    // Make sure the destination is valid.
    if (!IsValidDestination(dest)) {
        if (error_msg.empty()) error_msg = "Invalid address";
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, error_msg);
    }

    UniValue ret(UniValue::VOBJ);

    const std::string currentAddress = EncodeDestination(dest);
    ret.pushKV("address", currentAddress);

    const CScript scriptPubKey = GetScriptForDestination(dest);
    ret.pushKV("scriptPubKey", HexStr(scriptPubKey));

    std::unique_ptr<SigningProvider> provider = pwallet->GetSolvingProvider(scriptPubKey);

    isminetype mine = pwallet->IsMine(dest);
    ret.pushKV("ismine", bool(mine & ISMINE_SPENDABLE));

    bool solvable{false};

    if (provider) {
        auto inferred = InferDescriptor(scriptPubKey, *provider);
        solvable = inferred->IsSolvable();

        if (solvable) {
            ret.pushKV("desc", inferred->ToString());
        }
    }

    const auto push_p2qr_details = [&](const Kvanta5P2QRSpendInfo& info) {
        std::vector<std::vector<unsigned char>> redeem_solutions;
        const TxoutType redeem_type = Solver(info.redeem_script, redeem_solutions);

        if (redeem_type == TxoutType::KVANTA5_P2QR && redeem_solutions.size() == 1 && redeem_solutions[0].size() == 32) {
            // Display the exact 32 bytes committed inside:
            //   50 20 <program>
            //
            // Do not use uint256::GetHex() here. That is integer-display/endian
            // formatting, not script byte-order formatting.
            ret.pushKV("p2qr_program", HexStr(redeem_solutions[0]));
        } else {
            ret.pushKV("p2qr_program", HexStr(info.key_hash));
        }

        if (info.multisig) {
            ret.pushKV("p2qr_type", "multisig");
            ret.pushKV("p2qr_sigsrequired", info.multisig_required);
            ret.pushKV("p2qr_total_signers", static_cast<int>(info.multisig_signer_programs.size()));

            UniValue p2qr_signers(UniValue::VARR);
            UniValue p2qr_signer_programs(UniValue::VARR);

            for (const uint256& signer_program : info.multisig_signer_programs) {
                const Kvanta5P2QRDestination signer_dest{signer_program};

                p2qr_signers.push_back(EncodeDestination(signer_dest));
                p2qr_signer_programs.push_back(HexStr(signer_program));
            }

            ret.pushKV("p2qr_signer_addresses", std::move(p2qr_signers));
            ret.pushKV("p2qr_signer_programs", std::move(p2qr_signer_programs));
          
        } else {
            ret.pushKV("p2qr_type", "single");

            const Kvanta5P2QRDestination p2qr_dest{info.key_hash};
            std::vector<unsigned char> pubkey;
            if (pwallet->GetKvanta5P2QRPubKey(p2qr_dest, pubkey)) {
                ret.pushKV("p2qr_pubkey", HexStr(pubkey));
            }
        }
    };

    // Native / P2SH-carried KVANTA5_P2QR solvability and proof reporting.
    //
    // P2QR is not descriptor-solvable in the normal Bitcoin Core sense.
    // However, a native or P2SH-carried Kvanta5 P2QR output is wallet-solvable
    // if the wallet-native ML-DSA/Dilithium resolver can map the scriptPubKey
    // to owned P2QR key material.
    //
    // Important:
    // Do NOT gate this behind !solvable. P2SH-carried P2QR can already appear
    // descriptor/wallet-solvable as generic P2SH, but we still need to expose
    // the Kvanta5 redeemScript proof.
    const auto kv5_p2qr_info = pwallet->GetKvanta5P2QRSpendInfoForScript(scriptPubKey);
    if (kv5_p2qr_info.has_value()) {
        solvable = true;

        std::vector<std::vector<unsigned char>> spk_solutions;
        const TxoutType spk_type = Solver(scriptPubKey, spk_solutions);

        if (spk_type == TxoutType::KVANTA5_P2QR && !kv5_p2qr_info->wrapped_p2sh) {
            ret.pushKV("iskvanta5p2qr", true);
            ret.pushKV("native_scriptPubKey", HexStr(scriptPubKey));
            push_p2qr_details(*kv5_p2qr_info);
        } else if (spk_type == TxoutType::SCRIPTHASH && kv5_p2qr_info->wrapped_p2sh) {
            const CScript& redeem_script = kv5_p2qr_info->redeem_script;

            std::vector<std::vector<unsigned char>> redeem_solutions;
            const TxoutType redeem_type = Solver(redeem_script, redeem_solutions);

            bool hash_matches{false};
            if (spk_solutions.size() == 1) {
                const uint160 expected_hash{spk_solutions[0]};
                hash_matches = Hash160(redeem_script) == expected_hash;
            }

            if (redeem_type == TxoutType::KVANTA5_P2QR && hash_matches) {
                ret.pushKV("iskvanta5p2qr", true);
                ret.pushKV("mining_type", "p2sh_carried_kvanta5_p2qr");
                ret.pushKV("mining_redeemScript", HexStr(redeem_script));
                ret.pushKV("mining_scriptPubKey", HexStr(scriptPubKey));
                ret.pushKV("native_scriptPubKey", HexStr(redeem_script));
                push_p2qr_details(*kv5_p2qr_info);
            } else {
                ret.pushKV("iskvanta5p2qr", false);
                ret.pushKV("kvanta5_p2qr_error", "wallet spend-info exists but P2SH redeemScript is not a valid OP_RESERVED P2QR wrapper or hash does not match");
            }
        }
    }

    ret.pushKV("solvable", solvable);

    const auto& spk_mans = pwallet->GetScriptPubKeyMans(scriptPubKey);

    // In most cases there is only one matching ScriptPubKey manager and we can't resolve ambiguity in a better way.
    ScriptPubKeyMan* spk_man{nullptr};
    if (spk_mans.size()) spk_man = *spk_mans.begin();

    DescriptorScriptPubKeyMan* desc_spk_man = dynamic_cast<DescriptorScriptPubKeyMan*>(spk_man);
    if (desc_spk_man) {
        std::string desc_str;
        if (desc_spk_man->GetDescriptorString(desc_str, /*priv=*/false)) {
            ret.pushKV("parent_desc", desc_str);
        }
    }

    ret.pushKV("iswatchonly", bool(mine & ISMINE_WATCH_ONLY));

    UniValue detail = DescribeWalletAddress(*pwallet, dest);
    ret.pushKVs(std::move(detail));

    ret.pushKV("ischange", ScriptIsChange(*pwallet, scriptPubKey));

    if (spk_man) {
        if (const std::unique_ptr<CKeyMetadata> meta = spk_man->GetMetadata(dest)) {
            ret.pushKV("timestamp", meta->nCreateTime);
            if (meta->has_key_origin) {
                // In legacy wallets hdkeypath has always used an apostrophe for
                // hardened derivation. Perhaps some external tool depends on that.
                ret.pushKV("hdkeypath", WriteHDKeypath(meta->key_origin.path, /*apostrophe=*/!desc_spk_man));
                ret.pushKV("hdseedid", meta->hd_seed_id.GetHex());
                ret.pushKV("hdmasterfingerprint", HexStr(meta->key_origin.fingerprint));
            }
        }
    }

    // Return a `labels` array containing the label associated with the address,
    // equivalent to the `label` field above. Currently only one label can be
    // associated with an address, but we return an array so the API remains
    // stable if we allow multiple labels to be associated with an address in
    // the future.
    UniValue labels(UniValue::VARR);
    const auto* address_book_entry = pwallet->FindAddressBookEntry(dest);
    if (address_book_entry) {
        labels.push_back(address_book_entry->GetLabel());
    }
    ret.pushKV("labels", std::move(labels));

    return ret;
},
    };
}


RPCHelpMan getaddressesbylabel()
{
    return RPCHelpMan{"getaddressesbylabel",
                "\nReturns the list of addresses assigned the specified label.\n",
                {
                    {"label", RPCArg::Type::STR, RPCArg::Optional::NO, "The label."},
                },
                RPCResult{
                    RPCResult::Type::OBJ_DYN, "", "json object with addresses as keys",
                    {
                        {RPCResult::Type::OBJ, "address", "json object with information about address",
                        {
                            {RPCResult::Type::STR, "purpose", "Purpose of address (\"send\" for sending address, \"receive\" for receiving address)"},
                        }},
                    }
                },
                RPCExamples{
                    HelpExampleCli("getaddressesbylabel", "\"tabby\"")
            + HelpExampleRpc("getaddressesbylabel", "\"tabby\"")
                },
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
{
    const std::shared_ptr<const CWallet> pwallet = GetWalletForJSONRPCRequest(request);
    if (!pwallet) return UniValue::VNULL;

    LOCK(pwallet->cs_wallet);

    const std::string label{LabelFromValue(request.params[0])};

    // Find all addresses that have the given label
    UniValue ret(UniValue::VOBJ);
    std::set<std::string> addresses;
    pwallet->ForEachAddrBookEntry([&](const CTxDestination& _dest, const std::string& _label, bool _is_change, const std::optional<AddressPurpose>& _purpose) {
        if (_is_change) return;
        if (_label == label) {
            std::string address = EncodeDestination(_dest);
            // CWallet::m_address_book is not expected to contain duplicate
            // address strings, but build a separate set as a precaution just in
            // case it does.
            bool unique = addresses.emplace(address).second;
            CHECK_NONFATAL(unique);
            // UniValue::pushKV checks if the key exists in O(N)
            // and since duplicate addresses are unexpected (checked with
            // std::set in O(log(N))), UniValue::pushKVEnd is used instead,
            // which currently is O(1).
            UniValue value(UniValue::VOBJ);
            value.pushKV("purpose", _purpose ? PurposeToString(*_purpose) : "unknown");
            ret.pushKVEnd(address, std::move(value));
        }
    });

    if (ret.empty()) {
        throw JSONRPCError(RPC_WALLET_INVALID_LABEL_NAME, std::string("No addresses with label " + label));
    }

    return ret;
},
    };
}

RPCHelpMan listlabels()
{
    return RPCHelpMan{"listlabels",
                "\nReturns the list of all labels, or labels that are assigned to addresses with a specific purpose.\n",
                {
                    {"purpose", RPCArg::Type::STR, RPCArg::Optional::OMITTED, "Address purpose to list labels for ('send','receive'). An empty string is the same as not providing this argument."},
                },
                RPCResult{
                    RPCResult::Type::ARR, "", "",
                    {
                        {RPCResult::Type::STR, "label", "Label name"},
                    }
                },
                RPCExamples{
            "\nList all labels\n"
            + HelpExampleCli("listlabels", "") +
            "\nList labels that have receiving addresses\n"
            + HelpExampleCli("listlabels", "receive") +
            "\nList labels that have sending addresses\n"
            + HelpExampleCli("listlabels", "send") +
            "\nAs a JSON-RPC call\n"
            + HelpExampleRpc("listlabels", "receive")
                },
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
{
    const std::shared_ptr<const CWallet> pwallet = GetWalletForJSONRPCRequest(request);
    if (!pwallet) return UniValue::VNULL;

    LOCK(pwallet->cs_wallet);

    std::optional<AddressPurpose> purpose;
    if (!request.params[0].isNull()) {
        std::string purpose_str = request.params[0].get_str();
        if (!purpose_str.empty()) {
            purpose = PurposeFromString(purpose_str);
            if (!purpose) {
                throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid 'purpose' argument, must be a known purpose string, typically 'send', or 'receive'.");
            }
        }
    }

    // Add to a set to sort by label name, then insert into Univalue array
    std::set<std::string> label_set = pwallet->ListAddrBookLabels(purpose);

    UniValue ret(UniValue::VARR);
    for (const std::string& name : label_set) {
        ret.push_back(name);
    }

    return ret;
},
    };
}


#ifdef ENABLE_EXTERNAL_SIGNER
RPCHelpMan walletdisplayaddress()
{
    return RPCHelpMan{
        "walletdisplayaddress",
        "Display address on an external signer for verification.",
        {
            {"address", RPCArg::Type::STR, RPCArg::Optional::NO, "kvanta5 address to display"},
        },
        RPCResult{
            RPCResult::Type::OBJ,"","",
            {
                {RPCResult::Type::STR, "address", "The address as confirmed by the signer"},
            }
        },
        RPCExamples{""},
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
        {
            std::shared_ptr<CWallet> const wallet = GetWalletForJSONRPCRequest(request);
            if (!wallet) return UniValue::VNULL;
            CWallet* const pwallet = wallet.get();

            LOCK(pwallet->cs_wallet);

            CTxDestination dest = DecodeDestination(request.params[0].get_str());

            // Make sure the destination is valid
            if (!IsValidDestination(dest)) {
                throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid address");
            }

            util::Result<void> res = pwallet->DisplayAddress(dest);
            if (!res) throw JSONRPCError(RPC_MISC_ERROR, util::ErrorString(res).original);

            UniValue result(UniValue::VOBJ);
            result.pushKV("address", request.params[0].get_str());
            return result;
        }
    };
}
#endif // ENABLE_EXTERNAL_SIGNER
} // namespace wallet
