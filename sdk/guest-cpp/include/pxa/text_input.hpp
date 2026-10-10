#pragma once

#include "core.hpp"
#include <array>

namespace pxa::ui {

inline constexpr std::uint64_t text_input_control_feature = std::uint64_t{1} << 12;

// Borrowed from its Page, like CanvasRef. Keep it alive until Page unmounts.
// System IME settings and dictionary belong to the Host, not the application.
class TextInputRef {
    template<class, std::size_t, std::size_t, std::size_t, std::size_t> friend class Page;
public:
    TextInputRef() = default;
    TextInputRef(const TextInputRef&) = delete;
    TextInputRef& operator=(const TextInputRef&) = delete;
    bool mounted() const noexcept { return owner_ != nullptr; }
    Result<void> show_keyboard() noexcept { return focus(true); }
    Result<void> hide_keyboard() noexcept { return focus(false); }
private:
    Result<void> focus(bool value) noexcept {
        if (!mounted() || !transport_) return std::unexpected(Error::bad_state);
        std::array<std::byte, 12> payload{};
        wire::put32(payload.data(), 1);
        wire::put32(payload.data() + 4, node_);
        payload[8] = std::byte(value);
        return transport_->send(3, 12, 0, payload);
    }
    Transport* transport_ = nullptr;
    void* owner_ = nullptr;
    std::uint32_t node_ = 0;
};

} // namespace pxa::ui
