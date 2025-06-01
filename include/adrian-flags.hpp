#pragma once

#include "adrian-vocab.hpp"

namespace adrian {

[[nodiscard]] inline constexpr auto is_flag_set(int mask, int flag) -> bool { return bool(mask & flag); } 
[[nodiscard]] inline constexpr auto set_flag(int mask, int flag) -> int { return (mask |= flag); }
[[nodiscard]] inline constexpr auto unset_flag(int mask, int flag) -> int { return (mask &= ~(flag)); }
[[nodiscard]] inline constexpr auto set_flag(int mask, int flag, bool on) -> int { return on ? set_flag(mask, flag) : unset_flag(mask, flag); } 
[[nodiscard]] inline constexpr auto is_flag_different(int a, int b, int flag) -> bool { return is_flag_set(a, flag) != is_flag_set(b, flag); }

template <typename Mask> [[nodiscard]] constexpr
auto has_single_bit(Mask mask) -> bool {
	return mask.value != 0 && (mask.value & (mask.value - 1)) == 0;
}

template <typename Mask> [[nodiscard]] constexpr
auto is_flag_set(Mask mask, typename Mask::e flag) -> bool {
	return bool(mask.value & flag);
}

template <typename Mask> [[nodiscard]] constexpr
auto set_flag(Mask mask, typename Mask::e flag) -> Mask {
	return {(mask.value |= flag)};
}

template <typename Mask> [[nodiscard]] constexpr
auto unset_flag(Mask mask, typename Mask::e flag) -> Mask {
	return {(mask.value &= ~(flag))};
} 

template <typename Mask> [[nodiscard]] constexpr
auto set_flag(Mask mask, typename Mask::e flag, bool on) -> Mask {
	return on ? set_flag(mask, flag) : unset_flag(mask, flag);
} 

template <typename Mask> [[nodiscard]] constexpr
auto is_flag_different(Mask a, Mask b, typename Mask::e flag) -> bool {
	return is_flag_set(a, flag) != is_flag_set(b, flag);
}

template <typename Mask> [[nodiscard]] constexpr
auto flag_diff(Mask a, Mask b, typename Mask::e flag) -> voc::old_and_new<bool> {
	return {is_flag_set(a, flag), is_flag_set(b, flag)};
}

} // adrian