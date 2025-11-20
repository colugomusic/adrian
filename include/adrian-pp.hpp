#pragma once

#define ADRIAN_DEFAULT_EQUALITY(x)  [[nodiscard]] friend auto operator==(const x&, const x&) -> bool = default
#define ADRIAN_DEFAULT_SPACESHIP(x) [[nodiscard]] friend auto operator<=>(const x&, const x&) = default
