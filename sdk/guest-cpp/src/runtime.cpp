#include <pxa/core.hpp>
#include <pxa/service_wire.hpp>
#include <pxa/sensor.hpp>
#include <algorithm>
#include <bit>

namespace pxa::wire {

bool valid_utf8(std::span<const std::byte> bytes) noexcept {
    std::size_t index = 0;
    while (index < bytes.size()) {
        const auto first = std::to_integer<unsigned>(bytes[index++]);
        if (!first) return false;
        if (first < 0x80) continue;
        unsigned count;
        std::uint32_t code, minimum;
        if (first >= 0xc2 && first <= 0xdf) {
            count = 1; code = first & 0x1f; minimum = 0x80;
        } else if (first >= 0xe0 && first <= 0xef) {
            count = 2; code = first & 0x0f; minimum = 0x800;
        } else if (first >= 0xf0 && first <= 0xf4) {
            count = 3; code = first & 7; minimum = 0x10000;
        } else return false;
        if (bytes.size() - index < count) return false;
        while (count--) {
            const auto next = std::to_integer<unsigned>(bytes[index++]);
            if ((next & 0xc0) != 0x80) return false;
            code = (code << 6) | (next & 0x3f);
        }
        if (code < minimum || code > 0x10ffff ||
            (code >= 0xd800 && code <= 0xdfff)) return false;
    }
    return true;
}

} // namespace pxa::wire

namespace pxa {

namespace {
bool sensor_semantic_valid(std::span<const std::byte> text) noexcept {
    if (text.empty() || text.size() > 64 || text[0] < std::byte{'a'} ||
        text[0] > std::byte{'z'}) return false;
    for (auto byte : text) {
        const auto c = std::to_integer<unsigned>(byte);
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
              c == '.' || c == '_' || c == '-')) return false;
    }
    return true;
}

Result<SensorDescriptor> sensor_descriptor(std::span<const std::byte> bytes) noexcept {
    wire::Records records(bytes);
    auto id = records.take(1, 2);
    auto semantic = records.take(2);
    auto unit = records.take(3, 2);
    auto dimensions = records.take(4, 1);
    auto minimum = records.take(5, 4);
    auto maximum = records.take(6, 4);
    if (!id || !semantic || !unit || !dimensions || !minimum || !maximum ||
        !records.empty() || !sensor_semantic_valid(*semantic))
        return std::unexpected(Error::protocol_error);
    SensorDescriptor output;
    output.id = wire::get16(id->data());
    if (!output.semantic.assign(*semantic))
        return std::unexpected(Error::protocol_error);
    output.unit = static_cast<SensorUnit>(wire::get16(unit->data()));
    output.dimensions = std::to_integer<std::uint8_t>((*dimensions)[0]);
    output.min_period_ms = wire::get32(minimum->data());
    output.max_period_ms = wire::get32(maximum->data());
    if (!output.id || !static_cast<std::uint16_t>(output.unit) ||
        !output.dimensions || output.dimensions > 3 || !output.min_period_ms ||
        output.min_period_ms > output.max_period_ms)
        return std::unexpected(Error::protocol_error);
    return output;
}
} // namespace

Result<std::size_t> decode_sensor_list(std::span<const std::byte> payload,
    std::span<SensorDescriptor> output) noexcept {
    auto body = wire::result_body(payload);
    if (!body) return std::unexpected(body.error());
    wire::Records records(*body);
    std::array<std::uint16_t, 32> ids{};
    std::array<std::span<const std::byte>, 32> semantics{};
    std::size_t count = 0;
    while (!records.empty()) {
        if (count == ids.size()) return std::unexpected(Error::protocol_error);
        auto nested = records.take(1);
        if (!nested) return std::unexpected(nested.error());
        auto descriptor = sensor_descriptor(*nested);
        if (!descriptor) return std::unexpected(descriptor.error());
        wire::Records fields(*nested);
        (void)fields.take(1, 2);
        auto semantic = fields.take(2);
        for (std::size_t i = 0; i < count; ++i) {
            if (ids[i] == descriptor->id ||
                (semantics[i].size() == semantic->size() &&
                 std::equal(semantics[i].begin(), semantics[i].end(), semantic->begin())))
                return std::unexpected(Error::protocol_error);
        }
        ids[count] = descriptor->id;
        semantics[count++] = *semantic;
    }
    if (count > output.size()) return std::unexpected(Error::resource_limit);
    // Validate the entire catalog before modifying caller-owned descriptors.
    records = wire::Records(*body);
    for (std::size_t i = 0; i < count; ++i) {
        auto nested = records.take(1);
        output[i] = *sensor_descriptor(*nested);
    }
    return count;
}

Result<SensorSample> decode_sensor_sample(const Event& event) noexcept {
    if (event.service != 8 || event.opcode != 0x8001 || event.token)
        return std::unexpected(Error::protocol_error);
    wire::Records records(event.payload);
    auto handle = records.take(4, 8);
    auto timestamp = records.take(2, 8);
    auto count = records.take(3, 2);
    auto values = records.take(4);
    if (!handle || !timestamp || !count || !values || !records.empty() ||
        !(wire::get64(handle->data()) >> 32) || wire::get16(count->data()) != 1 ||
        values->empty() || values->size() > 12 || values->size() % 4)
        return std::unexpected(Error::protocol_error);
    SensorSample output;
    output.handle = wire::get64(handle->data());
    output.timestamp_us = wire::get64(timestamp->data());
    output.dimensions = static_cast<std::uint8_t>(values->size() / 4);
    for (std::size_t i = 0; i < output.dimensions; ++i)
        output.values[i] = std::bit_cast<std::int32_t>(wire::get32(values->data() + 4 * i));
    return output;
}

Result<Event> parse_event(std::span<const std::byte> bytes) noexcept {
    if (bytes.size() < wire::header_bytes ||
        bytes.size() > wire::max_control_bytes ||
        wire::get16(bytes.data()) == 0 || wire::get16(bytes.data() + 2) == 0 ||
        wire::get32(bytes.data() + 16) != 0 ||
        wire::get32(bytes.data() + 12) != bytes.size() - wire::header_bytes)
        return std::unexpected(Error::protocol_error);
    return Event{wire::get16(bytes.data()), wire::get16(bytes.data() + 2),
                 wire::get64(bytes.data() + 4),
                 bytes.subspan(wire::header_bytes)};
}

Result<void> Transport::scratch(std::span<std::byte> buffer) noexcept {
    if (buffer.size() < wire::header_bytes + 1 ||
        buffer.size() > wire::max_control_bytes)
        return std::unexpected(Error::invalid_argument);
    packet_ = buffer;
    return {};
}

std::uint64_t Transport::next_token() noexcept {
    token_ = token_ == UINT64_MAX ? 1 : token_ + 1;
    return token_;
}

Result<void> Transport::send(std::uint16_t service, std::uint16_t opcode,
                             std::uint64_t token,
                             std::span<const std::byte> payload) noexcept {
    if (phase_ != Phase::start && phase_ != Phase::event)
        return std::unexpected(Error::bad_state);
    if (!service || !opcode || payload.size() >
        packet_.size() - wire::header_bytes)
        return std::unexpected(Error::invalid_argument);
    // Copy before writing the header: payload may alias any part of scratch().
    wire::copy_bytes(packet_.subspan(wire::header_bytes), payload);
    wire::put16(packet_.data(), service);
    wire::put16(packet_.data() + 2, opcode);
    wire::put64(packet_.data() + 4, token);
    wire::put32(packet_.data() + 12,
                static_cast<std::uint32_t>(payload.size()));
    wire::put32(packet_.data() + 16, 0);
    auto result = pxa_submit(
        reinterpret_cast<const std::uint8_t*>(packet_.data()),
        static_cast<std::uint32_t>(wire::header_bytes + payload.size()));
    if (result != 0) return std::unexpected(static_cast<Error>(result));
    return {};
}

Result<void> Transport::send_prebuilt(
    std::uint16_t service, std::uint16_t opcode, std::uint64_t token,
    std::span<std::byte> packet) noexcept {
    if (phase_ != Phase::start && phase_ != Phase::event)
        return std::unexpected(Error::bad_state);
    if (!service || !opcode || packet.size() < wire::header_bytes ||
        packet.size() > wire::max_control_bytes)
        return std::unexpected(Error::invalid_argument);
    wire::put16(packet.data(), service);
    wire::put16(packet.data() + 2, opcode);
    wire::put64(packet.data() + 4, token);
    wire::put32(packet.data() + 12,
                static_cast<std::uint32_t>(packet.size() - wire::header_bytes));
    wire::put32(packet.data() + 16, 0);
    const auto result = pxa_submit(
        reinterpret_cast<const std::uint8_t*>(packet.data()),
        static_cast<std::uint32_t>(packet.size()));
    if (result != 0) return std::unexpected(static_cast<Error>(result));
    return {};
}

Result<std::uint32_t> Transport::io(std::uint64_t handle,
                                     std::uint32_t operation,
                                     std::span<std::byte> buffer) noexcept {
    if (phase_ != Phase::start && phase_ != Phase::event)
        return std::unexpected(Error::bad_state);
    if (!handle || buffer.size() > UINT32_MAX)
        return std::unexpected(Error::invalid_argument);
    auto result = pxa_io(handle, operation,
                         reinterpret_cast<std::uint8_t*>(buffer.data()),
                         static_cast<std::uint32_t>(buffer.size()));
    if (result < 0) return std::unexpected(static_cast<Error>(result));
    return static_cast<std::uint32_t>(result);
}

Result<void> Transport::close(std::uint64_t handle) noexcept {
    if (!handle) return std::unexpected(Error::invalid_argument);
    std::array<std::byte, 8> payload{};
    wire::put64(payload.data(), handle);
    return send(1, 2, 0, payload);
}

} // namespace pxa
