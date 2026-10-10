#pragma once

#include "binary.hpp"
#include <algorithm>

namespace pxa::binary {

// A codec defines an explicit wire schema, not a native object layout. Its
// decoded value must own its contents; Reader's borrowed event expires after
// completion. max_bytes is a compile-time bound, not a default global cache.
template<class C>
concept ValueCodec = requires(Writer& writer, Reader& reader,
                             const typename C::value_type& value) {
    typename C::value_type;
    requires std::is_object_v<typename C::value_type>;
    requires (C::max_bytes > 0);
    typename std::integral_constant<std::size_t, C::max_bytes>;
    { C::encode(writer, value) } -> std::same_as<Result<void>>;
    { C::decode(reader) } -> std::same_as<Result<typename C::value_type>>;
};

template<ValueCodec C>
Result<std::size_t> encode(const typename C::value_type& value,
                          std::span<std::byte> output) {
    Writer writer(output.first(std::min(output.size(), std::size_t{C::max_bytes})));
    auto result = C::encode(writer, value);
    if (!result) return std::unexpected(result.error());
    return writer.size();
}

template<ValueCodec C>
Result<typename C::value_type> decode(std::span<const std::byte> input) {
    if (input.size() > C::max_bytes) return std::unexpected(Error::protocol_error);
    Reader reader(input);
    auto result = C::decode(reader);
    if (!result) return std::unexpected(result.error());
    if (auto finished = reader.finish(); !finished)
        return std::unexpected(finished.error());
    return result;
}

} // namespace pxa::binary
