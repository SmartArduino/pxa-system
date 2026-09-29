#pragma once

#include "core.hpp"

#include <coroutine>
#include <cstddef>
#include <cstdio>
#include <new>
#include <optional>
#include <utility>

namespace pxa {

#ifndef PXA_COROUTINE_SLOT_BYTES
#define PXA_COROUTINE_SLOT_BYTES 1024
#endif
#ifndef PXA_COROUTINE_SLOT_COUNT
#define PXA_COROUTINE_SLOT_COUNT 8
#endif

namespace detail {
class FramePool {
    static constexpr std::size_t slot_bytes = PXA_COROUTINE_SLOT_BYTES;
    static constexpr std::size_t slot_count = PXA_COROUTINE_SLOT_COUNT;
    static_assert(slot_bytes >= 256 && slot_count > 0);
    struct alignas(std::max_align_t) Slot {
        std::byte bytes[slot_bytes];
        bool used;
    };
public:
    static void* allocate(std::size_t size) noexcept {
        if (size > slot_bytes) return nullptr;
        for (auto& slot : slots_) {
            if (slot.used) continue;
            slot.used = true;
            return slot.bytes;
        }
        return nullptr;
    }
    static void release(void* pointer) noexcept {
        for (auto& slot : slots_) {
            if (slot.bytes != pointer) continue;
            slot.used = false;
            return;
        }
    }
private:
    inline static Slot slots_[slot_count]{};
};
} // namespace detail

template<class T>
class Task {
public:
    struct promise_type {
        std::optional<Result<T>> result;
        std::coroutine_handle<> continuation{};

        static void* operator new(std::size_t size) noexcept {
            return detail::FramePool::allocate(size);
        }
        static void operator delete(void* pointer) noexcept {
            detail::FramePool::release(pointer);
        }
        static void operator delete(void* pointer, std::size_t) noexcept {
            detail::FramePool::release(pointer);
        }
        static Task get_return_object_on_allocation_failure() noexcept {
            return Task(std::unexpected(Error::resource_limit));
        }
        Task get_return_object() noexcept {
            return Task(std::coroutine_handle<promise_type>::from_promise(*this));
        }
        std::suspend_always initial_suspend() noexcept { return {}; }
        struct Final {
            bool await_ready() const noexcept { return false; }
            std::coroutine_handle<> await_suspend(
                std::coroutine_handle<promise_type> handle) const
                noexcept {
                auto continuation = handle.promise().continuation;
                return continuation ? continuation : std::noop_coroutine();
            }
            void await_resume() const noexcept {}
        };
        Final final_suspend() noexcept { return {}; }
        void unhandled_exception() noexcept {
            result.emplace(std::unexpected(Error::internal));
        }
        void return_value(Result<T> value) noexcept {
            result.emplace(std::move(value));
        }
        void return_value(std::unexpected<Error> error) noexcept {
            result.emplace(std::move(error));
        }
        template<class U>
            requires (!std::same_as<std::remove_cvref_t<U>, Result<T>> &&
                      !std::same_as<std::remove_cvref_t<U>,
                                    std::unexpected<Error>> &&
                      std::constructible_from<T, U>)
        void return_value(U&& value) noexcept {
            result.emplace(std::in_place, std::forward<U>(value));
        }
    };
    using handle_type = std::coroutine_handle<promise_type>;

    Task(const Task&) = delete;
    Task& operator=(const Task&) = delete;
    Task(Task&& other) noexcept
        : handle_(std::exchange(other.handle_, {})),
          failed_(other.failed_) {}
    Task& operator=(Task&& other) noexcept {
        if (this != &other) {
            if (handle_) handle_.destroy();
            handle_ = std::exchange(other.handle_, {});
            failed_ = other.failed_;
        }
        return *this;
    }
    ~Task() { if (handle_) handle_.destroy(); }

    static Task failed(Error error) noexcept { return Task(std::unexpected(error)); }

    bool valid() const noexcept { return static_cast<bool>(handle_); }
    Error failure() const noexcept { return failed_; }
    handle_type release() noexcept { return std::exchange(handle_, {}); }

    struct Awaiter {
        Task task;
        bool await_ready() const noexcept {
            return !task.handle_ || task.handle_.done();
        }
        std::coroutine_handle<> await_suspend(
            std::coroutine_handle<> continuation) noexcept {
            task.handle_.promise().continuation = continuation;
            return task.handle_;
        }
        Result<T> await_resume() noexcept {
            if (!task.handle_) return std::unexpected(task.failed_);
            auto& result = task.handle_.promise().result;
            if (!result) return std::unexpected(Error::bad_state);
            return std::move(*result);
        }
    };
    Awaiter operator co_await() && noexcept {
        return Awaiter{std::move(*this)};
    }

private:
    explicit Task(handle_type handle) noexcept : handle_(handle) {}
    explicit Task(std::unexpected<Error> failure) noexcept
        : failed_(failure.error()) {}
    handle_type handle_{};
    Error failed_ = Error::bad_state;
};

class RequestTable {
    struct Entry {
        std::uint64_t token = 0;
        void* context = nullptr;
        void (*complete)(void*, const Event&) = nullptr;
        Transport* transport = nullptr;
        std::uint16_t service = 0;
        std::uint16_t opcode = 0;
        bool close_late_handle = false;
        std::uint16_t late_handle_offset = 4;
    };
public:
    Result<void> add(std::uint64_t token, void* context,
                     void (*complete)(void*, const Event&)) noexcept {
        if (!token || !context || !complete)
            return std::unexpected(Error::invalid_argument);
        Entry* free = nullptr;
        for (auto& entry : entries_) {
            if (entry.token == token)
                return std::unexpected(Error::bad_state);
            if (!entry.token && !free) free = &entry;
        }
        if (!free) return std::unexpected(Error::resource_limit);
        *free = {token, context, complete};
        return {};
    }
    void remove(std::uint64_t token) noexcept {
        for (auto& entry : entries_)
            if (entry.token == token) { entry = {}; return; }
    }
    void abandon(std::uint64_t token, Transport& transport,
                 std::uint16_t service, std::uint16_t opcode,
                 bool close_late_handle,
                 std::uint16_t late_handle_offset = 4) noexcept {
        for (auto& entry : entries_) {
            if (entry.token != token) continue;
            entry.context = nullptr;
            entry.complete = nullptr;
            entry.transport = &transport;
            entry.service = service;
            entry.opcode = opcode;
            entry.close_late_handle = close_late_handle;
            entry.late_handle_offset = late_handle_offset;
            return;
        }
    }
    bool dispatch(const Event& event) noexcept {
        if (!event.token) return false;
        for (auto& entry : entries_) {
            if (entry.token != event.token) continue;
            auto selected = entry;
            entry = {};
            if (selected.complete) {
                selected.complete(selected.context, event);
            } else if (selected.close_late_handle &&
                       event.service == selected.service &&
                       event.opcode == selected.opcode &&
                       event.payload.size() >=
                           static_cast<std::size_t>(selected.late_handle_offset) + 8 &&
                       wire::get32(event.payload.data()) == 0) {
                auto handle = wire::get64(event.payload.data() +
                                           selected.late_handle_offset);
                if (handle) (void)selected.transport->close(handle);
            }
            return true;
        }
        return false;
    }
    void clear() noexcept { entries_ = {}; }
private:
    std::array<Entry, 16> entries_{};
};

class Response {
public:
    struct PrebuiltPacket {
        explicit PrebuiltPacket(std::span<std::byte> packet) noexcept
            : bytes(packet) {}
        std::span<std::byte> bytes;
    };

    Response(Transport& transport, RequestTable& requests,
             std::uint16_t service, std::uint16_t opcode,
             std::span<const std::byte> payload,
             bool close_late_handle = false,
             std::uint16_t late_handle_offset = 4) noexcept
        : transport_(transport), requests_(requests), service_(service),
          opcode_(opcode), payload_(payload),
          close_late_handle_(close_late_handle),
          late_handle_offset_(late_handle_offset) {}
    Response(Transport& transport, RequestTable& requests,
             std::uint16_t service, std::uint16_t opcode,
             PrebuiltPacket packet, bool close_late_handle = false,
             std::uint16_t late_handle_offset = 4) noexcept
        : transport_(transport), requests_(requests), service_(service),
          opcode_(opcode), packet_(packet.bytes),
          close_late_handle_(close_late_handle),
          late_handle_offset_(late_handle_offset) {}
    Response(const Response&) = delete;
    Response& operator=(const Response&) = delete;
    ~Response() {
        if (!registered_) return;
        if (transport_.phase() == Phase::stopped) {
            requests_.remove(token_);
            return;
        }
        requests_.abandon(token_, transport_, service_, opcode_,
                          close_late_handle_, late_handle_offset_);
        std::array<std::byte, 8> payload{};
        wire::put64(payload.data(), token_);
        (void)transport_.send(1, 1, 0, payload);
    }
    bool await_ready() const noexcept { return false; }
    bool await_suspend(std::coroutine_handle<> suspended) noexcept {
        suspended_ = suspended;
        token_ = transport_.next_token();
        auto added = requests_.add(token_, this, [](void* pointer,
                                                   const Event& event) {
            static_cast<Response*>(pointer)->complete(event);
        });
        if (!added) { error_ = added.error(); return false; }
        registered_ = true;
        auto sent = packet_.empty()
            ? transport_.send(service_, opcode_, token_, payload_)
            : transport_.send_prebuilt(service_, opcode_, token_, packet_);
        if (!sent) {
            error_ = sent.error();
            requests_.remove(token_);
            registered_ = false;
            return false;
        }
        return true;
    }
    Result<Event> await_resume() const noexcept {
        if (error_) return std::unexpected(*error_);
        if (!event_) return std::unexpected(Error::bad_state);
        return *event_;
    }
private:
    void complete(const Event& event) noexcept {
        registered_ = false;
        if (event.service != service_ || event.opcode != opcode_)
            error_ = Error::protocol_error;
        else
            event_ = event;
        suspended_.resume();
    }
    Transport& transport_;
    RequestTable& requests_;
    std::uint16_t service_;
    std::uint16_t opcode_;
    std::span<const std::byte> payload_;
    std::span<std::byte> packet_;
    std::coroutine_handle<> suspended_{};
    std::uint64_t token_ = 0;
    std::optional<Event> event_;
    std::optional<Error> error_;
    bool registered_ = false;
    bool close_late_handle_ = false;
    std::uint16_t late_handle_offset_ = 4;
};

class TaskScope {
    struct Entry {
        std::coroutine_handle<> handle{};
        void (*destroy)(std::coroutine_handle<>) = nullptr;
        std::optional<Error> (*failure)(std::coroutine_handle<>) = nullptr;
    };
public:
    TaskScope() = default;
    TaskScope(const TaskScope&) = delete;
    TaskScope& operator=(const TaskScope&) = delete;
    ~TaskScope() { cancel(); }

    void on_error(void* context,
                  void (*report)(void*, Error) noexcept) noexcept {
        error_context_ = context;
        error_report_ = report;
    }

    template<class T> Result<void> start(Task<T> task) noexcept {
        if (!task.valid()) return std::unexpected(task.failure());
        Entry* available = nullptr;
        for (auto& entry : entries_)
            if (!entry.handle) { available = &entry; break; }
        if (!available) return std::unexpected(Error::resource_limit);
        auto handle = task.release();
        *available = {
            handle,
            [](std::coroutine_handle<> generic) {
                Task<T>::handle_type::from_address(generic.address()).destroy();
            },
            [](std::coroutine_handle<> generic) -> std::optional<Error> {
                auto typed = Task<T>::handle_type::from_address(
                    generic.address());
                const auto& result = typed.promise().result;
                if (!result) return Error::bad_state;
                if (!*result) return result->error();
                return std::nullopt;
            }};
        handle.resume();
        reap();
        return {};
    }
    void reap() noexcept {
        for (auto& entry : entries_) {
            if (!entry.handle || !entry.handle.done()) continue;
            auto completed = entry;
            entry = {};
            auto failure = completed.failure(completed.handle);
            completed.destroy(completed.handle);
            if (!failure) continue;
            if (error_report_) error_report_(error_context_, *failure);
            else std::fprintf(stderr, "PXA task failed: %d\n",
                              static_cast<int>(*failure));
        }
    }
    void cancel() noexcept {
        for (auto& entry : entries_) {
            if (!entry.handle) continue;
            auto cancelled = entry;
            entry = {};
            cancelled.destroy(cancelled.handle);
        }
    }
private:
    std::array<Entry, 16> entries_{};
    void* error_context_ = nullptr;
    void (*error_report_)(void*, Error) noexcept = nullptr;
};

} // namespace pxa
