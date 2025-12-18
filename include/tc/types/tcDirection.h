#pragma once

// =============================================================================
// tcDirection.h - 汎用方向・位置指定 enum
// =============================================================================
// テキストアラインメント、レイアウト指定などに使用
// enum class で型安全を保ちつつ、tc::Left のように短く書ける

namespace trussc {

// 方向・位置指定（汎用）
enum class Direction {
    Left,
    Center,
    Right,
    Top,
    Bottom,
    Baseline  // テキスト専用：文字のベースライン
};

// 名前空間に展開（tc::Left のように短く書ける）
inline constexpr Direction Left = Direction::Left;
inline constexpr Direction Center = Direction::Center;
inline constexpr Direction Right = Direction::Right;
inline constexpr Direction Top = Direction::Top;
inline constexpr Direction Bottom = Direction::Bottom;
inline constexpr Direction Baseline = Direction::Baseline;

} // namespace trussc
