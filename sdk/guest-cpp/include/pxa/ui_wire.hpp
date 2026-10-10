#pragma once

#include "core.hpp"

#include <array>
#include <optional>
#include <string_view>

namespace pxa::ui {

namespace protocol {
constexpr std::uint16_t service = 3;
constexpr std::uint16_t begin = 1;
constexpr std::uint16_t write = 2;
constexpr std::uint16_t commit = 3;
constexpr std::uint16_t cancel = 4;
constexpr std::uint8_t patch = 1;
constexpr std::uint8_t replace_surface = 3;
constexpr std::uint8_t create = 1;
constexpr std::uint8_t set_property = 2;
constexpr std::uint8_t move = 4;
constexpr std::uint8_t remove = 5;
constexpr std::uint8_t root = 1;
constexpr std::uint8_t box = 2;
constexpr std::uint8_t text = 4;
constexpr std::uint8_t image = 5;
constexpr std::uint8_t control = 6;
constexpr std::uint8_t progress = 7;
constexpr std::uint8_t canvas = 8;
constexpr std::uint8_t virtual_list = 9;
constexpr std::uint8_t button = 1;
constexpr std::uint8_t toggle = 2;
constexpr std::uint8_t slider = 3;
constexpr std::uint8_t text_input = 4;
constexpr std::uint16_t layout = 262;
constexpr std::uint16_t width = 256;
constexpr std::uint16_t height = 257;
constexpr std::uint16_t min_width = 258, max_width = 259;
constexpr std::uint16_t min_height = 260, max_height = 261;
constexpr std::uint16_t justify = 264;
constexpr std::uint16_t align = 265;
constexpr std::uint16_t gap = 267;
constexpr std::uint16_t grow = 269;
constexpr std::uint16_t position = 271;
constexpr std::uint16_t x = 272;
constexpr std::uint16_t y = 273;
constexpr std::uint16_t foreground = 513;
constexpr std::uint16_t background = 514;
constexpr std::uint16_t border_color = 515;
constexpr std::uint16_t radius = 517, border_width = 518;
constexpr std::uint16_t font_role = 519;
constexpr std::uint16_t text_align = 520;
constexpr std::uint16_t composition = 522;
constexpr std::uint8_t alpha_overlay = 1;
constexpr std::uint16_t text_value = 768;
constexpr std::uint16_t text_max_bytes = 782, text_single_line = 783;
constexpr std::uint16_t asset = 770;
constexpr std::uint16_t image_fit = 771;
constexpr std::uint16_t value = 772;
constexpr std::uint16_t min_value = 773;
constexpr std::uint16_t max_value = 774;
constexpr std::uint16_t step = 775;
constexpr std::uint16_t scroll_axis = 776;
constexpr std::uint16_t scrollbar = 777;
constexpr std::uint16_t item_count = 778;
constexpr std::uint16_t item_extent = 779;
constexpr std::uint16_t event_mask = 3;
constexpr std::uint16_t visible = 1;
constexpr std::uint16_t enabled = 2;
/* Event kinds and mask bits mirror pxa_ui.h. */
constexpr std::uint16_t event_pointer_kind = 7;
constexpr std::uint64_t event_mask_pointer = UINT64_C(1) << 6;
constexpr std::uint8_t row = 1;
constexpr std::uint8_t column = 2;
constexpr std::uint8_t stack = 3;
constexpr std::uint8_t theme_primary = 2;
constexpr std::uint8_t theme_on_primary = 3;
constexpr std::uint8_t theme_text = 4;
} // namespace protocol

template<std::size_t PacketBytes = 512>
class Transaction {
    static_assert(PacketBytes >= 48 && PacketBytes <= wire::max_control_bytes);
public:
    Transaction(Transport& transport, std::uint32_t generation,
                std::uint32_t surface, std::uint8_t kind) noexcept
        : transport_(transport), generation_(generation) {
        std::array<std::byte, 20> begin{};
        wire::put32(begin.data(), surface);
        wire::put32(begin.data() + 4, generation);
        wire::put32(begin.data() + 8, generation);
        wire::put32(begin.data() + 12, 0);
        begin[16] = std::byte(kind);
        begin[17] = std::byte{1};
        auto result = transport_.send(protocol::service, protocol::begin,
                                      0, begin);
        if (result) {
            active_ = true;
            wire::put32(buffer_.data(), generation);
            used_ = 4;
        } else {
            error_ = result.error();
        }
    }

    Transaction(const Transaction&) = delete;
    Transaction& operator=(const Transaction&) = delete;
    ~Transaction() { abort(); }

    bool valid() const noexcept { return active_ && !error_.has_value(); }
    Error error() const noexcept { return error_.value_or(Error::bad_state); }
    bool fail(Error error) noexcept { error_ = error; return false; }

    bool create(std::uint32_t id, std::uint32_t parent,
                std::uint8_t type, std::uint8_t subtype = 0) noexcept {
        if (!id || (type != protocol::root && !parent)) {
            error_ = Error::invalid_argument;
            return false;
        }
        std::array<std::byte, 16> data{};
        wire::put32(data.data(), id);
        wire::put32(data.data() + 4, parent);
        data[12] = std::byte(type);
        data[13] = std::byte(subtype);
        return record(protocol::create, {}, data);
    }

    bool property(std::uint32_t id, std::uint16_t key,
                  std::span<const std::byte> value) noexcept {
        if (!id || !key) {
            error_ = Error::invalid_argument;
            return false;
        }
        std::array<std::byte, 6> prefix{};
        wire::put32(prefix.data(), id);
        wire::put16(prefix.data() + 4, key);
        return record(protocol::set_property, prefix, value);
    }

    bool remove(std::uint32_t id) noexcept {
        if (!id) { error_ = Error::invalid_argument; return false; }
        std::array<std::byte, 4> data{};
        wire::put32(data.data(), id);
        return record(protocol::remove, {}, data);
    }

    bool move(std::uint32_t id, std::uint32_t parent,
              std::uint32_t before = 0) noexcept {
        if (!id || !parent) { error_ = Error::invalid_argument; return false; }
        std::array<std::byte, 12> data{};
        wire::put32(data.data(), id);
        wire::put32(data.data() + 4, parent);
        wire::put32(data.data() + 8, before);
        return record(protocol::move, {}, data);
    }

    bool text(std::uint32_t id, std::string_view value) noexcept {
        return property(id, protocol::text_value,
                        {reinterpret_cast<const std::byte*>(value.data()),
                         value.size()});
    }

    bool u8(std::uint32_t id, std::uint16_t key, std::uint8_t value) noexcept {
        std::array<std::byte, 1> data{std::byte(value)};
        return property(id, key, data);
    }

    bool u16(std::uint32_t id, std::uint16_t key,
             std::uint16_t value) noexcept {
        std::array<std::byte, 2> data{};
        wire::put16(data.data(), value);
        return property(id, key, data);
    }

    bool u64(std::uint32_t id, std::uint16_t key,
             std::uint64_t value) noexcept {
        std::array<std::byte, 8> data{};
        wire::put64(data.data(), value);
        return property(id, key, data);
    }

    bool u32(std::uint32_t id, std::uint16_t key,
             std::uint32_t value) noexcept {
        std::array<std::byte, 4> data{};
        wire::put32(data.data(), value);
        return property(id, key, data);
    }

    bool i32(std::uint32_t id, std::uint16_t key,
             std::int32_t value) noexcept {
        return u32(id, key, static_cast<std::uint32_t>(value));
    }

    bool rgba(std::uint32_t id, std::uint16_t key,
              std::uint32_t value) noexcept {
        std::array<std::byte, 8> data{};
        data[0] = std::byte{1};
        wire::put32(data.data() + 4, value);
        return property(id, key, data);
    }

    bool fill(std::uint32_t id, std::uint16_t key) noexcept {
        if (key != protocol::width && key != protocol::height) {
            error_ = Error::invalid_argument;
            return false;
        }
        std::array<std::byte, 8> data{};
        data[0] = std::byte{4};
        return property(id, key, data);
    }

    bool logical_px(std::uint32_t id, std::uint16_t key,
                    std::int32_t value) noexcept {
        if ((key != protocol::width && key != protocol::height &&
             key != protocol::x && key != protocol::y) ||
            value < 0 || value > INT32_MAX / 64) {
            error_ = Error::invalid_argument;
            return false;
        }
        std::array<std::byte, 8> data{};
        data[0] = std::byte{1};
        wire::put32(data.data() + 4,
                    static_cast<std::uint32_t>(value * 64));
        return property(id, key, data);
    }

    // Constraint keys are compiled only when used. Existing runtime-key
    // callers retain their original encoder and generated code.
    template<std::uint16_t Key> requires
        ((Key >= protocol::width && Key <= protocol::max_height) ||
         Key == protocol::x || Key == protocol::y)
    bool logical_px(std::uint32_t id, std::int32_t value) noexcept {
        if (value < 0 || value > INT32_MAX / 64)
            return fail(Error::invalid_argument);
        std::array<std::byte, 8> data{};
        data[0] = std::byte{1};
        wire::put32(data.data() + 4, static_cast<std::uint32_t>(value * 64));
        return property(id, Key, data);
    }

    bool dp(std::uint32_t id, std::uint16_t key,
            std::int32_t value) noexcept {
        if (value < 0 || value > INT32_MAX / 64) {
            error_ = Error::invalid_argument;
            return false;
        }
        std::array<std::byte, 4> data{};
        wire::put32(data.data(), static_cast<std::uint32_t>(value * 64));
        return property(id, key, data);
    }

    Result<void> commit() noexcept {
        if (!valid()) {
            auto failure = error();
            abort();
            return std::unexpected(failure);
        }
        if (!flush()) {
            auto failure = error();
            abort();
            return std::unexpected(failure);
        }
        std::array<std::byte, 4> data{};
        wire::put32(data.data(), generation_);
        auto result = transport_.send(protocol::service, protocol::commit,
                                      0, data);
        active_ = false;
        return result;
    }

    void abort() noexcept {
        if (!active_) return;
        std::array<std::byte, 4> data{};
        wire::put32(data.data(), generation_);
        (void)transport_.send(protocol::service, protocol::cancel, 0, data);
        active_ = false;
    }

private:
    // Share packet encoding across property setters. O3 otherwise duplicates
    // the fragmented append/flush loop at every styled-widget property.
    [[gnu::noinline]] bool record(std::uint8_t command, std::span<const std::byte> prefix,
                std::span<const std::byte> value) noexcept {
        if (!valid()) return false;
        if (prefix.size() + value.size() > UINT16_MAX) {
            error_ = Error::limit_exceeded;
            return false;
        }
        std::array<std::byte, 4> header{};
        header[0] = std::byte(command);
        wire::put16(header.data() + 2,
                    static_cast<std::uint16_t>(prefix.size() + value.size()));
        return append(header) && append(prefix) && append(value);
    }

    bool append(std::span<const std::byte> data) noexcept {
        while (!data.empty()) {
            if (used_ == buffer_.size() && !flush()) return false;
            const auto count = data.size() < buffer_.size() - used_
                                   ? data.size() : buffer_.size() - used_;
            for (std::size_t i = 0; i < count; ++i)
                buffer_[used_ + i] = data[i];
            used_ += count;
            data = data.subspan(count);
        }
        return true;
    }

    bool flush() noexcept {
        if (used_ == 4) return true;
        auto result = transport_.send(protocol::service, protocol::write, 0,
                                      {buffer_.data(), used_});
        if (!result) {
            error_ = result.error();
            return false;
        }
        used_ = 4;
        return true;
    }

    Transport& transport_;
    std::uint32_t generation_;
    std::array<std::byte, PacketBytes - wire::header_bytes> buffer_{};
    std::size_t used_ = 0;
    std::optional<Error> error_;
    bool active_ = false;
};

} // namespace pxa::ui
