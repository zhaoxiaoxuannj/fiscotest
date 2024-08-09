#include "bcos-task/Generator.h"
#include "bcos-utilities/Overloaded.h"
#include <bcos-task/Task.h>
#include <bcos-task/Wait.h>
#include <oneapi/tbb/blocked_range.h>
#include <oneapi/tbb/task_arena.h>
#include <oneapi/tbb/task_group.h>
#include <tbb/concurrent_vector.h>
#include <tbb/parallel_for.h>
#include <tbb/task.h>
#include <tbb/task_group.h>
#include <boost/test/unit_test.hpp>
#include <boost/throw_exception.hpp>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>

using namespace bcos::task;

struct TaskFixture
{
    oneapi::tbb::task_group taskGroup;
};

BOOST_FIXTURE_TEST_SUITE(TaskTest, TaskFixture)

Task<void> nothingTask()
{
    BOOST_FAIL("No expect to run!");
    co_return;
}

Task<int> level3()
{
    std::cout << "Level3 execute finished" << std::endl;
    co_return 100;
}

Task<long> level2()
{
    auto numResult = co_await level3();
    BOOST_CHECK_EQUAL(numResult, 100);

    constexpr static auto mut = 100L;

    std::cout << "Level2 execute finished" << std::endl;
    co_return static_cast<long>(numResult) * mut;
}

Task<void> level1()
{
    auto num1 = co_await level3();
    auto num2 = co_await level2();

    BOOST_CHECK_EQUAL(num1, 100);
    BOOST_CHECK_EQUAL(num2, 10000);

    std::cout << "Level1 execute finished" << std::endl;
    co_return;
}

void innerThrow()
{
    BOOST_THROW_EXCEPTION(std::runtime_error("error11"));
}

BOOST_AUTO_TEST_CASE(taskException)
{
    BOOST_CHECK_THROW(bcos::task::wait([]() -> bcos::task::Task<void> {
        innerThrow();
        co_return;
    }()),
        std::runtime_error);
}

BOOST_AUTO_TEST_CASE(normalTask)
{
    bool finished = false;

    bcos::task::wait([](bool& finished) -> Task<void> {
        co_await level1();
        std::cout << "Callback called!" << std::endl;
        finished = true;

        co_return;
    }(finished));
    BOOST_CHECK_EQUAL(finished, true);

    auto num = bcos::task::syncWait(level2());
    BOOST_CHECK_EQUAL(num, 10000);
}

Task<int> asyncLevel2(oneapi::tbb::task_group& taskGroup)
{
    struct Awaitable
    {
        constexpr bool await_ready() const { return false; }

        void await_suspend(CO_STD::coroutine_handle<> handle)
        {
            std::cout << "Start run async thread: " << handle.address() << std::endl;
            taskGroup.run([this, m_handle = std::move(handle)]() {
                std::this_thread::sleep_for(std::chrono::seconds(2));
                num = 100;

                std::cout << "Call m_handle.resume(): " << m_handle.address() << std::endl;
                auto handle = const_cast<decltype(m_handle)&>(m_handle);
                handle.resume();
            });
        }

        int await_resume() const
        {
            std::cout << "Call await_resume()" << std::endl;
            return num;
        }

        oneapi::tbb::task_group& taskGroup;
        int num = 0;
    };

    std::cout << "co_await Awaitable started" << std::endl;
    auto num = co_await Awaitable{taskGroup, 0};
    std::cout << "co_await Awaitable ended" << std::endl;

    BOOST_CHECK_EQUAL(num, 100);

    std::cout << "asyncLevel2 co_return" << std::endl;
    co_return num;
}

Task<int> asyncLevel1(oneapi::tbb::task_group& taskGroup)
{
    std::cout << "co_await asyncLevel2 started" << std::endl;
    auto num1 = co_await asyncLevel2(taskGroup);
    std::cout << "co_await asyncLevel2 ended" << std::endl;

    BOOST_CHECK_EQUAL(num1, 100);

    std::cout << "AsyncLevel1 execute finished" << std::endl;
    co_return num1 * 2;
}

BOOST_AUTO_TEST_CASE(asyncTask)
{
    auto num = bcos::task::syncWait(asyncLevel1(taskGroup));
    BOOST_CHECK_EQUAL(num, 200);

    bcos::task::wait([](decltype(taskGroup)& taskGroup) -> Task<void> {
        auto result = co_await asyncLevel1(taskGroup);

        BOOST_CHECK_EQUAL(result, 200);
        std::cout << "Got async result" << std::endl;
        co_return;
    }(taskGroup));

    std::cout << "Top task destroyed" << std::endl;

    taskGroup.wait();
    std::cout << "asyncTask test over" << std::endl;
}

bcos::task::Task<int&> returnIntReference(int& num)
{
    co_return num;
}

BOOST_AUTO_TEST_CASE(referenceTask)
{
    int topNumber = 10;
    bcos::task::syncWait([&topNumber](int& number) -> bcos::task::Task<void> {
        auto& result = co_await returnIntReference(number);
        static_assert(std::is_reference_v<decltype(result)>);

        BOOST_CHECK_EQUAL(std::addressof(result), std::addressof(topNumber));
    }(topNumber));

    using Type = AwaitableReturnType<bcos::task::Task<int&>>;
    static_assert(std::is_same_v<Type, int&>);

    auto& result2 = bcos::task::syncWait(returnIntReference(topNumber));
    BOOST_CHECK_EQUAL(std::addressof(result2), std::addressof(topNumber));
}

struct SleepTask
{
    inline static oneapi::tbb::concurrent_vector<std::future<void>> futures;

    constexpr bool await_ready() const { return false; }
    void await_suspend(CO_STD::coroutine_handle<> handle)
    {
        futures.emplace_back(std::async([m_handle = handle]() mutable {
            using namespace std::chrono_literals;
            std::this_thread::sleep_for(1s);
            m_handle.resume();
        }));
    }
    constexpr void await_resume() const {}
};

bcos::task::Generator<int> genInt()
{
    co_yield 1;
    co_yield 2;
    co_yield 3;
}

BOOST_AUTO_TEST_CASE(generator)
{
    int j = 0;
    for (auto i : genInt())
    {
        BOOST_CHECK_EQUAL(i, ++j);
        std::cout << i << std::endl;
    }
    std::cout << "All outputed" << std::endl;
}

BOOST_AUTO_TEST_SUITE_END()