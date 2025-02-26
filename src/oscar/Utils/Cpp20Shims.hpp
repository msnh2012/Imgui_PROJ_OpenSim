#pragma once

#include <atomic>
#include <cstddef>
#include <memory>
#include <thread>
#include <type_traits>

// shims: *roughly* compatible shims to features available in newer C++es
namespace osc {

    // C++20: std::stop_token
    class stop_token final {
    public:
        stop_token(std::shared_ptr<std::atomic<bool>> st) : m_SharedState{std::move(st)}
        {
        }
        stop_token(stop_token const&) = delete;
        stop_token(stop_token&& tmp) noexcept : m_SharedState{tmp.m_SharedState}
        {
        }
        stop_token& operator=(stop_token const&) = delete;
        stop_token& operator=(stop_token&&) noexcept = delete;
        ~stop_token() noexcept = default;

        bool stop_requested() const noexcept
        {
            return *m_SharedState;
        }

    private:
        std::shared_ptr<std::atomic<bool>> m_SharedState;
    };

    // C++20: std::stop_source
    class stop_source final {
    public:
        stop_source() : m_SharedState{new std::atomic<bool>{false}}
        {
        }
        stop_source(stop_source const&) = delete;
        stop_source(stop_source&& tmp) noexcept : m_SharedState{std::move(tmp.m_SharedState)}
        {
        }
        stop_source& operator=(stop_source const&) = delete;
        stop_source& operator=(stop_source&& tmp) noexcept
        {
            m_SharedState = std::move(tmp.m_SharedState);
            return *this;
        }
        ~stop_source() = default;

        bool request_stop() noexcept
        {
            // as-per the C++20 spec, but always true for this impl.
            bool has_stop_state = m_SharedState != nullptr;
            bool already_stopped = m_SharedState->exchange(true);

            return has_stop_state && !already_stopped;
        }

        stop_token get_token() const noexcept
        {
            return stop_token{m_SharedState};
        }

    private:
        std::shared_ptr<std::atomic<bool>> m_SharedState;
    };

    // C++20: std::jthread
    class jthread final {
    public:
        // Creates new thread object which does not represent a thread
        jthread() :
            m_StopSource{},
            m_Thread{}
        {
        }

        // Creates new thread object and associates it with a thread of execution.
        // The new thread of execution immediately starts executing
        template<typename Function, typename... Args>
        jthread(Function&& f, Args&&... args) :
            m_StopSource{},
            m_Thread{std::forward<Function>(f), m_StopSource.get_token(), std::forward<Args>(args)...}
        {
        }

        // threads are non-copyable
        jthread(jthread const&) = delete;
        jthread& operator=(jthread const&) = delete;

        // threads are moveable: the moved-from value is a non-joinable thread that
        // does not represent a thread
        jthread(jthread&&) noexcept = default;

        jthread& operator=(jthread&& other) noexcept
        {
            if (joinable())
            {
                m_StopSource.request_stop();
                m_Thread.join();
            }
            std::swap(m_StopSource, other.m_StopSource);
            std::swap(m_Thread, other.m_Thread);
            return *this;
        }

        // jthreads (or "joining threads") cancel + join on destruction
        ~jthread() noexcept
        {
            if (joinable())
            {
                m_StopSource.request_stop();
                m_Thread.join();
            }
        }

        bool joinable() const noexcept
        {
            return m_Thread.joinable();
        }

        bool request_stop() noexcept
        {
            return m_StopSource.request_stop();
        }

        void join()
        {
            return m_Thread.join();
        }

    private:
        stop_source m_StopSource;
        std::thread m_Thread;
    };

    // C++20: <numbers>
    namespace numbers {
        template<typename T> inline constexpr T pi_v = static_cast<T>(3.14159265358979323846);
        inline constexpr double pi = pi_v<double>;
    }

    // C++20: ssize
    template<typename Container>
    constexpr auto ssize(Container const& c) ->
        std::common_type_t<std::ptrdiff_t, std::make_signed_t<decltype(c.size())>>
    {
        using R = std::common_type_t<std::ptrdiff_t, std::make_signed_t<decltype(c.size())>>;
        return static_cast<R>(c.size());
    }

    // C++20: ssize
    template<typename T, std::ptrdiff_t N>
    constexpr std::ptrdiff_t ssize(const T (&)[N]) noexcept
    {
        return N;
    }
}
