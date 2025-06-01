#pragma once

#include "adrian-allocation-thread.hpp"
#include "adrian-chain.hpp"
#include "adrian-catch-buffer.hpp"

namespace adrian::detail {

inline
auto update_mipmaps(ez::ui_t thread, const model& m, concepts::push_ui_event auto push_ui_event) -> void {
	detail::service_.beach.ui.with_ball<detail::service::MIPMAP_AUDIO_CATCHER>([thread, m, push_ui_event]{
		for (const auto& c : m.chains) {
			if (update_mipmap(thread, m, c)) {
				push_ui_event(ui::events::chain::mipmap_changed{c.id, c.client_data});
			}
		}
	});
}

inline
auto update(ez::ui_t thread, const model& was, const model& now, concepts::push_ui_event auto push_ui_event) -> void {
	diff(thread, was.chains, now.chains, push_ui_event);
	if (was.loading_chains != now.loading_chains) {
		// A loading chain may have been created so awaken the allocation thread.
		detail::service_.critical.cv_allocation_thread_wait.notify_one();
	}
	detail::update_mipmaps(thread, now, push_ui_event);
}

// audio thread receives messages from ui ------------------------------------------
inline
auto receive_(ez::audio_t thread, const model& m, msg::to_audio::catch_buffer::playback_start msg) -> void {
	playback_start(thread, msg.id);
}

inline
auto receive_(ez::audio_t thread, const model& m, msg::to_audio::catch_buffer::playback_stop msg) -> void {
	playback_stop(thread, m, msg.id);
}

inline
auto receive(ez::audio_t thread, const model& m, msg::to_audio::msg msg) -> void {
	std::visit([&](auto&& msg) { receive_(thread, m, msg); }, msg);
}

inline
auto receive_msgs_from_ui(ez::audio_t thread, service::model* service, const model& m) -> void {
	msg::to_audio::msg msg;
	while (service->critical.msgs_to_audio.v.try_dequeue(msg)) {
		receive(thread, m, msg);
	}
}

// ui thread receives messages from audio ------------------------------------------
inline
auto receive_(ez::ui_t thread, const model& m, msg::to_ui::catch_buffer::playback_finished msg, concepts::push_ui_event auto push_ui_event) -> void {
	const auto cbuf_id = msg.id;
	const auto& cbuf   = m.catch_buffers.at(cbuf_id);
	cbuf.service->ui.playback_active = false;
	push_ui_event(ui::events::catch_buffer::playback_finished{cbuf_id, cbuf.client_data});
}

inline
auto receive_(ez::ui_t thread, const model& m, msg::to_ui::catch_buffer::recording_finished msg, concepts::push_ui_event auto push_ui_event) -> void {
	const auto cbuf_id = msg.id;
	const auto& cbuf   = m.catch_buffers.at(cbuf_id);
	push_ui_event(ui::events::catch_buffer::recording_finished{cbuf_id, msg.region, cbuf.client_data});
}

inline
auto receive_(ez::ui_t thread, const model& m, msg::to_ui::catch_buffer::recording_started msg, concepts::push_ui_event auto push_ui_event) -> void {
	const auto cbuf_id = msg.id;
	const auto& cbuf   = m.catch_buffers.at(cbuf_id);
	push_ui_event(ui::events::catch_buffer::recording_started{cbuf_id, msg.beg, cbuf.client_data});
}

inline
auto receive_(ez::ui_t thread, const model& m, msg::to_ui::warn_queue_full msg, concepts::push_ui_event auto push_ui_event) -> void {
	push_ui_event(ui::events::warn_queue_full{msg.size_approx});
}

inline
auto receive(ez::ui_t thread, const model& m, msg::to_ui::msg msg, concepts::push_ui_event auto push_ui_event) -> void {
	std::visit([&](auto&& msg) { receive_(thread, m, msg, push_ui_event); }, msg);
}

inline
auto receive_msgs_from_audio(ez::ui_t thread, service::model* service, const model& m, concepts::push_ui_event auto push_ui_event) -> void {
	msg::to_ui::msg msg;
	while (service->critical.msgs_to_ui.v.try_dequeue(msg)) {
		receive(thread, m, msg, push_ui_event);
	}
}

inline
auto update_mipmaps(ez::audio_t thread, const model& m) -> void {
	detail::service_.beach.audio.with_ball<detail::service::MIPMAP_UI_CATCHER>([thread, m]{
		for (const auto& [_, table] : m.buffers) {
			for (const auto& service : table.service) {
				detail::update_mipmap(thread, service.get());
			}
		}
	});
}

} // adrian::detail

// public interface ----------------------------------------------------------------
namespace adrian {

inline
auto init(ez::ui_t) -> void {
	detail::allocation_thread_ = std::jthread{detail::allocation_thread::func, &detail::service_};
}

inline
auto shutdown(ez::ui_t thread) -> void {
	if (detail::allocation_thread_.joinable()) {
		detail::allocation_thread_.request_stop();
		detail::service_.critical.cv_allocation_thread_wait.notify_one();
		detail::allocation_thread_.join();
	}
}

inline
auto update(ez::audio_t thread) -> void {
	const auto model = detail::service_.model.read(thread);
	detail::receive_msgs_from_ui(thread, &detail::service_, *model);
	detail::update_mipmaps(thread, *model);
}

inline
auto update(ez::ui_t thread, concepts::push_ui_event auto push_ui_event) -> void {
	auto prev_frame = detail::service_.ui.prev_frame;
	auto this_frame = detail::service_.model.read(thread);
	detail::update(thread, prev_frame, this_frame, push_ui_event);
	detail::receive_msgs_from_audio(thread, &detail::service_, this_frame, push_ui_event);
	detail::service_.ui.prev_frame = this_frame;
}

} // adrian