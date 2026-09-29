#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>

extern "C" {
__attribute__((import_module("pxa.core.v1"), import_name("pxa_submit")))
std::int32_t pxa_submit(const std::uint8_t* data, std::uint32_t length);
__attribute__((import_module("pxa.core.v1"), import_name("pxa_io")))
std::int32_t pxa_io(std::uint64_t handle, std::uint32_t operation,
                    std::uint8_t* data, std::uint32_t length);
}

namespace pxa {

enum class Error : std::int32_t {
    invalid_argument = -1, bad_state = -2, unsupported = -3,
    denied = -4, not_found = -5, busy = -6, would_block = -7,
    quota_exceeded = -8, resource_limit = -9, cancelled = -10,
    internal = -11, timed_out = -12, unavailable = -13,
    io_error = -14, protocol_error = -15, limit_exceeded = -16
};

template<class T> using Result = std::expected<T, Error>;

namespace wire {
constexpr std::size_t header_bytes = 20;
constexpr std::size_t max_control_bytes = 4096;

constexpr void put16(std::byte* p, std::uint16_t value) noexcept {
    p[0] = std::byte(value);
    p[1] = std::byte(value >> 8);
}
constexpr void put32(std::byte* p, std::uint32_t value) noexcept {
    for (unsigned i = 0; i < 4; ++i) p[i] = std::byte(value >> (8 * i));
}
constexpr void put64(std::byte* p, std::uint64_t value) noexcept {
    for (unsigned i = 0; i < 8; ++i) p[i] = std::byte(value >> (8 * i));
}
constexpr std::uint16_t get16(const std::byte* p) noexcept {
    return std::uint16_t(std::to_integer<unsigned>(p[0])) |
           (std::uint16_t(std::to_integer<unsigned>(p[1])) << 8);
}
constexpr std::uint32_t get32(const std::byte* p) noexcept {
    std::uint32_t value = 0;
    for (unsigned i = 0; i < 4; ++i)
        value |= std::uint32_t(std::to_integer<unsigned>(p[i])) << (8 * i);
    return value;
}
constexpr std::uint64_t get64(const std::byte* p) noexcept {
    std::uint64_t value = 0;
    for (unsigned i = 0; i < 8; ++i)
        value |= std::uint64_t(std::to_integer<unsigned>(p[i])) << (8 * i);
    return value;
}

class Writer {
public:
    explicit constexpr Writer(std::span<std::byte> output) noexcept
        : output_(output) {}

    constexpr bool bytes(std::span<const std::byte> input) noexcept {
        if (input.size() > output_.size() - length_) return false;
        for (std::size_t i = 0; i < input.size(); ++i)
            output_[length_ + i] = input[i];
        length_ += input.size();
        return true;
    }
    constexpr bool u8(std::uint8_t value) noexcept {
        if (remaining() < 1) return false;
        output_[length_++] = std::byte(value);
        return true;
    }
    constexpr bool u16(std::uint16_t value) noexcept {
        if (remaining() < 2) return false;
        put16(output_.data() + length_, value);
        length_ += 2;
        return true;
    }
    constexpr bool u32(std::uint32_t value) noexcept {
        if (remaining() < 4) return false;
        put32(output_.data() + length_, value);
        length_ += 4;
        return true;
    }
    constexpr bool u64(std::uint64_t value) noexcept {
        if (remaining() < 8) return false;
        put64(output_.data() + length_, value);
        length_ += 8;
        return true;
    }
    constexpr std::size_t size() const noexcept { return length_; }
    constexpr std::size_t remaining() const noexcept {
        return output_.size() - length_;
    }
private:
    std::span<std::byte> output_;
    std::size_t length_ = 0;
};
} // namespace wire

struct Event {
    std::uint16_t service;
    std::uint16_t opcode;
    std::uint64_t token;
    std::span<const std::byte> payload;
};

Result<Event> parse_event(std::span<const std::byte> bytes) noexcept;

enum class Phase : std::uint8_t { inactive, start, event, stopped };

class Transport {
public:
    Result<void> scratch(std::span<std::byte> buffer) noexcept;

    void phase(Phase value) noexcept { phase_ = value; }
    Phase phase() const noexcept { return phase_; }

    std::uint64_t next_token() noexcept;

    std::uint32_t next_ui_generation() noexcept {
        return ui_generation_ == UINT32_MAX ? 0 : ++ui_generation_;
    }

    Result<void> send(std::uint16_t service, std::uint16_t opcode,
                      std::uint64_t token,
                      std::span<const std::byte> payload = {}) noexcept;

    Result<void> send_prebuilt(std::uint16_t service, std::uint16_t opcode,
                               std::uint64_t token,
                               std::span<std::byte> packet) noexcept;

    Result<std::uint32_t> io(std::uint64_t handle, std::uint32_t operation,
                             std::span<std::byte> buffer) noexcept;

    Result<void> close(std::uint64_t handle) noexcept;

private:
    std::array<std::byte, 512> local_packet_{};
    std::span<std::byte> packet_ = local_packet_;
    std::uint64_t token_ = 0;
    std::uint32_t ui_generation_ = 0;
    Phase phase_ = Phase::inactive;
};

template<class Tag>
class Resource {
public:
    Resource() = default;
    Resource(Transport& transport, std::uint64_t handle) noexcept
        : transport_(&transport), handle_(handle) {}
    Resource(const Resource&) = delete;
    Resource& operator=(const Resource&) = delete;
    Resource(Resource&& other) noexcept
        : transport_(other.transport_), handle_(other.release()) {}
    Resource& operator=(Resource&& other) noexcept {
        if (this != &other) {
            reset();
            transport_ = other.transport_;
            handle_ = other.release();
        }
        return *this;
    }
    ~Resource() { reset(); }

    std::uint64_t handle() const noexcept { return handle_; }
    explicit operator bool() const noexcept { return handle_ != 0; }
    std::uint64_t release() noexcept {
        auto handle = handle_;
        handle_ = 0;
        return handle;
    }
    void reset() noexcept {
        if (handle_ && transport_)
            (void)transport_->close(handle_);
        handle_ = 0;
    }
private:
    Transport* transport_ = nullptr;
    std::uint64_t handle_ = 0;
};

} // namespace pxa
