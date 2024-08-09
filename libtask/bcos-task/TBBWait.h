#pragma once

#include "Task.h"
#include "Trait.h"
#include <oneapi/tbb/task.h>
#include <boost/atomic/atomic.hpp>
#include <boost/atomic/atomic_flag.hpp>

namespace bcos::task::tbb
{

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
        boost::atomic_flag finished{};
        boost::atomic<oneapi::tbb::task::suspend_point> suspendPoint{};

        auto waitTask =
            [](Task&& task, decltype(result)& result, boost::atomic_flag& finished,
                boost::atomic<oneapi::tbb::task::suspend_point>& suspendPoint) -> task::Task<void> {
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
                // finished已经被设置,说明外部已经suspend了,此处要获取spsendPoint并resume
                // finished has been set, which means that the external has been suspended, here you
                // need to get spsendPoint and resume
                suspendPoint.wait({});
                oneapi::tbb::task::resume(suspendPoint.load());
            }
        }(std::forward<Task>(task), result, finished, suspendPoint);
        waitTask.start();

        if (!finished.test_and_set())
        {
            // finished第一次被设置,说明task还在执行中,suspend并等待task来执行resume
            // finished is set for the first time, indicating that the task is still being executed,
            // suspending and waiting for the task to execute resume
            oneapi::tbb::task::suspend([&](oneapi::tbb::task::suspend_point tag) {
                suspendPoint.store(tag);
                suspendPoint.notify_one();
            });
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

}  // namespace bcos::task::tbb