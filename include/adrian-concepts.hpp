#pragma once

#include "adrian-ui-events.hpp"

namespace adrian::concepts {

template <typename Fn> concept push_ui_event = std::invocable<Fn, adrian::ui::event>;

} // adrian::concepts
