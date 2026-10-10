#pragma once

#include "core.hpp"

// Optional notification routing. Forward declarations avoid pulling every
// service into an application; include the service used to decode its payload.
namespace pxa {
struct SensorSample;
struct ClockTick;
struct SystemEnvironment;
struct PermissionRevoked;
struct PlaybackEvent;
struct SurfaceRelease;
struct WorkStopRequested;
struct IpcRequest;
template<class Contract> struct TypedIpcRequest;
namespace ui { struct CanvasPointer; struct DisplayMetrics; struct UiCapabilities; struct UiAppearance; }

template<> struct EventTraits<ClockTick> {
    static constexpr std::uint16_t service = 4, opcode = 0x8001;
};
template<> struct EventTraits<SystemEnvironment> {
    static constexpr std::uint16_t service = 17, opcode = 0x8004;
};
template<> struct EventTraits<ui::DisplayMetrics> {
    static constexpr std::uint16_t service = 3, opcode = 0x8002;
};
template<> struct EventTraits<ui::UiAppearance> {
    static constexpr std::uint16_t service = 3, opcode = 0x8006;
};
template<> struct EventTraits<ui::UiCapabilities> : EventTraits<ui::DisplayMetrics> {};

template<> struct EventTraits<SensorSample> {
    static constexpr std::uint16_t service = 8, opcode = 0x8001;
};
template<> struct EventTraits<PermissionRevoked> {
    static constexpr std::uint16_t service = 11, opcode = 0x8001;
};
template<> struct EventTraits<PlaybackEvent> {
    static constexpr std::uint16_t service = 10, opcode = 0x8001;
};
template<> struct EventTraits<SurfaceRelease> {
    static constexpr std::uint16_t service = 16, opcode = 0x8001;
};
template<> struct EventTraits<WorkStopRequested> {
    static constexpr std::uint16_t service = 13, opcode = 0x8001;
};
template<> struct EventTraits<IpcRequest> {
    static constexpr std::uint16_t service = 7, opcode = 0x8001;
};
template<class Contract> struct EventTraits<TypedIpcRequest<Contract>> : EventTraits<IpcRequest> {};
template<> struct EventTraits<ui::CanvasPointer> {
    static constexpr std::uint16_t service = 3, opcode = 0x8001;
};
} // namespace pxa
