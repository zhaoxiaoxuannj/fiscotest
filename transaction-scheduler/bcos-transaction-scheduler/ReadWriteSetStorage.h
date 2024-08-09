#pragma once
#include "bcos-framework/storage2/Storage.h"
#include "bcos-framework/transaction-executor/TransactionExecutor.h"
#include <bcos-task/Trait.h>
#include <oneapi/tbb.h>
#include <compare>
#include <type_traits>
#include <variant>

namespace bcos::transaction_scheduler
{

template <class Storage, class KeyType>
class ReadWriteSetStorage
{
private:
    Storage& m_storage;
    struct ReadWriteFlag
    {
        bool read = false;
        bool write = false;
    };
    std::unordered_map<KeyType, ReadWriteFlag> m_readWriteSet;

    void putSet(bool write, auto const& key)
    {
        auto [it, inserted] =
            m_readWriteSet.try_emplace(KeyType(key), ReadWriteFlag{.read = !write, .write = write});
        if (!inserted)
        {
            it->second.write |= write;
            it->second.read |= (!write);
        }
    }

public:
    friend auto tag_invoke(storage2::tag_t<storage2::readSome> /*unused*/,
        ReadWriteSetStorage& storage, RANGES::input_range auto&& keys)
        -> task::Task<task::AwaitableReturnType<
            std::invoke_result_t<storage2::ReadSome, Storage&, decltype(keys)>>>
    {
        for (auto&& key : keys)
        {
            storage.putSet(false, std::forward<decltype(key)>(key));
        }
        co_return co_await storage2::readSome(
            storage.m_storage, std::forward<decltype(keys)>(keys));
    }

    friend auto tag_invoke(storage2::tag_t<storage2::readSome> /*unused*/,
        ReadWriteSetStorage& storage, RANGES::input_range auto&& keys,
        const storage2::READ_FRONT_TYPE& /*unused*/)
        -> task::Task<task::AwaitableReturnType<
            std::invoke_result_t<storage2::ReadSome, Storage&, decltype(keys)>>>
    {
        co_return co_await storage2::readSome(
            storage.m_storage, std::forward<decltype(keys)>(keys));
    }

    friend auto tag_invoke(
        storage2::tag_t<storage2::readOne> /*unused*/, ReadWriteSetStorage& storage, auto&& key)
        -> task::Task<task::AwaitableReturnType<
            std::invoke_result_t<storage2::ReadOne, Storage&, decltype(key)>>>
    {
        storage.putSet(false, key);
        co_return co_await storage2::readOne(storage.m_storage, std::forward<decltype(key)>(key));
    }

    friend auto tag_invoke(storage2::tag_t<storage2::readOne> /*unused*/,
        ReadWriteSetStorage& storage, auto&& key, storage2::READ_FRONT_TYPE /*unused*/)
        -> task::Task<task::AwaitableReturnType<
            std::invoke_result_t<storage2::ReadOne, Storage&, decltype(key)>>>
    {
        co_return co_await storage2::readOne(storage.m_storage, std::forward<decltype(key)>(key));
    }

    friend auto tag_invoke(storage2::tag_t<storage2::writeSome> /*unused*/,
        ReadWriteSetStorage& storage, RANGES::input_range auto&& keys,
        RANGES::input_range auto&& values)
        -> task::Task<task::AwaitableReturnType<
            std::invoke_result_t<storage2::WriteSome, Storage&, decltype(keys), decltype(values)>>>
    {
        for (auto&& key : keys)
        {
            storage.putSet(true, std::forward<decltype(key)>(key));
        }
        co_return co_await storage2::writeSome(
            storage.m_storage, keys, std::forward<decltype(values)>(values));
    }

    friend auto tag_invoke(storage2::tag_t<storage2::removeSome> /*unused*/,
        ReadWriteSetStorage& storage, RANGES::input_range auto const& keys)
        -> task::Task<task::AwaitableReturnType<
            std::invoke_result_t<storage2::RemoveSome, Storage&, decltype(keys)>>>
    {
        for (auto&& key : keys)
        {
            storage.putSet(true, std::forward<decltype(key)>(key));
        }
        co_return co_await storage2::removeSome(storage.m_storage, keys);
    }

    using Key = KeyType;
    using Value = typename task::AwaitableReturnType<decltype(storage2::readOne(
        m_storage, std::declval<KeyType>()))>::value_type;

    ReadWriteSetStorage(Storage& storage) : m_storage(storage) {}

    auto& readWriteSet() { return m_readWriteSet; }
    auto const& readWriteSet() const { return m_readWriteSet; }
    void mergeWriteSet(auto& inputWriteSet)
    {
        auto& writeMap = inputWriteSet.readWriteSet();
        for (auto& [key, flag] : writeMap)
        {
            if (flag.write)
            {
                putSet(true, key);
            }
        }
    }

    // RAW: read after write
    bool hasRAWIntersection(const auto& rhs) const
    {
        auto const& lhsSet = m_readWriteSet;
        auto const& rhsSet = rhs.readWriteSet();

        if (RANGES::empty(lhsSet) || RANGES::empty(rhsSet))
        {
            return false;
        }

        for (auto const& [key, flag] : rhsSet)
        {
            if (flag.read && lhsSet.contains(key))
            {
                return true;
            }
        }

        return false;
    }
};

}  // namespace bcos::transaction_scheduler