#pragma once
#include "Task.h"
#include "Trait.h"
#include <boost/atomic/atomic_flag.hpp>
#include <exception>
#include <future>
#include <type_traits>
#include <variant>

namespace bcos::task
{

struct Wait
{
    void operator()(auto&& task) const
        requires std::is_rvalue_reference_v<decltype(task)>
    {
        task.start();
    }
};
constexpr inline Wait wait{};

struct SyncWait
{
    template <class Task>
    auto operator()(Task&& task) const -> AwaitableReturnType<std::remove_cvref_t<Task>>
        requires IsAwaitable<Task> && std::is_rvalue_reference_v<decltype(task)>
    {
        using ReturnType = AwaitableReturnType<std::remove_cvref_t<Task>>;
        using ReturnTypeWrap = std::conditional_t<std::is_reference_v<ReturnType>,
            std::add_pointer_t<ReturnType>, ReturnType>;
        using ReturnVariant = std::conditional_t<std::is_void_v<ReturnType>,
            std::variant<std::monostate, std::exception_ptr>,
            std::variant<std::monostate, ReturnTypeWrap, std::exception_ptr>>;
        ReturnVariant result;
        boost::atomic_flag finished;
        boost::atomic_flag waitFlag;

        auto waitTask = [](Task&& task, decltype(result)& result, boost::atomic_flag& finished,
                            boost::atomic_flag& waitFlag) -> task::Task<void> {
            try
            {
                if constexpr (std::is_void_v<ReturnType>)
                {
                    co_await task;
                }
                else
                {
                    if constexpr (std::is_reference_v<ReturnType>)
                    {
                        decltype(auto) ref = co_await task;
                        result = std::addressof(ref);
                    }
                    else
                    {
                        result = co_await task;
                    }
                }
            }
            catch (...)
            {
                result = std::current_exception();
            }

            if (finished.test_and_set())
            {
                // 此处返回true说明外部首先设置了finished，那么需要通知外部已经执行完成了
                // If true is returned here, the external finish is set first, and the external
                // execution needs to be notified
                waitFlag.test_and_set();
                waitFlag.notify_one();
            }
        }(std::forward<Task>(task), result, finished, waitFlag);
        waitTask.start();

        if (!finished.test_and_set())
        {
            // 此处返回false说明task还在执行中，需要等待task完成
            // If false is returned, the task is still being executed and you need to wait for the
            // task to complete
            waitFlag.wait(false);
        }
        if (std::holds_alternative<std::exception_ptr>(result))
        {
            std::rethrow_exception(std::get<std::exception_ptr>(result));
        }

        if constexpr (!std::is_void_v<ReturnType>)
        {
            if constexpr (std::is_reference_v<ReturnType>)
            {
                return *(std::get<ReturnTypeWrap>(result));
            }
            else
            {
                return std::move(std::get<ReturnTypeWrap>(result));
            }
        }
    }
};
constexpr inline SyncWait syncWait{};

}  // namespace bcos::task