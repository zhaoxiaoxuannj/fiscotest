#pragma once
#include "bcos-framework/storage/Common.h"
#include "bcos-framework/storage/StorageInterface.h"
#include "bcos-framework/storage2/Storage.h"
#include "bcos-framework/transaction-executor/TransactionExecutor.h"
#include "bcos-table/src/StateStorageInterface.h"
#include "bcos-task/Task.h"
#include "bcos-task/Wait.h"
#include "bcos-utilities/Error.h"
#include <boost/throw_exception.hpp>
#include <exception>
#include <iterator>
#include <stdexcept>

namespace bcos::storage
{

template <class Storage>
class LegacyStorageWrapper : public virtual bcos::storage::StorageInterface
{
private:
    Storage& m_storage;

public:
    explicit LegacyStorageWrapper(Storage& m_storage) : m_storage(m_storage) {}

    void asyncGetPrimaryKeys(std::string_view table,
        const std::optional<storage::Condition const>& condition,
        std::function<void(Error::UniquePtr, std::vector<std::string>)> _callback) override
    {
        _callback(BCOS_ERROR_UNIQUE_PTR(-1, "asyncGetPrimaryKeys error!"), {});
    }

    void asyncGetRow(std::string_view table, std::string_view key,
        std::function<void(Error::UniquePtr, std::optional<storage::Entry>)> callback) override
    {
        task::wait([](decltype(this) self, decltype(table) table, decltype(key) key,
                       decltype(callback) callback) -> task::Task<void> {
            try
            {
                auto value = co_await storage2::readOne(
                    self->m_storage, transaction_executor::StateKeyView{table, key});
                callback(nullptr, std::move(value));
            }
            catch (std::exception& e)
            {
                callback(BCOS_ERROR_WITH_PREV_UNIQUE_PTR(-1, "asyncGetRow error!", e), {});
            }
        }(this, table, key, std::move(callback)));
    }

    void asyncGetRows(std::string_view table,
        RANGES::any_view<std::string_view,
            RANGES::category::input | RANGES::category::random_access | RANGES::category::sized>
            keys,
        std::function<void(Error::UniquePtr, std::vector<std::optional<storage::Entry>>)> callback)
        override
    {
        task::wait([](decltype(this) self, decltype(table) table, decltype(keys) keys,
                       decltype(callback) callback) -> task::Task<void> {
            try
            {
                auto stateKeys = keys | RANGES::views::transform([&table](auto&& key) -> auto {
                    return transaction_executor::StateKeyView{
                        table, std::forward<decltype(key)>(key)};
                }) | RANGES::to<std::vector>();
                auto values = co_await storage2::readSome(self->m_storage, stateKeys);

                std::vector<std::optional<storage::Entry>> vectorValues(
                    std::make_move_iterator(values.begin()), std::make_move_iterator(values.end()));
                callback(nullptr, std::move(vectorValues));
            }
            catch (std::exception& e)
            {
                callback(BCOS_ERROR_WITH_PREV_UNIQUE_PTR(-1, "asyncGetRows error!", e), {});
            }
        }(this, table, std::move(keys), std::move(callback)));
    }

    void asyncSetRow(std::string_view table, std::string_view key, storage::Entry entry,
        std::function<void(Error::UniquePtr)> callback) override
    {
        task::wait([](decltype(this) self, decltype(table) table, decltype(key) key,
                       decltype(entry) entry, decltype(callback) callback) -> task::Task<void> {
            try
            {
                co_await storage2::writeOne(
                    self->m_storage, transaction_executor::StateKey(table, key), std::move(entry));
                callback(nullptr);
            }
            catch (std::exception& e)
            {
                callback(BCOS_ERROR_WITH_PREV_UNIQUE_PTR(-1, "asyncSetRow error!", e));
            }
        }(this, table, key, std::move(entry), std::move(callback)));
    }

    Error::Ptr setRows(std::string_view tableName,
        RANGES::any_view<std::string_view,
            RANGES::category::random_access | RANGES::category::sized>
            keys,
        RANGES::any_view<std::string_view,
            RANGES::category::random_access | RANGES::category::sized>
            values) override
    {
        return task::syncWait(
            [](decltype(this) self, decltype(tableName) tableName, decltype(keys)& keys,
                decltype(values)& values) -> task::Task<Error::Ptr> {
                try
                {
                    co_await storage2::writeSome(self->m_storage,
                        keys | RANGES::views::transform([&](std::string_view key) {
                            return transaction_executor::StateKey{tableName, key};
                        }),
                        values | RANGES::views::transform([](std::string_view value) -> auto {
                            storage::Entry entry;
                            entry.setField(0, value);
                            return entry;
                        }));
                    co_return nullptr;
                }
                catch (std::exception& e)
                {
                    co_return BCOS_ERROR_WITH_PREV_PTR(-1, "setRows error!", e);
                }
            }(this, tableName, keys, values));
    };
};

template <class Storage>
class LegacyStateStorageWrapper : public virtual storage::StateStorageInterface,
                                  public virtual LegacyStorageWrapper<Storage>
{
public:
    LegacyStateStorageWrapper(Storage& m_storage)
      : StateStorageInterface(nullptr), LegacyStorageWrapper<Storage>(m_storage)
    {}

    void parallelTraverse(bool onlyDirty,
        std::function<bool(const std::string_view& table, const std::string_view& key,
            storage::Entry const& entry)>
            callback) const override
    {
        BOOST_THROW_EXCEPTION(std::runtime_error("Unimplemented!"));
    }

    void rollback(const storage::Recoder& recoder) override
    {
        BOOST_THROW_EXCEPTION(std::runtime_error("Unimplemented!"));
    }

    crypto::HashType hash(
        const bcos::crypto::Hash::Ptr& hashImpl, const ledger::Features& features) const override
    {
        BOOST_THROW_EXCEPTION(std::runtime_error("Unimplemented!"));
    }
};

}  // namespace bcos::storage