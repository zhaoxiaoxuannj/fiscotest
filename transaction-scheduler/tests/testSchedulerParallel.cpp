#include "bcos-framework/ledger/LedgerConfig.h"
#include "bcos-framework/storage2/MemoryStorage.h"
#include "bcos-framework/storage2/Storage.h"
#include "bcos-framework/transaction-scheduler/TransactionScheduler.h"
#include "bcos-tars-protocol/protocol/BlockHeaderImpl.h"
#include "bcos-tars-protocol/protocol/TransactionReceiptFactoryImpl.h"
#include "bcos-transaction-scheduler/MultiLayerStorage.h"
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-framework/transaction-executor/TransactionExecutor.h>
#include <bcos-tars-protocol/protocol/TransactionImpl.h>
#include <bcos-task/Wait.h>
#include <bcos-transaction-scheduler/SchedulerParallelImpl.h>
#include <boost/test/unit_test.hpp>
#include <mutex>

using namespace bcos;
using namespace bcos::storage2;
using namespace bcos::transaction_executor;
using namespace bcos::transaction_scheduler;
using namespace std::string_view_literals;

struct MockExecutor
{
    friend task::Task<protocol::TransactionReceipt::Ptr> tag_invoke(
        bcos::transaction_executor::tag_t<
            bcos::transaction_executor::executeTransaction> /*unused*/,
        MockExecutor& executor, auto& storage, protocol::BlockHeader const& blockHeader,
        protocol::Transaction const& transaction, int contextID, ledger::LedgerConfig const&,
        auto&& waitOperator)
    {
        co_return std::shared_ptr<bcos::protocol::TransactionReceipt>();
    }
};

class TestSchedulerParallelFixture
{
public:
    using MutableStorage = memory_storage::MemoryStorage<StateKey, StateValue,
        memory_storage::Attribute(memory_storage::ORDERED | memory_storage::LOGICAL_DELETION)>;
    using BackendStorage = memory_storage::MemoryStorage<StateKey, StateValue,
        memory_storage::Attribute(memory_storage::ORDERED | memory_storage::CONCURRENT),
        std::hash<StateKey>>;

    TestSchedulerParallelFixture()
      : cryptoSuite(std::make_shared<bcos::crypto::CryptoSuite>(
            std::make_shared<bcos::crypto::Keccak256>(), nullptr, nullptr)),
        receiptFactory(cryptoSuite),
        multiLayerStorage(backendStorage)
    {}

    BackendStorage backendStorage;
    bcos::crypto::CryptoSuite::Ptr cryptoSuite;
    bcostars::protocol::TransactionReceiptFactoryImpl receiptFactory;
    MultiLayerStorage<MutableStorage, void, BackendStorage> multiLayerStorage;
    crypto::Hash::Ptr hashImpl = std::make_shared<bcos::crypto::Keccak256>();
};

BOOST_FIXTURE_TEST_SUITE(TestSchedulerParallel, TestSchedulerParallelFixture)

BOOST_AUTO_TEST_CASE(simple)
{
    task::syncWait([&, this]() -> task::Task<void> {
        MockExecutor executor;
        SchedulerParallelImpl scheduler;

        bcostars::protocol::BlockHeaderImpl blockHeader(
            [inner = bcostars::BlockHeader()]() mutable { return std::addressof(inner); });
        auto transactions =
            RANGES::iota_view<int, int>(0, 100) | RANGES::views::transform([](int index) {
                return std::make_unique<bcostars::protocol::TransactionImpl>(
                    [inner = bcostars::Transaction()]() mutable { return std::addressof(inner); });
            }) |
            RANGES::to<std::vector<std::unique_ptr<bcostars::protocol::TransactionImpl>>>();

        multiLayerStorage.newMutable();
        auto view = multiLayerStorage.fork(true);
        ledger::LedgerConfig ledgerConfig;
        auto receipts = co_await bcos::transaction_scheduler::executeBlock(scheduler, view,
            executor, blockHeader,
            transactions | RANGES::views::transform([](auto& ptr) -> auto& { return *ptr; }),
            ledgerConfig);
        BOOST_CHECK_EQUAL(transactions.size(), receipts.size());

        co_return;
    }());
    // Wait for tbb
}

constexpr static size_t MOCK_USER_COUNT = 1000;
struct MockConflictExecutor
{
    friend task::Task<protocol::TransactionReceipt::Ptr> tag_invoke(
        bcos::transaction_executor::tag_t<
            bcos::transaction_executor::executeTransaction> /*unused*/,
        MockConflictExecutor& executor, auto& storage, protocol::BlockHeader const& blockHeader,
        protocol::Transaction const& transaction, int contextID, ledger::LedgerConfig const&,
        auto&& waitOperator)
    {
        auto input = transaction.input();
        auto inputNum =
            boost::lexical_cast<int>(std::string_view((const char*)input.data(), input.size()));

        auto fromAddress = std::to_string(inputNum % MOCK_USER_COUNT);
        auto toAddress = std::to_string((inputNum + (MOCK_USER_COUNT / 2)) % MOCK_USER_COUNT);

        // Read fromKey and -1
        StateKey fromKey{"t_test"sv, fromAddress};
        auto fromEntry = co_await storage2::readOne(storage, fromKey);
        fromEntry->set(
            boost::lexical_cast<std::string>(boost::lexical_cast<int>(fromEntry->get()) - 1));
        co_await storage2::writeOne(storage, fromKey, *fromEntry);

        // Read toKey and +1
        StateKey toKey{"t_test"sv, toAddress};
        auto toEntry = co_await storage2::readOne(storage, toKey);
        toEntry->set(
            boost::lexical_cast<std::string>(boost::lexical_cast<int>(toEntry->get()) + 1));
        co_await storage2::writeOne(storage, toKey, *toEntry);

        co_return std::shared_ptr<bcos::protocol::TransactionReceipt>();
    }
};

BOOST_AUTO_TEST_CASE(conflict)
{
    task::syncWait([&, this]() -> task::Task<void> {
        MockConflictExecutor executor;
        SchedulerParallelImpl scheduler;
        scheduler.setChunkSize(1);
        scheduler.setMaxToken(std::thread::hardware_concurrency());

        multiLayerStorage.newMutable();
        constexpr static int INITIAL_VALUE = 100000;
        for (auto i : RANGES::views::iota(0LU, MOCK_USER_COUNT))
        {
            StateKey key{"t_test"sv, boost::lexical_cast<std::string>(i)};
            storage::Entry entry;
            entry.set(boost::lexical_cast<std::string>(INITIAL_VALUE));
            co_await storage2::writeOne(multiLayerStorage.mutableStorage(), key, std::move(entry));
        }

        bcostars::protocol::BlockHeaderImpl blockHeader(
            [inner = bcostars::BlockHeader()]() mutable { return std::addressof(inner); });
        constexpr static auto TRANSACTION_COUNT = 1000;
        auto transactions =
            RANGES::views::iota(0, TRANSACTION_COUNT) | RANGES::views::transform([](int index) {
                auto transaction = std::make_unique<bcostars::protocol::TransactionImpl>(
                    [inner = bcostars::Transaction()]() mutable { return std::addressof(inner); });
                auto num = boost::lexical_cast<std::string>(index);
                transaction->mutableInner().data.input.assign(num.begin(), num.end());

                return transaction;
            }) |
            RANGES::to<std::vector<std::unique_ptr<bcostars::protocol::TransactionImpl>>>();

        auto transactionRefs =
            transactions | RANGES::views::transform([](auto& ptr) -> auto& { return *ptr; });
        auto view = multiLayerStorage.fork(true);
        ledger::LedgerConfig ledgerConfig;
        auto receipts = co_await bcos::transaction_scheduler::executeBlock(
            scheduler, view, executor, blockHeader, transactionRefs, ledgerConfig);

        for (auto i : RANGES::views::iota(0LU, MOCK_USER_COUNT))
        {
            StateKey key{"t_test"sv, boost::lexical_cast<std::string>(i)};
            auto entry = co_await storage2::readOne(multiLayerStorage.mutableStorage(), key);
            BOOST_CHECK_EQUAL(boost::lexical_cast<int>(entry->get()), INITIAL_VALUE);
        }

        co_return;
    }());
}

BOOST_AUTO_TEST_SUITE_END()