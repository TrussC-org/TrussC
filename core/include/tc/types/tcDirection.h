#pragma once

// =============================================================================
// tcDirection.h - Generic direction/position enum
// =============================================================================
// Used for text alignment, layout specification, etc.
// Type-safe with enum class, yet can be written concisely like tc::Left

namespace trussc {

// Direction/position specification (generic)
enum class Direction {
    Left,
    Center,
    Right,
    Top,
    Bottom,
    Baseline  // Text-specific: character baseline
};

// Expand to namespace (can be written concisely like tc::Left)
inline constexpr Direction Left = Direction::Left;
inline constexpr Direction Center = Direction::Center;
inline constexpr Direction Right = Direction::Right;
inline constexpr Direction Top = Direction::Top;
inline constexpr Direction Bottom = Direction::Bottom;
inline constexpr Direction Baseline = Direction::Baseline;

} // namespace trussc
