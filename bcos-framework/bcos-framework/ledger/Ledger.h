#pragma once

#include "LedgerConfig.h"
#include "LedgerTypeDef.h"
#include "bcos-framework/ledger/Features.h"
#include "bcos-framework/protocol/Block.h"
#include "bcos-framework/protocol/ProtocolTypeDef.h"
#include "bcos-ledger/src/libledger/LedgerImpl.h"
#include "bcos-task/Task.h"
#include "bcos-task/Trait.h"
#include "bcos-tool/ConsensusNode.h"
#include <type_traits>

namespace bcos::ledger
{
inline constexpr struct BuildGenesisBlock
{
    task::Task<bool> operator()(
        auto& ledger, GenesisConfig const& genesis, ledger::LedgerConfig const& ledgerConfig) const
    {
        co_return co_await tag_invoke(*this, ledger, genesis, ledgerConfig);
    }
} buildGenesisBlock{};

struct PrewriteBlock
{
    task::Task<void> operator()(auto& ledger, bcos::protocol::ConstTransactionsPtr transactions,
        bcos::protocol::Block::ConstPtr block, bool withTransactionsAndReceipts,
        auto& storage) const
    {
        co_await tag_invoke(*this, ledger, std::move(transactions), std::move(block),
            withTransactionsAndReceipts, storage);
    }
};
inline constexpr PrewriteBlock prewriteBlock{};

struct StoreTransactionsAndReceipts
{
    task::Task<void> operator()(auto& ledger, bcos::protocol::ConstTransactionsPtr transactions,
        bcos::protocol::Block::ConstPtr block) const
    {
        co_await tag_invoke(*this, ledger, std::move(transactions), std::move(block));
    }
};
inline constexpr StoreTransactionsAndReceipts storeTransactionsAndReceipts{};

struct GetBlockData
{
    task::Task<protocol::Block::Ptr> operator()(
        auto& ledger, protocol::BlockNumber blockNumber, int32_t blockFlag) const
    {
        co_return co_await tag_invoke(*this, ledger, blockNumber, blockFlag);
    }
};
inline constexpr GetBlockData getBlockData{};

struct TransactionCount
{
    int64_t total{};
    int64_t failed{};
    bcos::protocol::BlockNumber blockNumber{};
};
struct GetTransactionCount
{
    task::Task<TransactionCount> operator()(auto& ledger) const
    {
        co_return co_await tag_invoke(*this, ledger);
    }
};
inline constexpr GetTransactionCount getTransactionCount{};

struct GetCurrentBlockNumber
{
    task::Task<protocol::BlockNumber> operator()(auto& ledger) const
    {
        co_return co_await tag_invoke(*this, ledger);
    }
};
inline constexpr GetCurrentBlockNumber getCurrentBlockNumber{};

struct GetBlockHash
{
    task::Task<crypto::HashType> operator()(auto& ledger, protocol::BlockNumber blockNumber) const
    {
        co_return co_await tag_invoke(*this, ledger, blockNumber);
    }
};
inline constexpr GetBlockHash getBlockHash{};

using SystemConfigEntry = std::tuple<std::string, protocol::BlockNumber>;
struct GetSystemConfig
{
    task::Task<SystemConfigEntry> operator()(auto& ledger, std::string_view key) const
    {
        co_return co_await tag_invoke(*this, ledger, key);
    }
};
inline constexpr GetSystemConfig getSystemConfig{};

struct GetNodeList
{
    task::Task<consensus::ConsensusNodeList> operator()(auto& ledger, std::string_view type) const
    {
        co_return co_await tag_invoke(*this, ledger, type);
    }
};
inline constexpr GetNodeList getNodeList{};

struct GetLedgerConfig
{
    task::Task<LedgerConfig::Ptr> operator()(auto& ledger) const
    {
        co_return co_await tag_invoke(*this, ledger);
    }
};
inline constexpr GetLedgerConfig getLedgerConfig{};

struct GetFeatures
{
    task::Task<Features> operator()(auto& ledger) const
    {
        co_return co_await tag_invoke(*this, ledger);
    }
};
inline constexpr GetFeatures getFeatures{};

template <auto& Tag>
using tag_t = std::decay_t<decltype(Tag)>;
}  // namespace bcos::ledger