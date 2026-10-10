#pragma once
#include <pxa/codec.hpp>

struct Save {
    std::int32_t count = 0;
    bool sound = true;
    bool operator==(const Save&) const = default;
};

// Explicit version and fields keep the save portable across native/Wasm/AOT.
struct SaveCodec {
    using value_type = Save;
    static constexpr std::size_t max_bytes = 7;
    static pxa::Result<void> encode(pxa::binary::Writer& writer, const Save& value) {
        if (value.count < 0) return std::unexpected(pxa::Error::invalid_argument);
        if (auto r = writer.write(std::uint16_t{1}); !r) return r;
        if (auto r = writer.write(value.count); !r) return r;
        return writer.write(value.sound);
    }
    static pxa::Result<Save> decode(pxa::binary::Reader& reader) {
        auto version = reader.read<std::uint16_t>();
        if (!version) return std::unexpected(version.error());
        if (*version != 1) return std::unexpected(pxa::Error::unsupported);
        auto count = reader.read<std::int32_t>();
        if (!count) return std::unexpected(count.error());
        auto sound = reader.read<bool>();
        if (!sound) return std::unexpected(sound.error());
        if (*count < 0) return std::unexpected(pxa::Error::protocol_error);
        return Save{*count, *sound};
    }
};
