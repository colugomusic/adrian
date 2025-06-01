#pragma once

#define DEFAULT_EQUALITY(x)  [[nodiscard]] auto operator==(const x&) const -> bool = default
#define DEFAULT_SPACESHIP(x) [[nodiscard]] auto operator<=>(const x&) const = default