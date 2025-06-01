#pragma once

#include "adrian-ids.hpp"
#include <ads.hpp>
#include <any>
#include <variant>

namespace adrian::ui::events::catch_buffer {

struct recording_started  { catch_buffer_id id; ads::frame_idx beg; std::any client_data; };
struct recording_finished { catch_buffer_id id; ads::region region; std::any client_data; };
struct playback_finished  { catch_buffer_id id; std::any client_data; };

} // adrian::ui::events::catch_buffer

namespace adrian::ui::events::chain {

struct load_begin     { chain_id id; std::any client_data; };
struct load_end       { chain_id id; std::any client_data; };
struct load_progress  { chain_id id; float progress; std::any client_data; };
struct mipmap_changed { chain_id id; std::any client_data; };

} // adrian::ui::events::chain

namespace adrian::ui::events {

struct warn_queue_full { size_t size_approx; };

} // adrian::ui::events

namespace adrian::ui {

using event = std::variant<
	ui::events::catch_buffer::recording_started,
	ui::events::catch_buffer::recording_finished,
	ui::events::catch_buffer::playback_finished,
	ui::events::chain::load_begin,
	ui::events::chain::load_end,
	ui::events::chain::load_progress,
	ui::events::chain::mipmap_changed,
	ui::events::warn_queue_full
>;

} // adrian::ui
