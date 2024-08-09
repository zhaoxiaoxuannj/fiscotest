#pragma once
#include "bcos-concepts/ByteBuffer.h"
#include "bcos-concepts/Exception.h"
#include "bcos-framework/storage/Entry.h"
#include "bcos-framework/storage2/Storage.h"
#include "bcos-framework/transaction-executor/TransactionExecutor.h"
#include "bcos-task/AwaitableValue.h"
#include "bcos-task/Wait.h"
#include "bcos-utilities/Error.h"
#include "bcos-utilities/Overloaded.h"
#include <oneapi/tbb/concurrent_vector.h>
#include <oneapi/tbb/parallel_for_each.h>
#include <oneapi/tbb/parallel_pipeline.h>
#include <rocksdb/db.h>
#include <rocksdb/iterator.h>
#include <rocksdb/slice.h>
#include <boost/throw_exception.hpp>
#include <functional>
#include <type_traits>
#include <variant>

namespace bcos::storage2::rocksdb
{

template <class ResolverType, class Item>
concept Resolver = requires(ResolverType&& resolver) {
                       // clang-format off
    resolver.encode(std::declval<Item>());
    { resolver.decode(std::string_view{})} -> std::convertible_to<Item>;
                       // clang-format on
                   };

// clang-format off
struct RocksDBException : public bcos::Error {};
struct UnsupportedMethod : public bcos::Error {};
struct UnexpectedItemType : public bcos::Error {};
// clang-format on

// Copy from rocksdb util/coding.h
inline constexpr int varintLength(uint64_t v)
{
    int len = 1;
    while (v >= 128)
    {
        v >>= 7;
        len++;
    }
    return len;
}

constexpr static size_t ROCKSDB_SEP_HEADER_SIZE = 12;
constexpr inline size_t getRocksDBKeyPairSize(
    bool hashColumnFamily, size_t keySize, size_t valueSize)
{
    /*
    default cf:
    |KTypeValue|
    |key_size|key_bytes|
    |value_length|value_bytes|
    */
    return hashColumnFamily ?
               1 + 4 + varintLength(keySize) + keySize + varintLength(valueSize) + valueSize :
               1 + varintLength(keySize) + keySize + varintLength(valueSize) + valueSize;
}

constexpr static auto ROCKSDB_WRITE_CHUNK_SIZE = 64;

template <class KeyType, class ValueType, Resolver<KeyType> KeyResolver,
    Resolver<ValueType> ValueResolver>
class RocksDBStorage2
{
private:
    ::rocksdb::DB& m_rocksDB;
    [[no_unique_address]] KeyResolver m_keyResolver;
    [[no_unique_address]] ValueResolver m_valueResolver;

public:
    RocksDBStorage2(::rocksdb::DB& rocksDB) : m_rocksDB(rocksDB) {}
    RocksDBStorage2(
        ::rocksdb::DB& rocksDB, KeyResolver&& keyResolver, ValueResolver&& valueResolver)
      : m_rocksDB(rocksDB),
        m_keyResolver(std::forward<KeyResolver>(keyResolver)),
        m_valueResolver(std::forward<ValueResolver>(valueResolver))
    {}
    using Key = KeyType;
    using Value = ValueType;

    static auto executeReadSome(RocksDBStorage2& storage, RANGES::input_range auto&& keys)
    {
        auto encodedKeys = keys | RANGES::views::transform([&](auto&& key) {
            return storage.m_keyResolver.encode(std::forward<decltype(key)>(key));
        }) | RANGES::to<std::vector>();

        std::vector<::rocksdb::PinnableSlice> results(RANGES::size(encodedKeys));
        std::vector<::rocksdb::Status> status(RANGES::size(encodedKeys));

        auto rocksDBKeys = encodedKeys | RANGES::views::transform([](const auto& encodedKey) {
            return ::rocksdb::Slice(RANGES::data(encodedKey), RANGES::size(encodedKey));
        }) | RANGES::to<std::vector>();
        storage.m_rocksDB.MultiGet(::rocksdb::ReadOptions(),
            storage.m_rocksDB.DefaultColumnFamily(), rocksDBKeys.size(), rocksDBKeys.data(),
            results.data(), status.data());

        auto result =
            RANGES::views::zip(results, status) |
            RANGES::views::transform([&](auto tuple) -> std::optional<ValueType> {
                auto& [result, status] = tuple;
                if (!status.ok())
                {
                    if (!status.IsNotFound())
                    {
                        BOOST_THROW_EXCEPTION(
                            RocksDBException{} << bcos::Error::ErrorMessage(status.ToString()));
                    }
                    return {};
                }

                return std::make_optional(storage.m_valueResolver.decode(result.ToStringView()));
            }) |
            RANGES::to<std::vector>();
        return result;
    }

    friend auto tag_invoke(storage2::tag_t<storage2::readSome> /*unused*/, RocksDBStorage2& storage,
        RANGES::input_range auto&& keys) -> task::AwaitableValue<decltype(executeReadSome(storage,
        std::forward<decltype(keys)>(keys)))>
    {
        return {executeReadSome(storage, std::forward<decltype(keys)>(keys))};
    }

    friend auto tag_invoke(storage2::tag_t<storage2::readOne> /*unused*/, RocksDBStorage2& storage,
        auto&& key) -> task::AwaitableValue<std::optional<ValueType>>
    {
        auto rocksDBKey = storage.m_keyResolver.encode(key);
        std::string result;
        auto status =
            storage.m_rocksDB.Get(::rocksdb::ReadOptions(), storage.m_rocksDB.DefaultColumnFamily(),
                ::rocksdb::Slice(RANGES::data(rocksDBKey), RANGES::size(rocksDBKey)),
                std::addressof(result));
        if (!status.ok())
        {
            if (!status.IsNotFound())
            {
                BOOST_THROW_EXCEPTION(
                    RocksDBException{} << bcos::Error::ErrorMessage(status.ToString()));
            }
            return {std::optional<ValueType>{}};
        }

        return {std::make_optional(storage.m_valueResolver.decode(std::move(result)))};
    }

    friend task::AwaitableValue<void> tag_invoke(storage2::tag_t<storage2::writeSome> /*unused*/,
        RocksDBStorage2& storage, RANGES::input_range auto&& keys,
        RANGES::input_range auto&& values)
    {
        using RocksDBKeyValueTuple =
            std::tuple<decltype(storage.m_keyResolver.encode(
                           std::declval<RANGES::range_value_t<decltype(keys)>>())),
                decltype(storage.m_valueResolver.encode(
                    std::declval<RANGES::range_value_t<decltype(values)>>()))>;

        std::atomic_size_t totalReservedLength = ROCKSDB_SEP_HEADER_SIZE;
        tbb::concurrent_vector<RocksDBKeyValueTuple> rocksDBKeyValues;
        rocksDBKeyValues.reserve(RANGES::size(keys));
        auto chunkRange =
            RANGES::views::zip(keys, values) | RANGES::views::chunk(ROCKSDB_WRITE_CHUNK_SIZE);
        tbb::task_group writeGroup;
        for (auto&& subrange : chunkRange)
        {
            writeGroup.run([subrange = std::forward<decltype(subrange)>(subrange),
                               &rocksDBKeyValues, &storage, &totalReservedLength]() {
                size_t localReservedLength = 0;
                for (auto&& [key, value] : subrange)
                {
                    auto it = rocksDBKeyValues.emplace_back(
                        storage.m_keyResolver.encode(std::forward<decltype(key)>(key)),
                        storage.m_valueResolver.encode(std::forward<decltype(value)>(value)));
                    auto const& [keyBuffer, valueBuffer] = *it;
                    localReservedLength += getRocksDBKeyPairSize(
                        false, RANGES::size(keyBuffer), RANGES::size(valueBuffer));
                }
                totalReservedLength += localReservedLength;
            });
        }
        writeGroup.wait();

        ::rocksdb::WriteBatch writeBatch(totalReservedLength);
        for (auto&& [keyBuffer, valueBuffer] : rocksDBKeyValues)
        {
            writeBatch.Put(::rocksdb::Slice(RANGES::data(keyBuffer), RANGES::size(keyBuffer)),
                ::rocksdb::Slice(RANGES::data(valueBuffer), RANGES::size(valueBuffer)));
        }
        ::rocksdb::WriteOptions options;
        auto status = storage.m_rocksDB.Write(options, &writeBatch);

        if (!status.ok())
        {
            BOOST_THROW_EXCEPTION(RocksDBException{} << error::ErrorMessage(status.ToString()));
        }
        return {};
    }

    friend task::AwaitableValue<void> tag_invoke(
        storage2::tag_t<storage2::writeOne> /*unused*/, RocksDBStorage2& storage, auto const& key)
    {
        auto rocksDBKey = storage.m_keyResolver.encode(key);
        auto rocksDBValue = storage.m_valueResolver.encode(key);

        ::rocksdb::WriteOptions options;
        auto status = storage.m_rocksDB.Put(options,
            ::rocksdb::Slice(RANGES::data(rocksDBKey), RANGES::size(rocksDBKey)),
            ::rocksdb::Slice(RANGES::data(rocksDBValue), RANGES::size(rocksDBValue)));

        if (!status.ok())
        {
            BOOST_THROW_EXCEPTION(RocksDBException{} << error::ErrorMessage(status.ToString()));
        }
    }

    friend task::AwaitableValue<void> tag_invoke(storage2::tag_t<storage2::removeSome> /*unused*/,
        RocksDBStorage2& storage, RANGES::input_range auto const& keys)
    {
        ::rocksdb::WriteBatch writeBatch;

        for (auto const& key : keys)
        {
            auto encodedKey = storage.m_keyResolver.encode(key);
            writeBatch.Delete(::rocksdb::Slice(RANGES::data(encodedKey), RANGES::size(encodedKey)));
        }

        ::rocksdb::WriteOptions options;
        auto status = storage.m_rocksDB.Write(options, &writeBatch);
        if (!status.ok())
        {
            BOOST_THROW_EXCEPTION(RocksDBException{} << error::ErrorMessage(status.ToString()));
        }

        return {};
    }

    friend task::Task<void> tag_invoke(
        storage2::tag_t<merge> /*unused*/, RocksDBStorage2& storage, auto&& fromStorage)
    {
        auto range = co_await storage2::range(fromStorage);

        using RangeValueType = RANGES::range_value_t<decltype(range)>;
        using RocksDBKeyValueTuple = std::tuple<
            decltype(storage.m_keyResolver.encode(
                std::declval<std::remove_pointer_t<std::tuple_element_t<0, RangeValueType>>>())),
            std::optional<decltype(storage.m_valueResolver.encode(
                std::declval<std::remove_pointer_t<std::tuple_element_t<1, RangeValueType>>>()))>>;

        std::atomic_size_t totalReservedLength = ROCKSDB_SEP_HEADER_SIZE;
        tbb::concurrent_vector<RocksDBKeyValueTuple> rocksDBKeyValues;
        auto chunkRange = range | RANGES::views::chunk(ROCKSDB_WRITE_CHUNK_SIZE);
        tbb::task_group mergeGroup;
        for (auto&& subrange : chunkRange)
        {
            mergeGroup.run([&rocksDBKeyValues, &storage, &totalReservedLength,
                               subrange = std::forward<decltype(subrange)>(subrange)]() {
                size_t localReservedLength = 0;
                for (auto [key, value] : subrange)
                {
                    if (value)
                    {
                        auto it = rocksDBKeyValues.emplace_back(storage.m_keyResolver.encode(*key),
                            std::make_optional(storage.m_valueResolver.encode(*value)));
                        auto&& [keyBuffer, valueBuffer] = *it;
                        localReservedLength += getRocksDBKeyPairSize(
                            false, RANGES::size(keyBuffer), RANGES::size(*valueBuffer));
                    }
                    else
                    {
                        auto it = rocksDBKeyValues.emplace_back(storage.m_keyResolver.encode(*key),
                            std::tuple_element_t<1, RocksDBKeyValueTuple>{});
                        auto&& [keyBuffer, valueBuffer] = *it;
                        localReservedLength +=
                            getRocksDBKeyPairSize(false, RANGES::size(keyBuffer), 0);
                    }
                }
                totalReservedLength += localReservedLength;
            });
        }
        mergeGroup.wait();

        ::rocksdb::WriteBatch writeBatch(totalReservedLength);
        for (auto&& [keyBuffer, valueBuffer] : rocksDBKeyValues)
        {
            if (valueBuffer)
            {
                writeBatch.Put(::rocksdb::Slice(RANGES::data(keyBuffer), RANGES::size(keyBuffer)),
                    ::rocksdb::Slice(RANGES::data(*valueBuffer), RANGES::size(*valueBuffer)));
            }
            else
            {
                writeBatch.Delete(
                    ::rocksdb::Slice(RANGES::data(keyBuffer), RANGES::size(keyBuffer)));
            }
        }
        ::rocksdb::WriteOptions options;
        auto status = storage.m_rocksDB.Write(options, std::addressof(writeBatch));

        if (!status.ok())
        {
            BOOST_THROW_EXCEPTION(RocksDBException{} << error::ErrorMessage(status.ToString()));
        }
    }
};

}  // namespace bcos::storage2::rocksdb