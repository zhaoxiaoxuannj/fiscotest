#pragma once
// if windows, manual include tup/Tars.h first
#ifdef _WIN32
#include <tup/Tars.h>
#endif
#include "bcos-tars-protocol/tars/TransactionReceipt.h"
#include <bcos-concepts/Basic.h>
#include <bcos-concepts/ByteBuffer.h>
#include <bcos-crypto/hasher/Hasher.h>
#include <bcos-framework/protocol/Protocol.h>
#include <bcos-tars-protocol/tars/Block.h>
#include <bcos-tars-protocol/tars/Transaction.h>
#include <boost/endian/conversion.hpp>
#include <string>
#include <vector>

namespace bcostars
{

void impl_calculate(bcos::crypto::hasher::Hasher auto hasher,
    bcostars::Transaction const& transaction, bcos::concepts::bytebuffer::ByteBuffer auto& out)
{
    if (!transaction.dataHash.empty())
    {
        bcos::concepts::bytebuffer::assignTo(transaction.dataHash, out);
        return;
    }

    impl_calculate(std::forward<decltype(hasher)>(hasher), transaction.data, out);
}

void impl_calculate(bcos::crypto::hasher::Hasher auto hasher,
    bcostars::TransactionData const& hashFields, bcos::concepts::bytebuffer::ByteBuffer auto& out)
{
    int32_t version = boost::endian::native_to_big((int32_t)hashFields.version);
    hasher.update(version);
    hasher.update(hashFields.chainID);
    hasher.update(hashFields.groupID);
    int64_t blockLimit = boost::endian::native_to_big((int64_t)hashFields.blockLimit);
    hasher.update(blockLimit);
    hasher.update(hashFields.nonce);
    hasher.update(hashFields.to);
    hasher.update(hashFields.input);
    hasher.update(hashFields.abi);
    // if version == 1, update value, gasPrice, gasLimit, maxFeePerGas, maxPriorityFeePerGas to
    // hashBuffer calculate hash
    if ((uint32_t)hashFields.version >= (uint32_t)bcos::protocol::TransactionVersion::V1_VERSION)
    {
        hasher.update(hashFields.value);
        hasher.update(hashFields.gasPrice);
        int64_t bigEndGasLimit = boost::endian::native_to_big((int64_t)hashFields.gasLimit);
        hasher.update(bigEndGasLimit);
        hasher.update(hashFields.maxFeePerGas);
        hasher.update(hashFields.maxPriorityFeePerGas);
    }
    if ((uint32_t)hashFields.version >= (uint32_t)bcos::protocol::TransactionVersion::V2_VERSION)
    {
        hasher.update(hashFields.extension);
    }

    hasher.final(out);
}

void impl_calculate(bcos::crypto::hasher::Hasher auto hasher,
    bcostars::TransactionReceipt const& receipt, bcos::concepts::bytebuffer::ByteBuffer auto& out)
{
    if (!receipt.dataHash.empty())
    {
        bcos::concepts::bytebuffer::assignTo(receipt.dataHash, out);
        return;
    }

    impl_calculate(std::forward<decltype(hasher)>(hasher), receipt.data, out);
}

void impl_calculate(bcos::crypto::hasher::Hasher auto hasher,
    bcostars::TransactionReceiptData const& hashFields,
    bcos::concepts::bytebuffer::ByteBuffer auto& out)
{
    int32_t version = boost::endian::native_to_big((int32_t)hashFields.version);
    hasher.update(version);
    hasher.update(hashFields.gasUsed);
    hasher.update(hashFields.contractAddress);
    int32_t status = boost::endian::native_to_big((int32_t)hashFields.status);
    hasher.update(status);
    hasher.update(hashFields.output);
    if (hashFields.version >= int32_t(bcos::protocol::TransactionVersion::V1_VERSION))
    {
        hasher.update(hashFields.effectiveGasPrice);
    }
    for (auto const& log : hashFields.logEntries)
    {
        hasher.update(log.address);
        for (auto const& topicItem : log.topic)
        {
            hasher.update(topicItem);
        }
        hasher.update(log.data);
    }
    int64_t blockNumber = boost::endian::native_to_big((int64_t)hashFields.blockNumber);
    hasher.update(blockNumber);
    hasher.final(out);
}

auto impl_calculate(bcos::crypto::hasher::Hasher auto hasher,
    bcostars::BlockHeader const& blockHeader, bcos::concepts::bytebuffer::ByteBuffer auto& out)
{
    if (!blockHeader.dataHash.empty())
    {
        bcos::concepts::bytebuffer::assignTo(blockHeader.dataHash, out);
        return;
    }

    auto const& hashFields = blockHeader.data;

    int32_t version = boost::endian::native_to_big((int32_t)hashFields.version);
    hasher.update(version);
    for (auto const& parent : hashFields.parentInfo)
    {
        int64_t blockNumber = boost::endian::native_to_big((int64_t)parent.blockNumber);
        hasher.update(blockNumber);
        hasher.update(parent.blockHash);
    }
    hasher.update(hashFields.txsRoot);
    hasher.update(hashFields.receiptRoot);
    hasher.update(hashFields.stateRoot);

    int64_t number = boost::endian::native_to_big((int64_t)hashFields.blockNumber);
    hasher.update(number);
    hasher.update(hashFields.gasUsed);

    int64_t timestamp = boost::endian::native_to_big((int64_t)hashFields.timestamp);
    hasher.update(timestamp);

    int64_t sealer = boost::endian::native_to_big((int64_t)hashFields.sealer);
    hasher.update(sealer);

    for (auto const& nodeID : hashFields.sealerList)
    {
        hasher.update(nodeID);
    }
    // update extraData to hashBuffer: 12
    hasher.update(hashFields.extraData);
    // update consensusWeights to hashBuffer: 13
    for (auto weight : hashFields.consensusWeights)
    {
        int64_t networkWeight = boost::endian::native_to_big((int64_t)weight);
        hasher.update(networkWeight);
    }

    hasher.final(out);
}

auto impl_calculate(bcos::crypto::hasher::Hasher auto hasher, bcostars::Block const& block,
    bcos::concepts::bytebuffer::ByteBuffer auto& out)
{
    if (!block.blockHeader.dataHash.empty())
    {
        bcos::concepts::bytebuffer::assignTo(block.blockHeader.dataHash, out);
        return;
    }

    impl_calculate(std::move(hasher), block.blockHeader, out);
}

}  // namespace bcostars
