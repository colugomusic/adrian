#pragma once

#include "pp.h"
#include <functional>

namespace adrian {

struct catch_buffer_id { DEFAULT_EQUALITY(catch_buffer_id); int32_t value = -1; explicit operator bool() const { return value >= 0; } };
struct chain_id        { DEFAULT_EQUALITY(chain_id);        int32_t value = -1; explicit operator bool() const { return value >= 0; } };

} // adrian

namespace std {

template <>
struct hash<adrian::catch_buffer_id> {
	auto operator()(const adrian::catch_buffer_id& id) const -> size_t { return std::hash<int32_t>{}(id.value); }
};

template <>
struct hash<adrian::chain_id> {
	auto operator()(const adrian::chain_id& id) const -> size_t { return std::hash<int32_t>{}(id.value); }
};

} // std
