#include "../bcos-transaction-executor/precompiled/PrecompiledManager.h"
#include "../bcos-transaction-executor/vm/HostContext.h"
#include "../bcos-transaction-executor/vm/VMInstance.h"
#include "TestBytecode.h"
#include "TestMemoryStorage.h"
#include "bcos-codec/bcos-codec/abi/ContractABICodec.h"
#include "bcos-crypto/interfaces/crypto/CryptoSuite.h"
#include "bcos-crypto/interfaces/crypto/Hash.h"
#include "bcos-executor/src/Common.h"
#include "bcos-framework/ledger/GenesisConfig.h"
#include "bcos-framework/protocol/Protocol.h"
#include "bcos-ledger/src/libledger/Ledger.h"
#include "bcos-table/src/LegacyStorageWrapper.h"
#include "bcos-tars-protocol/protocol/BlockFactoryImpl.h"
#include "bcos-tars-protocol/protocol/BlockHeaderFactoryImpl.h"
#include "bcos-tars-protocol/protocol/TransactionFactoryImpl.h"
#include "bcos-tars-protocol/protocol/TransactionReceiptFactoryImpl.h"
#include "bcos-task/Wait.h"
#include "bcos-tool/VersionConverter.h"
#include "bcos-transaction-executor/RollbackableStorage.h"
#include "bcos-transaction-executor/vm/VMFactory.h"
#include "bcos-utilities/FixedBytes.h"
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-framework/storage2/MemoryStorage.h>
#include <bcos-tars-protocol/protocol/BlockHeaderImpl.h>
#include <evmc/evmc.h>
#include <boost/algorithm/hex.hpp>
#include <boost/test/unit_test.hpp>
#include <atomic>
#include <iterator>
#include <memory>

using namespace bcos::task;
using namespace bcos::storage2;
using namespace bcos::transaction_executor;

class TestHostContextFixture
{
public:
    bcos::crypto::Hash::Ptr hashImpl = std::make_shared<bcos::crypto::Keccak256>();
    MutableStorage storage;
    Rollbackable<decltype(storage)> rollbackableStorage;
    evmc_address helloworldAddress;
    int64_t seq = 0;
    std::optional<PrecompiledManager> precompiledManager;
    bcos::ledger::LedgerConfig ledgerConfig;

    TestHostContextFixture() : rollbackableStorage(storage)
    {
        bcos::executor::GlobalHashImpl::g_hashImpl = std::make_shared<bcos::crypto::Keccak256>();
        precompiledManager.emplace(hashImpl);

        // deploy the hello world contract
        bcostars::protocol::BlockHeaderImpl blockHeader(
            [inner = bcostars::BlockHeader()]() mutable { return std::addressof(inner); });
        blockHeader.setVersion(static_cast<uint32_t>(bcos::protocol::BlockVersion::V3_3_VERSION));
        blockHeader.calculateHash(*hashImpl);

        std::string helloworldBytecodeBinary;
        boost::algorithm::unhex(helloworldBytecode, std::back_inserter(helloworldBytecodeBinary));

        evmc_message message = {.kind = EVMC_CREATE,
            .flags = 0,
            .depth = 0,
            .gas = 300 * 10000,
            .recipient = {},
            .destination_ptr = nullptr,
            .destination_len = 0,
            .sender = {},
            .sender_ptr = nullptr,
            .sender_len = 0,
            .input_data = (const uint8_t*)helloworldBytecodeBinary.data(),
            .input_size = helloworldBytecodeBinary.size(),
            .value = {},
            .create2_salt = {},
            .code_address = {}};
        evmc_address origin = {};

        HostContext<decltype(rollbackableStorage)> hostContext(rollbackableStorage, blockHeader,
            message, origin, "", 0, seq, *precompiledManager, ledgerConfig, *hashImpl,
            bcos::task::syncWait);
        syncWait(hostContext.prepare());
        auto result = syncWait(hostContext.execute());

        BOOST_REQUIRE_EQUAL(result.status_code, 0);

        helloworldAddress = result.create_address;
    }

    template <class... Arg>
    Task<EVMCResult> call(std::string_view abi, evmc_address sender, Arg const&... args)
    {
        bcos::codec::abi::ContractABICodec abiCodec(bcos::executor::GlobalHashImpl::g_hashImpl);
        auto input = abiCodec.abiIn(std::string(abi), args...);

        bcostars::protocol::BlockHeaderImpl blockHeader(
            [inner = bcostars::BlockHeader()]() mutable { return std::addressof(inner); });
        blockHeader.setVersion(static_cast<uint32_t>(bcos::protocol::BlockVersion::V3_3_VERSION));

        static std::atomic_int64_t number = 0;
        blockHeader.setNumber(number++);
        blockHeader.calculateHash(*hashImpl);

        evmc_message message = {.kind = EVMC_CALL,
            .flags = 0,
            .depth = 0,
            .gas = 1000000,
            .recipient = helloworldAddress,
            .destination_ptr = nullptr,
            .destination_len = 0,
            .sender = sender,
            .sender_ptr = nullptr,
            .sender_len = 0,
            .input_data = input.data(),
            .input_size = input.size(),
            .value = {},
            .create2_salt = {},
            .code_address = helloworldAddress};
        evmc_address origin = {};

        HostContext<decltype(rollbackableStorage)> hostContext(rollbackableStorage, blockHeader,
            message, origin, "", 0, seq, *precompiledManager, ledgerConfig, *hashImpl,
            bcos::task::syncWait);
        co_await hostContext.prepare();
        auto result = co_await hostContext.execute();

        co_return result;
    }
};

bcos::crypto::Hash::Ptr bcos::executor::GlobalHashImpl::g_hashImpl;

BOOST_FIXTURE_TEST_SUITE(TestHostContext, TestHostContextFixture)

BOOST_AUTO_TEST_CASE(bits)
{
    auto evmAddress = bcos::unhexAddress("0x0000000000000000000000000000000000000100");
    bcos::u160 address1;
    boost::multiprecision::import_bits(
        address1, evmAddress.bytes, evmAddress.bytes + sizeof(evmAddress.bytes));
    auto address2 =
        fromBigEndian<bcos::u160>(bcos::bytesConstRef(evmAddress.bytes, sizeof(evmAddress.bytes)));

    BOOST_CHECK_EQUAL(address1, address2);
}

BOOST_AUTO_TEST_CASE(simpleCall)
{
    syncWait([this]() -> Task<void> {
        auto result = co_await call(std::string("getInt()"), {});

        BOOST_CHECK_EQUAL(result.status_code, 0);
        bcos::s256 getIntResult = -1;
        bcos::codec::abi::ContractABICodec abiCodec(bcos::executor::GlobalHashImpl::g_hashImpl);
        abiCodec.abiOut(bcos::bytesConstRef(result.output_data, result.output_size), getIntResult);
        BOOST_CHECK_EQUAL(getIntResult, 0);

        co_return;
    }());
}

BOOST_AUTO_TEST_CASE(executeAndCall)
{
    syncWait([this]() -> Task<void> {
        auto result1 = co_await call("setInt(int256)", {}, bcos::s256(10000));
        auto result2 = co_await call("getInt()", {});
        auto result3 =
            co_await call("setString(string)", {}, std::string("Hello world, fisco-bcos!"));
        auto result4 = co_await call("getString()", {});

        BOOST_CHECK_EQUAL(result1.status_code, 0);
        BOOST_CHECK_EQUAL(result2.status_code, 0);
        BOOST_CHECK_EQUAL(result3.status_code, 0);
        BOOST_CHECK_EQUAL(result4.status_code, 0);
        bcos::s256 getIntResult = -1;
        bcos::codec::abi::ContractABICodec abiCodec(bcos::executor::GlobalHashImpl::g_hashImpl);
        abiCodec.abiOut(
            bcos::bytesConstRef(result2.output_data, result2.output_size), getIntResult);
        BOOST_CHECK_EQUAL(getIntResult, 10000);

        std::string out;
        abiCodec.abiOut(bcos::bytesConstRef(result4.output_data, result4.output_size), out);
        BOOST_CHECK_EQUAL(out, "Hello world, fisco-bcos!");

        co_return;
    }());
}

BOOST_AUTO_TEST_CASE(contractDeploy)
{
    syncWait([this]() -> Task<void> {
        auto result = co_await call("deployAndCall(int256)", {}, bcos::s256(999));

        BOOST_CHECK_EQUAL(result.status_code, 0);
        bcos::s256 getIntResult = -1;
        bcos::codec::abi::ContractABICodec abiCodec(bcos::executor::GlobalHashImpl::g_hashImpl);
        abiCodec.abiOut(bcos::bytesConstRef(result.output_data, result.output_size), getIntResult);
        BOOST_CHECK_EQUAL(getIntResult, 999);

        co_return;
    }());
}

BOOST_AUTO_TEST_CASE(createTwice)
{
    syncWait([this]() -> Task<void> {
        auto result = co_await call("createTwice()", {});
        BOOST_CHECK_EQUAL(result.status_code, 0);

        co_return;
    }());
}

BOOST_AUTO_TEST_CASE(failure)
{
    syncWait([this]() -> Task<void> {
        bcos::codec::abi::ContractABICodec abiCodec(bcos::executor::GlobalHashImpl::g_hashImpl);

        auto result1 = co_await call("returnRequire()", {});
        BOOST_CHECK_EQUAL(result1.status_code, 2);

        auto result2 = co_await call("getInt()", {});
        BOOST_CHECK_EQUAL(result2.status_code, 0);
        bcos::s256 getIntResult = -1;
        abiCodec.abiOut(
            bcos::bytesConstRef(result2.output_data, result2.output_size), getIntResult);
        BOOST_CHECK_EQUAL(getIntResult, 0);

        auto result3 = co_await call("returnRevert()", {});
        BOOST_CHECK_EQUAL(result3.status_code, 2);

        auto result4 = co_await call("getInt()", {});
        BOOST_CHECK_EQUAL(result4.status_code, 0);
        abiCodec.abiOut(
            bcos::bytesConstRef(result4.output_data, result4.output_size), getIntResult);
        BOOST_CHECK_EQUAL(getIntResult, 0);

        co_return;
    }());
}

BOOST_AUTO_TEST_CASE(delegateCall)
{
    syncWait([this]() -> Task<void> {
        bcos::codec::abi::ContractABICodec abiCodec(bcos::executor::GlobalHashImpl::g_hashImpl);

        evmc_address sender = bcos::unhexAddress("0x0000000000000000000000000000000000000050");
        auto result1 = co_await call("delegateCall()", sender);
        BOOST_CHECK_EQUAL(result1.status_code, 0);

        auto result2 = co_await call("getInt()", sender);
        bcos::s256 getIntResult = -1;
        abiCodec.abiOut(
            bcos::bytesConstRef(result2.output_data, result2.output_size), getIntResult);
        BOOST_CHECK_EQUAL(getIntResult, 19876);

        auto result3 = co_await call("getString()", sender);
        std::string strResult;
        abiCodec.abiOut(bcos::bytesConstRef(result3.output_data, result3.output_size), strResult);
        BOOST_CHECK_EQUAL(strResult, "hi!");
    }());
}

BOOST_AUTO_TEST_CASE(log)
{
    // syncWait([this]() -> Task<void> {
    // auto result1 = co_await call("setInt(int256)", bcos::s256(10000));
    // auto result2 = co_await call("setString(string)", std::string("Hello world,
    // fisco-bcos!")); auto result3 = co_await call("logOut()");

    // BOOST_CHECK_EQUAL(result1.status_code, 0);
    // BOOST_CHECK_EQUAL(result2.status_code, 0);
    // BOOST_CHECK_EQUAL(result3.status_code, 0);

    // bcos::s256 getIntResult = -1;
    // std::string out;
    // bcos::codec::abi::ContractABICodec abiCodec(
    //     bcos::transaction_executor::GlobalHashImpl::g_hashImpl);

    // abiCodec.abiOut(bcos::bytesConstRef(.output_data, result4.output_size), out);
    // BOOST_CHECK_EQUAL(out, "Hello world, fisco-bcos!");

    // releaseResult(result1);
    // releaseResult(result2);

    //     co_return;
    // }());
}

BOOST_AUTO_TEST_CASE(precompiled)
{
    // Use ledger to init storage
    auto ledgerConfig = bcos::ledger::LedgerConfig{};
    auto storageWrapper =
        std::make_shared<bcos::storage::LegacyStorageWrapper<std::decay_t<decltype(storage)>>>(
            storage);
    auto cryptoSuite = std::make_shared<bcos::crypto::CryptoSuite>(
        std::make_shared<bcos::crypto::Keccak256>(), nullptr, nullptr);
    bcos::ledger::Ledger ledger(
        std::make_shared<bcostars::protocol::BlockFactoryImpl>(cryptoSuite,
            std::make_shared<bcostars::protocol::BlockHeaderFactoryImpl>(cryptoSuite),
            std::make_shared<bcostars::protocol::TransactionFactoryImpl>(cryptoSuite),
            std::make_shared<bcostars::protocol::TransactionReceiptFactoryImpl>(cryptoSuite)),
        storageWrapper);
    bcos::ledger::GenesisConfig genesis;
    genesis.m_txGasLimit = 100000;
    genesis.m_compatibilityVersion = bcos::tool::toVersionNumber("3.6.0");
    ledger.buildGenesisBlock(genesis, ledgerConfig);

    bcostars::protocol::BlockHeaderImpl blockHeader(
        [inner = bcostars::BlockHeader()]() mutable { return std::addressof(inner); });
    blockHeader.mutableInner().data.version = (int)bcos::protocol::BlockVersion::V3_5_VERSION;
    blockHeader.calculateHash(*bcos::executor::GlobalHashImpl::g_hashImpl);

    bcos::codec::abi::ContractABICodec abiCodec(bcos::executor::GlobalHashImpl::g_hashImpl);
    {
        auto input = abiCodec.abiIn("initBfs()");
        auto address = bcos::Address(0x100e);
        evmc_address callAddress{};
        std::uninitialized_copy(address.begin(), address.end(), callAddress.bytes);
        evmc_message message = {.kind = EVMC_CALL,
            .flags = 0,
            .depth = 0,
            .gas = 1000000,
            .recipient = callAddress,
            .destination_ptr = nullptr,
            .destination_len = 0,
            .sender = {},
            .sender_ptr = nullptr,
            .sender_len = 0,
            .input_data = input.data(),
            .input_size = input.size(),
            .value = {},
            .create2_salt = {},
            .code_address = callAddress};
        evmc_address origin = {};

        HostContext<decltype(rollbackableStorage)> hostContext(rollbackableStorage, blockHeader,
            message, origin, "", 0, seq, *precompiledManager, ledgerConfig, *hashImpl,
            bcos::task::syncWait);
        syncWait(hostContext.prepare());
        auto result = syncWait(hostContext.execute());
    }

    std::optional<EVMCResult> result;
    {
        auto input = abiCodec.abiIn(std::string("makeShard(string)"), std::string("shared1"));

        auto address = bcos::Address(0x1010);
        evmc_address callAddress{};
        std::uninitialized_copy(address.begin(), address.end(), callAddress.bytes);
        evmc_message message = {.kind = EVMC_CALL,
            .flags = 0,
            .depth = 0,
            .gas = 1000000,
            .recipient = callAddress,
            .destination_ptr = nullptr,
            .destination_len = 0,
            .sender = {},
            .sender_ptr = nullptr,
            .sender_len = 0,
            .input_data = input.data(),
            .input_size = input.size(),
            .value = {},
            .create2_salt = {},
            .code_address = callAddress};
        evmc_address origin = {};

        HostContext<decltype(rollbackableStorage)> hostContext(rollbackableStorage, blockHeader,
            message, origin, "", 0, seq, *precompiledManager, ledgerConfig, *hashImpl,
            bcos::task::syncWait);
        syncWait(hostContext.prepare());
        result.emplace(syncWait(hostContext.execute()));
    }

    BOOST_CHECK_EQUAL(result->status_code, 0);
    bcos::s256 getIntResult = -1;
    abiCodec.abiOut(bcos::bytesConstRef(result->output_data, result->output_size), getIntResult);
    BOOST_CHECK_EQUAL(getIntResult, 0);
}

BOOST_AUTO_TEST_SUITE_END()