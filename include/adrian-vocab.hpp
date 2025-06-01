#pragma once

namespace adrian::voc {

template <typename T> struct old_and_new { T was; T now; auto operator<=>(const old_and_new&) const = default; };
template <typename T> [[nodiscard]] auto changed(const old_and_new<T>& value) -> bool { return value.was != value.now; }

} // adrian::voc