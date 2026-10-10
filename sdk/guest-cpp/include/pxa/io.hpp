#pragma once

#include "core.hpp"

#include <algorithm>
#include <concepts>
#include <functional>
#include <utility>

namespace pxa {

enum class TransferState : std::uint8_t { progress, complete, blocked, end };
struct TransferStep {
    std::size_t bytes = 0; // Transferred by this step, not the cumulative count.
    TransferState state = TransferState::progress;
};

// An optional, allocation-free cursor over caller-owned storage. Each step
// performs at most one synchronous I/O call. The caller schedules retries;
// neither would_block nor a zero-byte write triggers a spin/retry loop.
template<bool Reading>
class Transfer {
    using Byte = std::conditional_t<Reading, std::byte, const std::byte>;
public:
    explicit constexpr Transfer(std::span<Byte> buffer) noexcept : buffer_(buffer) {}
    Transfer(const Transfer&) = delete;
    Transfer& operator=(const Transfer&) = delete;
    // One cursor owns progress. Moving invalidates the old borrowed view.
    constexpr Transfer(Transfer&& other) noexcept
        : buffer_(std::exchange(other.buffer_, {})),
          position_(std::exchange(other.position_, 0)),
          ended_(std::exchange(other.ended_, false)) {}
    constexpr Transfer& operator=(Transfer&& other) noexcept {
        if (this != &other) {
            buffer_ = std::exchange(other.buffer_, {});
            position_ = std::exchange(other.position_, 0);
            ended_ = std::exchange(other.ended_, false);
        }
        return *this;
    }
    constexpr std::size_t transferred() const noexcept { return position_; }
    constexpr std::size_t remaining() const noexcept { return buffer_.size() - position_; }
    constexpr bool complete() const noexcept { return remaining() == 0; }
    constexpr bool ended() const noexcept { return ended_; }
    constexpr std::span<Byte> completed_bytes() const noexcept { return buffer_.first(position_); }

    template<class Operation>
        requires requires(Operation&& operation, std::span<Byte> bytes) {
            { std::invoke(std::forward<Operation>(operation), bytes) } ->
                std::same_as<Result<std::uint32_t>>;
        }
    Result<TransferStep> step_with(Operation&& operation,
                                  std::size_t max_bytes = UINT32_MAX) {
        if (complete()) return TransferStep{0, TransferState::complete};
        if (ended_) return TransferStep{0, TransferState::end};
        if (!max_bytes) return std::unexpected(Error::invalid_argument);
        const auto size = std::min({remaining(), max_bytes, std::size_t{UINT32_MAX}});
        auto result = std::invoke(std::forward<Operation>(operation),
                                  buffer_.subspan(position_, size));
        if (!result) {
            if (result.error() == Error::would_block)
                return TransferStep{0, TransferState::blocked};
            return std::unexpected(result.error());
        }
        if (*result > size) return std::unexpected(Error::protocol_error);
        if (!*result) {
            if constexpr (Reading) {
                ended_ = true;
                return TransferStep{0, TransferState::end};
            } else return TransferStep{0, TransferState::blocked};
        }
        position_ += *result;
        return TransferStep{*result, complete() ? TransferState::complete
                                               : TransferState::progress};
    }

    template<class Stream>
        requires (Reading && requires(Stream& stream, std::span<std::byte> bytes) {
            { stream.read(bytes) } -> std::same_as<Result<std::uint32_t>>;
        }) || (!Reading && requires(Stream& stream, std::span<const std::byte> bytes) {
            { stream.write(bytes) } -> std::same_as<Result<std::uint32_t>>;
        })
    Result<TransferStep> step(Stream& stream, std::size_t max_bytes = UINT32_MAX) {
        return step_with([&](std::span<Byte> bytes) {
            if constexpr (Reading) return stream.read(bytes);
            else return stream.write(bytes);
        }, max_bytes);
    }
private:
    std::span<Byte> buffer_;
    std::size_t position_ = 0;
    bool ended_ = false;
};

using ReadTransfer = Transfer<true>;
using WriteTransfer = Transfer<false>;

} // namespace pxa
