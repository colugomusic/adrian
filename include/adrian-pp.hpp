#pragma once

#define DEFAULT_EQUALITY(x)  [[nodiscard]] friend auto operator==(const x&, const x&) -> bool = default
#define DEFAULT_SPACESHIP(x) [[nodiscard]] friend auto operator<=>(const x&, const x&) = default