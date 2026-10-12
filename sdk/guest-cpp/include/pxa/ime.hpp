#pragma once

#include "core.hpp"
#include "service_wire.hpp"
#include <algorithm>
#include <array>
#include <cstring>
#include <string_view>

// Optional provider API. Ordinary applications only need ui::TextInput.
namespace pxa::ime {
inline constexpr std::uint16_t service_id = 22;
enum class Action : std::uint16_t {
    reset = 1, text, backspace, confirm, choose, page, choose_pinyin
};
enum class Layout : std::uint8_t { full, nine };
enum class Language : std::uint8_t { chinese, english };
enum class Edit : std::uint16_t { none, insert, backspace, submit, clear };

struct Input {
    std::uint64_t session;
    std::uint32_t sequence;
    Action action;
    bool sensitive;
    Layout layout;
    Language language;
    std::uint32_t value;
    std::string_view text;
};

inline Result<Input> decode_input(const Event& event) noexcept {
    auto bytes = event.payload;
    if (event.service != service_id || event.opcode != 0x8001)
        return std::unexpected(Error::not_found);
    if (event.token != 0 || bytes.size() < 24 || bytes.size() > wire::max_call_payload_bytes)
        return std::unexpected(Error::invalid_argument);
    auto action = wire::get16(bytes.data() + 12);
    auto flags = wire::get16(bytes.data() + 14);
    auto layout = std::to_integer<std::uint8_t>(bytes[16]);
    auto language = std::to_integer<std::uint8_t>(bytes[17]);
    auto session = wire::get64(bytes.data());
    auto sequence = wire::get32(bytes.data() + 8);
    if (!session || !sequence || action < 1 || action > 7 || flags > 1 ||
        layout > 1 || language > 1 || wire::get16(bytes.data() + 18) ||
        (action != 2 && bytes.size() != 24) || !wire::valid_utf8(bytes.subspan(24)))
        return std::unexpected(Error::invalid_argument);
    return Input{session, sequence, static_cast<Action>(action), flags != 0,
        static_cast<Layout>(layout), static_cast<Language>(language),
        wire::get32(bytes.data() + 20),
        {reinterpret_cast<const char*>(bytes.data() + 24), bytes.size() - 24}};
}

// A bounded wire buffer, separate from the engine's dynamically sized text.
// Use caller-owned storage to keep a provider's stack and default RAM small.
class Frame {
public:
    Frame(std::span<std::byte> storage, const Input& input) noexcept
        : storage_(storage), session_(input.session), sequence_(input.sequence) {}

    Result<void> composition(std::string_view preedit,
        Edit edit = Edit::none, std::string_view commit = {}) noexcept {
        if (size_ != 32 || initialized_ || preedit.size() > UINT16_MAX ||
            commit.size() > UINT16_MAX ||
            (edit == Edit::insert) != !commit.empty() ||
            static_cast<unsigned>(edit) > 4 || storage_.size() < 32 ||
            !wire::valid_utf8(std::as_bytes(std::span(preedit))) ||
            !wire::valid_utf8(std::as_bytes(std::span(commit))) ||
            preedit.size() + commit.size() > storage_.size() - 32)
            return std::unexpected(Error::invalid_argument);
        std::fill_n(storage_.data(), 32, std::byte{});
        wire::put64(storage_.data(), session_);
        wire::put32(storage_.data() + 8, sequence_);
        wire::put16(storage_.data() + 14, static_cast<std::uint16_t>(edit));
        wire::put16(storage_.data() + 20, static_cast<std::uint16_t>(preedit.size()));
        wire::put16(storage_.data() + 22, static_cast<std::uint16_t>(commit.size()));
        copy(preedit); copy(commit); initialized_ = true;
        return {};
    }
    Result<void> candidate(std::string_view text) noexcept {
        if (spellings_ || candidates_ == UINT16_MAX)
            return std::unexpected(Error::bad_state);
        auto result = item(text);
        if (result) ++candidates_;
        return result;
    }
    Result<void> spelling(std::string_view text) noexcept {
        if (spellings_ == UINT16_MAX) return std::unexpected(Error::resource_limit);
        auto result = item(text);
        if (result) ++spellings_;
        return result;
    }
    void submit() noexcept { submit_ = true; }
    void page(std::uint32_t first, bool more) noexcept {
        first_ = first; more_ = more;
    }
    Result<void> present(Transport& transport) noexcept {
        if (!initialized_) return std::unexpected(Error::bad_state);
        if (first_ > UINT32_MAX - candidates_)
            return std::unexpected(Error::invalid_argument);
        wire::put16(storage_.data() + 12, (more_ ? 1 : 0) | (submit_ ? 2 : 0));
        wire::put32(storage_.data() + 16, first_);
        wire::put16(storage_.data() + 24, candidates_);
        wire::put16(storage_.data() + 26, spellings_);
        return transport.send(service_id, 1, 0, storage_.first(size_));
    }
    std::size_t remaining() const noexcept {
        return size_ <= storage_.size() ? storage_.size() - size_ : 0;
    }
private:
    Result<void> item(std::string_view text) noexcept {
        if (!initialized_) return std::unexpected(Error::bad_state);
        if (!wire::valid_utf8(std::as_bytes(std::span(text))) || text.empty() || text.size() > UINT16_MAX)
            return std::unexpected(Error::invalid_argument);
        if (text.size() + 2 > remaining())
            return std::unexpected(Error::resource_limit);
        wire::put16(storage_.data() + size_, static_cast<std::uint16_t>(text.size()));
        size_ += 2; copy(text); return {};
    }
    void copy(std::string_view text) noexcept {
        if (!text.empty()) std::memcpy(storage_.data() + size_, text.data(), text.size());
        size_ += text.size();
    }
    std::span<std::byte> storage_;
    std::uint64_t session_;
    std::uint32_t sequence_, first_ = 0;
    std::size_t size_ = 32;
    std::uint16_t candidates_ = 0, spellings_ = 0;
    bool initialized_ = false, more_ = false, submit_ = false;
};
} // namespace pxa::ime
