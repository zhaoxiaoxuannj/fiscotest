#include "BaselineSchedulerInitializer.h"
#include "bcos-framework/storage2/MemoryStorage.h"
#include "bcos-framework/transaction-executor/TransactionExecutor.h"
#include "bcos-storage/RocksDBStorage2.h"
#include "bcos-storage/StateKVResolver.h"
#include "bcos-transaction-executor/TransactionExecutorImpl.h"
#include "bcos-transaction-executor/precompiled/PrecompiledManager.h"
#include "bcos-transaction-scheduler/BaselineScheduler.h"
#include "bcos-transaction-scheduler/SchedulerParallelImpl.h"
#include "bcos-transaction-scheduler/SchedulerSerialImpl.h"
#include "libinitializer/Common.h"

std::tuple<std::function<std::shared_ptr<bcos::scheduler::SchedulerInterface>()>,
    std::function<void(std::function<void(bcos::protocol::BlockNumber)>)>>
bcos::transaction_scheduler::BaselineSchedulerInitializer::build(::rocksdb::DB& rocksDB,
    std::shared_ptr<protocol::BlockFactory> blockFactory,
    std::shared_ptr<txpool::TxPoolInterface> txpool,
    std::shared_ptr<protocol::TransactionSubmitResultFactory> transactionSubmitResultFactory,
    std::shared_ptr<ledger::LedgerInterface> ledger,
    tool::NodeConfig::BaselineSchedulerConfig const& config)
{
    struct Data
    {
        using MutableStorage =
            storage2::memory_storage::MemoryStorage<transaction_executor::StateKey,
                transaction_executor::StateValue,
                storage2::memory_storage::Attribute(storage2::memory_storage::ORDERED |
                                                    storage2::memory_storage::LOGICAL_DELETION)>;
        using CacheStorage = storage2::memory_storage::MemoryStorage<transaction_executor::StateKey,
            transaction_executor::StateValue,
            storage2::memory_storage::Attribute(storage2::memory_storage::ORDERED |
                                                storage2::memory_storage::CONCURRENT |
                                                storage2::memory_storage::MRU),
            std::hash<bcos::transaction_executor::StateKey>>;

        CacheStorage m_cacheStorage;
        storage2::rocksdb::RocksDBStorage2<transaction_executor::StateKey,
            transaction_executor::StateValue, storage2::rocksdb::StateKeyResolver,
            storage2::rocksdb::StateValueResolver>
            m_rocksDBStorage;

        MultiLayerStorage<MutableStorage, CacheStorage, decltype(m_rocksDBStorage)>
            m_multiLayerStorage;
        transaction_executor::PrecompiledManager m_precompiledManager;
        transaction_executor::TransactionExecutorImpl m_transactionExecutor;

        Data(::rocksdb::DB& rocksDB, protocol::BlockFactory& blockFactory)
          : m_rocksDBStorage(rocksDB, storage2::rocksdb::StateKeyResolver{},
                storage2::rocksdb::StateValueResolver{}),
            m_multiLayerStorage(m_rocksDBStorage, m_cacheStorage),
            m_precompiledManager(blockFactory.cryptoSuite()->hashImpl()),
            m_transactionExecutor(
                *blockFactory.receiptFactory(), blockFactory.cryptoSuite()->hashImpl())
        {}
    };
    auto data = std::make_shared<Data>(rocksDB, *blockFactory);

    auto buildBaselineHolder = [&](auto scheduler) {
        auto baselineScheduler =
            std::make_shared<BaselineScheduler<decltype(data->m_multiLayerStorage),
                decltype(data->m_transactionExecutor), decltype(*scheduler),
                ledger::LedgerInterface>>(data->m_multiLayerStorage, *scheduler,
                data->m_transactionExecutor, *blockFactory->blockHeaderFactory(), *ledger, *txpool,
                *transactionSubmitResultFactory, *blockFactory->cryptoSuite()->hashImpl());
        baselineScheduler->registerTransactionNotifier(
            [txpool](bcos::protocol::BlockNumber blockNumber,
                bcos::protocol::TransactionSubmitResultsPtr result,
                std::function<void(bcos::Error::Ptr)> callback) mutable {
                txpool->asyncNotifyBlockResult(blockNumber, std::move(result), std::move(callback));
            });

        return std::make_tuple(
            [scheduler = std::move(scheduler), baselineScheduler, data = std::move(data),
                blockFactory = std::move(blockFactory), txpool = std::move(txpool),
                transactionSubmitResultFactory = std::move(transactionSubmitResultFactory),
                ledger = std::move(ledger)]() -> std::shared_ptr<scheduler::SchedulerInterface> {
                return baselineScheduler;
            },
            [baselineScheduler](std::function<void(protocol::BlockNumber)> notifier) {
                baselineScheduler->registerBlockNumberNotifier(std::move(notifier));
            });
    };

    if (config.parallel)
    {
        auto scheduler = std::make_shared<SchedulerParallelImpl>();
        scheduler->setChunkSize(config.chunkSize);
        scheduler->setMaxToken(config.maxThread);

        return buildBaselineHolder(std::move(scheduler));
    }

    auto scheduler = std::make_shared<SchedulerSerialImpl>();

    INITIALIZER_LOG(INFO) << "Initialize baseline scheduler, parallel: " << config.parallel
                          << ", chunkSize: " << config.chunkSize
                          << ", maxThread: " << config.maxThread;

    return buildBaselineHolder(std::move(scheduler));
}
