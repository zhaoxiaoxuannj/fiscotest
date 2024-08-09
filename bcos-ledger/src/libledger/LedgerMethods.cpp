#include "LedgerMethods.h"
#include "bcos-ledger/src/libledger/Ledger.h"
#include "bcos-tool/VersionConverter.h"
#include <boost/exception/diagnostic_information.hpp>
#include <exception>

bcos::task::Task<void> bcos::ledger::prewriteBlockToStorage(LedgerInterface& ledger,
    bcos::protocol::ConstTransactionsPtr transactions, bcos::protocol::Block::ConstPtr block,
    bool withTransactionsAndReceipts, storage::StorageInterface::Ptr storage)
{
    struct Awaitable
    {
        LedgerInterface& m_ledger;
        decltype(transactions) m_transactions;
        decltype(block) m_block;
        bool m_withTransactionsAndReceipts{};
        decltype(storage) m_storage;

        Error::Ptr m_error;

        constexpr static bool await_ready() noexcept { return false; }
        void await_suspend(CO_STD::coroutine_handle<> handle)
        {
            m_ledger.asyncPrewriteBlock(
                m_storage, std::move(m_transactions), std::move(m_block),
                [this, handle](std::string, Error::Ptr error) {
                    if (error)
                    {
                        m_error = std::move(error);
                    }
                    handle.resume();
                },
                m_withTransactionsAndReceipts);
        }
        void await_resume()
        {
            if (m_error)
            {
                BOOST_THROW_EXCEPTION(*m_error);
            }
        }
    };

    Awaitable awaitable{.m_ledger = ledger,
        .m_transactions = std::move(transactions),
        .m_block = std::move(block),
        .m_withTransactionsAndReceipts = withTransactionsAndReceipts,
        .m_storage = std::move(storage),
        .m_error = {}};
    co_await awaitable;
}

bcos::task::Task<void> bcos::ledger::tag_invoke(
    ledger::tag_t<storeTransactionsAndReceipts> /*unused*/, LedgerInterface& ledger,
    bcos::protocol::ConstTransactionsPtr blockTxs, bcos::protocol::Block::ConstPtr block)
{
    ledger.storeTransactionsAndReceipts(std::move(blockTxs), std::move(block));
    co_return;
}

bcos::task::Task<bcos::protocol::Block::Ptr> bcos::ledger::tag_invoke(
    ledger::tag_t<getBlockData> /*unused*/, LedgerInterface& ledger,
    protocol::BlockNumber blockNumber, int32_t blockFlag)
{
    struct Awaitable
    {
        LedgerInterface& m_ledger;
        protocol::BlockNumber m_blockNumber;
        int32_t m_blockFlag;

        std::variant<Error::Ptr, bcos::protocol::Block::Ptr> m_result;

        constexpr static bool await_ready() noexcept { return false; }
        void await_suspend(CO_STD::coroutine_handle<> handle)
        {
            m_ledger.asyncGetBlockDataByNumber(m_blockNumber, m_blockFlag,
                [this, handle](Error::Ptr error, bcos::protocol::Block::Ptr block) {
                    if (error)
                    {
                        m_result.emplace<Error::Ptr>(std::move(error));
                    }
                    else
                    {
                        m_result.emplace<bcos::protocol::Block::Ptr>(std::move(block));
                    }
                    handle.resume();
                });
        }
        bcos::protocol::Block::Ptr await_resume()
        {
            if (std::holds_alternative<Error::Ptr>(m_result))
            {
                BOOST_THROW_EXCEPTION(*std::get<Error::Ptr>(m_result));
            }
            return std::move(std::get<bcos::protocol::Block::Ptr>(m_result));
        }
    };
    Awaitable awaitable{
        .m_ledger = ledger, .m_blockNumber = blockNumber, .m_blockFlag = blockFlag, .m_result = {}};

    co_return co_await awaitable;
}

bcos::task::Task<bcos::ledger::TransactionCount> bcos::ledger::tag_invoke(
    ledger::tag_t<getTransactionCount> /*unused*/, LedgerInterface& ledger)
{
    struct Awaitable
    {
        LedgerInterface& m_ledger;
        std::variant<Error::Ptr, TransactionCount> m_result;

        constexpr static bool await_ready() noexcept { return false; }
        void await_suspend(CO_STD::coroutine_handle<> handle)
        {
            m_ledger.asyncGetTotalTransactionCount(
                [this, handle](Error::Ptr error, int64_t total, int64_t failed,
                    bcos::protocol::BlockNumber blockNumber) {
                    if (error)
                    {
                        m_result.emplace<Error::Ptr>(std::move(error));
                    }
                    else
                    {
                        m_result.emplace<TransactionCount>(TransactionCount{
                            .total = total,
                            .failed = failed,
                            .blockNumber = blockNumber,
                        });
                    }
                    handle.resume();
                });
        }
        TransactionCount await_resume()
        {
            if (std::holds_alternative<Error::Ptr>(m_result))
            {
                BOOST_THROW_EXCEPTION(*std::get<Error::Ptr>(m_result));
            }
            return std::get<TransactionCount>(m_result);
        }
    };

    Awaitable awaitable{.m_ledger = ledger, .m_result = {}};
    co_return co_await awaitable;
}
bcos::task::Task<bcos::protocol::BlockNumber> bcos::ledger::tag_invoke(
    ledger::tag_t<getCurrentBlockNumber> /*unused*/, LedgerInterface& ledger)
{
    struct Awaitable
    {
        LedgerInterface& m_ledger;
        std::variant<Error::Ptr, protocol::BlockNumber> m_result;

        constexpr static bool await_ready() noexcept { return false; }
        void await_suspend(CO_STD::coroutine_handle<> handle)
        {
            m_ledger.asyncGetBlockNumber(
                [this, handle](Error::Ptr error, bcos::protocol::BlockNumber blockNumber) {
                    if (error)
                    {
                        m_result.emplace<Error::Ptr>(std::move(error));
                    }
                    else
                    {
                        m_result.emplace<protocol::BlockNumber>(blockNumber);
                    }
                    handle.resume();
                });
        }
        protocol::BlockNumber await_resume()
        {
            if (std::holds_alternative<Error::Ptr>(m_result))
            {
                BOOST_THROW_EXCEPTION(*std::get<Error::Ptr>(m_result));
            }
            return std::get<protocol::BlockNumber>(m_result);
        }
    };

    Awaitable awaitable{.m_ledger = ledger, .m_result = {}};
    co_return co_await awaitable;
}
bcos::task::Task<bcos::crypto::HashType> bcos::ledger::tag_invoke(
    ledger::tag_t<getBlockHash> /*unused*/, LedgerInterface& ledger,
    protocol::BlockNumber blockNumber)
{
    struct Awaitable
    {
        LedgerInterface& m_ledger;
        protocol::BlockNumber m_blockNumber;

        std::variant<Error::Ptr, crypto::HashType> m_result;

        constexpr static bool await_ready() noexcept { return false; }
        void await_suspend(CO_STD::coroutine_handle<> handle)
        {
            m_ledger.asyncGetBlockHashByNumber(
                m_blockNumber, [this, handle](Error::Ptr error, crypto::HashType hash) {
                    if (error)
                    {
                        m_result.emplace<Error::Ptr>(std::move(error));
                    }
                    else
                    {
                        m_result.emplace<crypto::HashType>(hash);
                    }
                    handle.resume();
                });
        }
        crypto::HashType await_resume()
        {
            if (std::holds_alternative<Error::Ptr>(m_result))
            {
                BOOST_THROW_EXCEPTION(*std::get<Error::Ptr>(m_result));
            }
            return std::get<crypto::HashType>(m_result);
        }
    };

    Awaitable awaitable{.m_ledger = ledger, .m_blockNumber = blockNumber, .m_result = {}};
    co_return co_await awaitable;
}
bcos::task::Task<bcos::ledger::SystemConfigEntry> bcos::ledger::tag_invoke(
    ledger::tag_t<getSystemConfig> /*unused*/, LedgerInterface& ledger, std::string_view key)
{
    struct Awaitable
    {
        LedgerInterface& m_ledger;
        std::string_view m_key;
        std::variant<Error::Ptr, SystemConfigEntry> m_result;

        constexpr static bool await_ready() noexcept { return false; }
        void await_suspend(CO_STD::coroutine_handle<> handle)
        {
            m_ledger.asyncGetSystemConfigByKey(
                m_key, [this, handle](Error::Ptr error, std::string value,
                           bcos::protocol::BlockNumber blockNumber) {
                    if (error)
                    {
                        m_result.emplace<Error::Ptr>(std::move(error));
                    }
                    else
                    {
                        m_result.emplace<SystemConfigEntry>(std::move(value), blockNumber);
                    }
                    handle.resume();
                });
        }
        SystemConfigEntry await_resume()
        {
            if (std::holds_alternative<Error::Ptr>(m_result))
            {
                BOOST_THROW_EXCEPTION(*std::get<Error::Ptr>(m_result));
            }
            return std::move(std::get<SystemConfigEntry>(m_result));
        }
    };

    Awaitable awaitable{.m_ledger = ledger, .m_key = key, .m_result = {}};
    co_return co_await awaitable;
}
bcos::task::Task<bcos::consensus::ConsensusNodeList> bcos::ledger::tag_invoke(
    ledger::tag_t<getNodeList> /*unused*/, LedgerInterface& ledger, std::string_view type)
{
    struct Awaitable
    {
        LedgerInterface& m_ledger;
        std::string_view m_type;
        std::variant<Error::Ptr, consensus::ConsensusNodeList> m_result;

        constexpr static bool await_ready() noexcept { return false; }
        void await_suspend(CO_STD::coroutine_handle<> handle)
        {
            m_ledger.asyncGetNodeListByType(
                m_type, [this, handle](Error::Ptr error,
                            const consensus::ConsensusNodeListPtr& consensusNodeList) {
                    if (error)
                    {
                        m_result.emplace<Error::Ptr>(std::move(error));
                    }
                    else
                    {
                        m_result.emplace<consensus::ConsensusNodeList>(
                            std::move(*consensusNodeList));
                    }
                    handle.resume();
                });
        }
        consensus::ConsensusNodeList await_resume()
        {
            if (std::holds_alternative<Error::Ptr>(m_result))
            {
                BOOST_THROW_EXCEPTION(*std::get<Error::Ptr>(m_result));
            }
            return std::move(std::get<consensus::ConsensusNodeList>(m_result));
        }
    };

    Awaitable awaitable{.m_ledger = ledger, .m_type = type, .m_result = {}};
    co_return co_await awaitable;
}

static bcos::task::Task<std::tuple<uint64_t, bcos::protocol::BlockNumber>> getSystemConfigOrDefault(
    bcos::ledger::LedgerInterface& ledger, std::string_view key, int64_t defaultValue)
{
    try
    {
        auto [value, blockNumber] = co_await bcos::ledger::getSystemConfig(ledger, key);
        co_return std::tuple<uint64_t, bcos::protocol::BlockNumber>{
            boost::lexical_cast<uint64_t>(value), blockNumber};
    }
    catch (std::exception& e)
    {
        LEDGER2_LOG(DEBUG) << "Get " << key << " failed, use default value"
                           << LOG_KV("defaultValue", defaultValue)
                           << boost::diagnostic_information(e);
        co_return std::tuple<uint64_t, bcos::protocol::BlockNumber>{defaultValue, 0};
    }
}

static bcos::task::Task<std::tuple<std::string, bcos::protocol::BlockNumber>>
getSystemConfigOrDefault(
    bcos::ledger::LedgerInterface& ledger, std::string_view key, std::string defaultValue)
{
    try
    {
        auto [value, blockNumber] = co_await bcos::ledger::getSystemConfig(ledger, key);
        co_return std::tuple<std::string, bcos::protocol::BlockNumber>{value, blockNumber};
    }
    catch (std::exception& e)
    {
        LEDGER2_LOG(DEBUG) << "Get " << key << " failed, use default value"
                           << LOG_KV("defaultValue", defaultValue)
                           << boost::diagnostic_information(e);
        co_return std::tuple<std::string, bcos::protocol::BlockNumber>{defaultValue, 0};
    }
}

bcos::task::Task<bcos::ledger::LedgerConfig::Ptr> bcos::ledger::tag_invoke(
    ledger::tag_t<getLedgerConfig> /*unused*/, LedgerInterface& ledger)
{
    auto ledgerConfig = std::make_shared<ledger::LedgerConfig>();
    ledgerConfig->setConsensusNodeList(co_await getNodeList(ledger, ledger::CONSENSUS_SEALER));
    ledgerConfig->setObserverNodeList(co_await getNodeList(ledger, ledger::CONSENSUS_OBSERVER));
    ledgerConfig->setBlockTxCountLimit(boost::lexical_cast<uint64_t>(
        std::get<0>(co_await getSystemConfig(ledger, SYSTEM_KEY_TX_COUNT_LIMIT))));
    ledgerConfig->setLeaderSwitchPeriod(boost::lexical_cast<uint64_t>(
        std::get<0>(co_await getSystemConfig(ledger, SYSTEM_KEY_CONSENSUS_LEADER_PERIOD))));
    ledgerConfig->setGasLimit(
        co_await getSystemConfigOrDefault(ledger, SYSTEM_KEY_TX_GAS_LIMIT, 0));
    ledgerConfig->setCompatibilityVersion(tool::toVersionNumber(
        std::get<0>(co_await getSystemConfig(ledger, SYSTEM_KEY_COMPATIBILITY_VERSION))));
    ledgerConfig->setGasPrice(
        co_await getSystemConfigOrDefault(ledger, SYSTEM_KEY_TX_GAS_PRICE, "0x0"));

    auto blockNumber = co_await getCurrentBlockNumber(ledger);
    ledgerConfig->setBlockNumber(blockNumber);
    auto hash = co_await getBlockHash(ledger, blockNumber);
    ledgerConfig->setHash(hash);
    ledgerConfig->setFeatures(co_await getFeatures(ledger));

    auto enableRPBFT =
        (std::get<0>(co_await getSystemConfigOrDefault(ledger, SYSTEM_KEY_RPBFT_SWITCH, 0)) == 1);
    ledgerConfig->setConsensusType(
        std::string(enableRPBFT ? ledger::RPBFT_CONSENSUS_TYPE : ledger::PBFT_CONSENSUS_TYPE));
    if (enableRPBFT)
    {
        ledgerConfig->setCandidateSealerNodeList(
            co_await getNodeList(ledger, ledger::CONSENSUS_CANDIDATE_SEALER));
        ledgerConfig->setEpochSealerNum(co_await getSystemConfigOrDefault(
            ledger, SYSTEM_KEY_RPBFT_EPOCH_SEALER_NUM, DEFAULT_EPOCH_SEALER_NUM));
        ledgerConfig->setEpochBlockNum(co_await getSystemConfigOrDefault(
            ledger, SYSTEM_KEY_RPBFT_EPOCH_BLOCK_NUM, DEFAULT_EPOCH_BLOCK_NUM));
        ledgerConfig->setNotifyRotateFlagInfo(std::get<0>(co_await getSystemConfigOrDefault(
            ledger, INTERNAL_SYSTEM_KEY_NOTIFY_ROTATE, DEFAULT_INTERNAL_NOTIFY_FLAG)));
    }
    ledgerConfig->setAuthCheckStatus(
        std::get<0>(co_await getSystemConfigOrDefault(ledger, SYSTEM_KEY_AUTH_CHECK_STATUS, 0)));

    LEDGER_LOG(INFO) << "LEDGER_CONFIG auth check status: " << ledgerConfig->authCheckStatus();

    co_return ledgerConfig;
}

bcos::task::Task<bcos::ledger::Features> bcos::ledger::tag_invoke(
    ledger::tag_t<getFeatures> /*unused*/, LedgerInterface& ledger)
{
    auto blockNumber = co_await getCurrentBlockNumber(ledger);
    Features features;
    for (auto key : bcos::ledger::Features::featureKeys())
    {
        try
        {
            auto value = co_await getSystemConfig(ledger, key);
            if (blockNumber + 1 >= std::get<1>(value))
            {
                features.set(key);
            }
        }
        catch (std::exception& e)
        {
            LEDGER2_LOG(DEBUG) << "Not found system config: " << key;
        }
    }

    co_return features;
}
