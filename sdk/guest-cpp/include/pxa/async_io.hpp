#pragma once

#include "task.hpp"
#include "service_wire.hpp"

namespace pxa {

// One operation embedded in its caller's coroutine frame. It allocates no
// coroutine, buffer or subscription. Readiness is advisory: retry and rearm
// after a spurious wake. Cancellation only cancels this wait, not the stream.
class [[nodiscard]] AsyncIo {
  public:
    AsyncIo(Transport &transport, RequestTable &requests, std::uint64_t handle,
            std::uint32_t operation, std::uint32_t interests,
            std::span<std::byte> buffer = {}) noexcept
        : AsyncIo(&transport, &requests, handle, operation, interests, buffer) {}
    AsyncIo(Transport *transport, RequestTable *requests, std::uint64_t handle,
            std::uint32_t operation, std::uint32_t interests,
            std::span<std::byte> buffer = {}) noexcept
        : transport_(transport), requests_(requests), handle_(handle),
          operation_(operation), interests_(interests), buffer_(buffer) {
        wire::put64(packet_.data(), handle);
        wire::put32(packet_.data() + 8, interests);
    }
    AsyncIo(const AsyncIo &) = delete;
    AsyncIo &operator=(const AsyncIo &) = delete;
    ~AsyncIo() {
        if (!registered_)
            return;
        if (transport_->phase() == Phase::stopped) {
            requests_->remove(token_, 1, 4);
        } else {
            requests_->abandon(token_, *transport_, 1, 4, false);
            std::array<std::byte, 8> cancel;
            wire::put64(cancel.data(), token_);
            (void)transport_->send(1, 1, 0, cancel);
        }
    }
    bool await_ready() noexcept {
        return attempt();
    }
    bool await_suspend(std::coroutine_handle<> continuation) noexcept {
        continuation_ = continuation;
        return arm();
    }
    Result<std::uint32_t> await_resume() noexcept {
        return result_;
    }

  private:
    bool attempt() noexcept {
        if (!transport_ || !requests_ || !handle_) {
            result_ = std::unexpected(Error::bad_state);
            return true;
        }
        result_ = transport_->io(handle_, operation_, buffer_);
        if (result_ && (operation_ == 1 || operation_ == 2) &&
            *result_ > buffer_.size())
            result_ = std::unexpected(Error::protocol_error);
        return result_ || result_.error() != Error::would_block;
    }
    bool arm() noexcept {
        token_ = transport_->next_token();
        auto added = requests_->add(
            token_, this,
            [](void *self, const Event &event) {
                static_cast<AsyncIo *>(self)->complete(event);
            },
            1, 4);
        if (!added) {
            result_ = std::unexpected(added.error());
            return false;
        }
        registered_ = true;
        auto sent = transport_->send(1, 4, token_, packet_);
        if (!sent) {
            requests_->remove(token_, 1, 4);
            registered_ = false;
            result_ = std::unexpected(sent.error());
            return false;
        }
        return true;
    }
    void complete(const Event &event) noexcept {
        registered_ = false;
        auto body = wire::result_body(event.payload);
        if (event.service != 1 || event.opcode != 4)
            result_ = std::unexpected(Error::protocol_error);
        else if (!body)
            result_ = std::unexpected(body.error());
        else if (body->size() != 4 ||
                 !(wire::get32(body->data()) & (interests_ | 8u)) ||
                 (wire::get32(body->data()) & ~15u))
            result_ = std::unexpected(Error::protocol_error);
        else if (!attempt() && arm())
            return;
        continuation_.resume();
    }
    Transport *transport_;
    RequestTable *requests_;
    std::uint64_t handle_, token_ = 0;
    std::uint32_t operation_, interests_;
    std::span<std::byte> buffer_;
    std::array<std::byte, 12> packet_;
    std::coroutine_handle<> continuation_{};
    Result<std::uint32_t> result_ = std::unexpected(Error::bad_state);
    bool registered_ = false;
};

} // namespace pxa
