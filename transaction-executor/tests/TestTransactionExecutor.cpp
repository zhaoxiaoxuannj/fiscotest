#include "../bcos-transaction-executor/TransactionExecutorImpl.h"
#include "TestBytecode.h"
#include "TestMemoryStorage.h"
#include "bcos-codec/bcos-codec/abi/ContractABICodec.h"
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-tars-protocol/protocol/BlockHeaderImpl.h>
#include <bcos-tars-protocol/protocol/TransactionFactoryImpl.h>
#include <bcos-tars-protocol/protocol/TransactionReceiptFactoryImpl.h>
#include <boost/test/unit_test.hpp>

using namespace bcos;
using namespace bcos::storage2;
using namespace bcos::transaction_executor;

class TestTransactionExecutorImplFixture
{
public:
    TestTransactionExecutorImplFixture()
    {
        bcos::executor::GlobalHashImpl::g_hashImpl = std::make_shared<bcos::crypto::Keccak256>();
    }
    ledger::LedgerConfig ledgerConfig;
};

BOOST_FIXTURE_TEST_SUITE(TransactionExecutorImpl, TestTransactionExecutorImplFixture)

BOOST_AUTO_TEST_CASE(execute)
{
    task::syncWait([this]() mutable -> task::Task<void> {
        MutableStorage storage;

        auto cryptoSuite = std::make_shared<bcos::crypto::CryptoSuite>(
            bcos::executor::GlobalHashImpl::g_hashImpl, nullptr, nullptr);
        bcostars::protocol::TransactionReceiptFactoryImpl receiptFactory(cryptoSuite);

        bcos::transaction_executor::TransactionExecutorImpl executor(
            receiptFactory, bcos::executor::GlobalHashImpl::g_hashImpl);
        bcostars::protocol::BlockHeaderImpl blockHeader(
            [inner = bcostars::BlockHeader()]() mutable { return std::addressof(inner); });
        blockHeader.setVersion((uint32_t)bcos::protocol::BlockVersion::V3_1_VERSION);
        blockHeader.calculateHash(*bcos::executor::GlobalHashImpl::g_hashImpl);

        bcostars::protocol::TransactionFactoryImpl transactionFactory(cryptoSuite);

        bcos::bytes helloworldBytecodeBinary;
        boost::algorithm::unhex(helloworldBytecode, std::back_inserter(helloworldBytecodeBinary));
        // First deploy
        auto transaction =
            transactionFactory.createTransaction(0, "", helloworldBytecodeBinary, {}, 0, "", "", 0);
        auto receipt = co_await bcos::transaction_executor::executeTransaction(
            executor, storage, blockHeader, *transaction, 0, ledgerConfig, task::syncWait);
        BOOST_CHECK_EQUAL(receipt->status(), 0);
        BOOST_CHECK_EQUAL(receipt->contractAddress(), "e0e794ca86d198042b64285c5ce667aee747509b");

        // Set the value
        bcos::codec::abi::ContractABICodec abiCodec(bcos::executor::GlobalHashImpl::g_hashImpl);
        auto input = abiCodec.abiIn("setInt(int256)", bcos::s256(10099));
        auto transaction2 = transactionFactory.createTransaction(
            0, std::string(receipt->contractAddress()), input, {}, 0, "", "", 0);
        auto receipt2 = co_await bcos::transaction_executor::executeTransaction(
            executor, storage, blockHeader, *transaction2, 1, ledgerConfig, task::syncWait);
        BOOST_CHECK_EQUAL(receipt2->status(), 0);

        // Get the value
        auto input2 = abiCodec.abiIn("getInt()");
        auto transaction3 = transactionFactory.createTransaction(
            0, std::string(receipt->contractAddress()), input2, {}, 0, "", "", 0);
        auto receipt3 = co_await bcos::transaction_executor::executeTransaction(
            executor, storage, blockHeader, *transaction3, 2, ledgerConfig, task::syncWait);
        BOOST_CHECK_EQUAL(receipt3->status(), 0);
        bcos::s256 getIntResult = -1;
        abiCodec.abiOut(receipt3->output(), getIntResult);
        BOOST_CHECK_EQUAL(getIntResult, 10099);
    }());
}

BOOST_AUTO_TEST_SUITE_END()