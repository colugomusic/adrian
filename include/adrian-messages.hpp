#pragma once

#include "adrian-ids.hpp"
#include <ads.hpp>
#include <readerwriterqueue.h>
#include <variant>

namespace adrian::msg::to_ui::catch_buffer {

struct recording_started  { catch_buffer_id id; ads::frame_idx beg; };
struct recording_finished { catch_buffer_id id; ads::region region; };
struct playback_finished  { catch_buffer_id id; };

} // adrian::msg::to_ui::catch_buffer

namespace adrian::msg::to_ui {

struct warn_queue_full { size_t size_approx; };

using msg = std::variant<
	catch_buffer::recording_started,
	catch_buffer::recording_finished,
	catch_buffer::playback_finished,
	warn_queue_full
>;

struct msg_queue {
	static constexpr auto SIZE = 1024;
	using queue_type = moodycamel::ReaderWriterQueue<msg>;
	queue_type v = queue_type{SIZE};
};

inline
auto send(msg_queue* queue, msg m) -> void {
	if (!queue->v.try_enqueue(m)) {
		static auto already_warned = false;
		if (!already_warned) {
			queue->v.enqueue(warn_queue_full{queue->v.size_approx()});
			already_warned = true;
		}
		queue->v.enqueue(m);
	}
}

} // adrian::msg::to_ui

namespace adrian::msg::to_audio::catch_buffer {

struct playback_start { catch_buffer_id id; };
struct playback_stop  { catch_buffer_id id; };

} // adrian::msg::to_audio::catch_buffer

namespace adrian::msg::to_audio {

using msg = std::variant<
	catch_buffer::playback_start,
	catch_buffer::playback_stop
>;

struct msg_queue {
	static constexpr auto SIZE = 1024;
	using queue_type = moodycamel::ReaderWriterQueue<msg>;
	queue_type v = queue_type{SIZE};
};

} // adrian::msg::to_audio
