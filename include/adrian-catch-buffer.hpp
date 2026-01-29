#pragma once

#include "adrian-chain.hpp"

namespace adrian::detail {

[[nodiscard]] inline
auto make_catch_buffer(ez::nort_t thread, model m, ads::channel_count channel_count, ads::frame_count frame_count, chain_options options, std::any client_data) -> std::tuple<model, catch_buffer_id> {
	catch_buffer::model cbuf;
	cbuf.id            = {++m.next_id};
	cbuf.service       = std::make_shared<catch_buffer::service::model>();
	cbuf.chain_options = options;
	cbuf.client_data   = client_data;
	peak_gate::init(&cbuf.service->audio.peak_gate, channel_count, kFloatsPerDSPVector * 128.0f);
	std::tie(m, cbuf.chain) = make_chain(thread, std::move(m), channel_count, frame_count * 2, options, client_data);
	m.catch_buffers = std::move(m.catch_buffers).insert(cbuf);
	return std::make_tuple(std::move(m), cbuf.id);
}

[[nodiscard]] inline
auto make_catch_buffer(ez::nort_t thread, service::model* service, ads::channel_count channel_count, ads::frame_count frame_count, chain_options options, std::any client_data) -> catch_buffer_id {
	catch_buffer_id id;
	service->model.update_publish(thread, [channel_count, frame_count, options, client_data, &id](detail::model&& m) mutable {
		std::tie(m, id) = detail::make_catch_buffer(ez::nort, std::move(m), channel_count, frame_count, options, client_data);
		return std::move(m);
	});
	return id;
}

[[nodiscard]] inline
auto erase(model m, catch_buffer_id id) -> model {
	const auto& cbuf = m.catch_buffers.at(id);
	m = detail::erase(std::move(m), cbuf.chain);
	m.catch_buffers = m.catch_buffers.erase(id);
	return m;
}

inline
auto erase(ez::nort_t thread, service::model* service, catch_buffer_id id) -> void {
	service->model.update_publish(thread, [id](detail::model x){
		return erase(std::move(x), id);
	});
}

[[nodiscard]] inline
auto advance_marker(ads::frame_count frame_count, uint64_t current_marker) -> uint64_t {
	current_marker += kFloatsPerDSPVector;
	if (current_marker >= frame_count) {
		current_marker -= frame_count;
	}
	return current_marker;
}

inline
auto advance_write_marker(catch_buffer::service::critical* critical, ads::frame_count frame_count, uint64_t current_marker) -> void {
	critical->write_marker.store(advance_marker(frame_count, current_marker), std::memory_order_release);
}

[[nodiscard]] inline
auto get_partition_size(ads::frame_count frame_count) -> ads::frame_count {
	return {frame_count.value / 2};
}

[[nodiscard]] inline
auto get_partition_size(const chain::model& chain) -> ads::frame_count {
	return get_partition_size(chain.frame_count);
}

inline
auto record(ez::audio_t thread, service::model* service, const model& m, const catch_buffer::model& cbuf, const chain::model& chain, bool record_gate, auto write_fn) -> void {
	auto& audio    = cbuf.service->audio;
	auto& critical = cbuf.service->critical;
	const auto record_active = critical.record_active.load(std::memory_order_relaxed);
	if (record_gate) {
		const auto write_marker = critical.write_marker.load(std::memory_order_relaxed);
		const auto write_marker_frame = ads::frame_idx{static_cast<int64_t>(write_marker)};
		detail::scary_write_one_valid_sub_buffer_region(thread, service, chain.id, write_marker_frame, {kFloatsPerDSPVector}, write_fn);
		if (!record_active) {
			audio.record_start = write_marker_frame;
			msg::to_ui::send(&service->critical.msgs_to_ui, msg::to_ui::catch_buffer::recording_started{cbuf.id, audio.record_start});
			critical.record_active.store(true, std::memory_order_relaxed);
		}
		advance_write_marker(&critical, chain.frame_count, write_marker);
	}
	else {
		if (record_active) {
			const auto partition_size     = get_partition_size(chain);
			const auto write_marker       = critical.write_marker.load(std::memory_order_relaxed);
			const auto write_marker_frame = ads::frame_idx{static_cast<int64_t>(write_marker)};
			const auto beg                = audio.record_start % partition_size;
			const auto end                = write_marker_frame % partition_size;
			msg::to_ui::send(&service->critical.msgs_to_ui, msg::to_ui::catch_buffer::recording_finished{cbuf.id, {beg, end}});
			critical.record_active.store(false, std::memory_order_relaxed);
		}
	}
}

[[nodiscard]] inline
auto is_aligned(ads::frame_idx start, ads::frame_count frame_count) -> bool {
	return start % kFloatsPerDSPVector == 0 && frame_count == kFloatsPerDSPVector;
}

[[nodiscard]]
auto get_partitioned_read_frame(ads::frame_count frame_count, uint64_t write_marker, auto read_frame) -> decltype(read_frame) {
    const auto partition_size = get_partition_size(frame_count).value;
	// The part of the chain that is being written to
	const auto write_part = write_marker >= partition_size;
	// The part of the chain that is not being written to
	const auto other_part = !write_part;
	// We read from the other part if to the left of the write marker, otherwise read from the write part
	const auto read_part  = read_frame < (write_marker % partition_size) ? write_part : other_part;
	return read_frame + (partition_size * read_part);
}

[[nodiscard]]
auto get_partitioned_read_frame(const catch_buffer::model& cbuf, ads::frame_count frame_count, auto read_frame) -> decltype(read_frame) {
	const auto write_marker = cbuf.service->critical.write_marker.load(std::memory_order_acquire);
	return get_partitioned_read_frame(frame_count, write_marker, read_frame);
}

[[nodiscard]] inline
auto playback_one_channel(ez::audio_t, const model& m, const catch_buffer::model& cbuf, const chain::model& chain, ads::channel_idx ch, ads::frame_idx read_marker) -> ml::DSPVector {
	ml::DSPVector out;
	auto input = [&](float* chunk, ads::frame_idx start, ads::frame_count frame_count) -> ads::frame_count {
		auto transfer = [&](const float* buffer, ads::frame_idx start, ads::frame_count frame_count) -> ads::frame_count {
			std::copy(buffer, buffer + frame_count.value, chunk);
			return frame_count;
		};
		return detail::scary_read_one_valid_sub_buffer_region(m, chain, ch, start, frame_count, transfer);
	};
	auto output = [&out](const float* chunk, ads::frame_idx start, ads::frame_count frame_count) -> ads::frame_count {
		assert (frame_count.value == kFloatsPerDSPVector);
		ml::loadAligned(out, chunk);
		return frame_count;
	};
	auto input_start_xform = [&](ads::frame_idx fr) -> ads::frame_idx {
		return get_partitioned_read_frame(cbuf, chain.frame_count, fr);
	};
	static constexpr auto input_region_alignment  = processor::input_region_alignment{BUFFER_SIZE};
	static constexpr auto output_region_alignment = processor::OUTPUT_REGION_ALIGNMENT_IGNORE;
	static constexpr auto chunk_size              = processor::chunk_size{kFloatsPerDSPVector};
	static constexpr auto fixed_chunk_size        = processor::fixed_chunk_size::on;
	const auto frames_processed = detail::processor::process<input_region_alignment, output_region_alignment, chunk_size, fixed_chunk_size>(read_marker, read_marker, {kFloatsPerDSPVector}, input, output, input_start_xform);
	assert (frames_processed.value == kFloatsPerDSPVector);
	return out;
}

[[nodiscard]] inline
auto playback_mono(ez::audio_t thread, const model& m, const catch_buffer::model& cbuf, const chain::model& chain, ads::frame_idx read_marker) -> ml::DSPVectorArray<2> {
	return ml::repeatRows<2>(playback_one_channel(thread, m, cbuf, chain, ads::channel_idx{0}, read_marker));
}

[[nodiscard]] inline
auto playback_stereo(ez::audio_t thread, const model& m, const catch_buffer::model& cbuf, const chain::model& chain, ads::frame_idx read_marker) -> ml::DSPVectorArray<2> {
	ml::DSPVectorArray<2> out;
	for (auto ch = ads::channel_idx{0}; ch < chain.channel_count; ++ch) {
		auto& row = out.row(static_cast<int>(ch.value));
		row = playback_one_channel(thread, m, cbuf, chain, ch, read_marker);
	}
	return out;
}

[[nodiscard]] inline
auto playback(ez::audio_t thread, service::model* service, const model& m, const catch_buffer::model& cbuf, const chain::model& chain) -> ml::DSPVectorArray<2> {
	ml::DSPVectorArray<2> out;
	auto& audio                = cbuf.service->audio;
	auto& critical             = cbuf.service->critical;
	if (!audio.playback_active) { return {}; }
	auto read_marker = critical.playback_marker.load(std::memory_order_relaxed);
	if (chain.channel_count.value == 1) { out = playback_mono  (thread, m, cbuf, chain, {static_cast<int64_t>(read_marker)}); }
	else                                { out = playback_stereo(thread, m, cbuf, chain, {static_cast<int64_t>(read_marker)}); }
	read_marker = advance_marker(chain.frame_count, read_marker);
	critical.playback_marker.store(read_marker, std::memory_order_relaxed);
	if (read_marker >= cbuf.playback_region.end) {
		audio.playback_active = false;
		msg::to_ui::send(&service->critical.msgs_to_ui, msg::to_ui::catch_buffer::playback_finished{cbuf.id});
	}
	return out;
}

[[nodiscard]] inline
auto process(ez::audio_t thread, service::model* service, const model& m, const catch_buffer::model& cbuf, const ml::DSPVector& in, float threshold, float gain, bool disable_recording) -> ml::DSPVectorArray<2> {
	auto& audio            = cbuf.service->audio;
	const auto& chain      = m.chains.at(cbuf.chain);
	const auto record_gate = disable_recording ? false : peak_gate::process(&audio.peak_gate, in, threshold);
	auto write_fn = [in, gain](float* buffer, ads::frame_idx start, ads::frame_count frame_count) -> ads::frame_count {
		assert (frame_count.value == kFloatsPerDSPVector);
		ml::storeAligned(in * gain, buffer);
		return ads::frame_count{kFloatsPerDSPVector};
	};
	record(thread, service, m, cbuf, chain, record_gate, write_fn);
	return playback(thread, service, m, cbuf, chain);
}

[[nodiscard]] inline
auto process(ez::audio_t thread, service::model* service, const model& m, const catch_buffer::model& cbuf, const ml::DSPVectorArray<2>& in, float threshold, float gain, bool disable_recording) -> ml::DSPVectorArray<2> {
	auto& audio            = cbuf.service->audio;
	const auto& chain      = m.chains.at(cbuf.chain);
	const auto record_gate = disable_recording ? false : peak_gate::process(&audio.peak_gate, in, threshold);
	auto write_fn = [in, gain](float* buffer, ads::channel_idx ch, ads::frame_idx start, ads::frame_count frame_count) -> ads::frame_count {
		assert (frame_count.value == kFloatsPerDSPVector);
		assert (ch.value < 2);
		ml::storeAligned(in.constRow(static_cast<int>(ch.value)) * gain, buffer);
		return ads::frame_count{kFloatsPerDSPVector};
	};
	record(thread, service, m, cbuf, chain, record_gate, write_fn);
	return playback(thread, service, m, cbuf, chain);
}

[[nodiscard]] inline
auto get_channel_count(const model& m, const catch_buffer::model& cbuf) -> ads::channel_count {
	return m.chains.at(cbuf.chain).channel_count;
}

[[nodiscard]] inline
auto get_channel_count(const model& m, catch_buffer_id id) -> ads::channel_count {
	return get_channel_count(m, m.catch_buffers.at(id));
}

[[nodiscard]] inline
auto get_channel_count(ez::ui_t thread, service::model* service, catch_buffer_id id) -> ads::channel_count {
	return get_channel_count(service->model.read(thread), id);
}

[[nodiscard]] inline
auto get_frame_count(const model& m, const catch_buffer::model& cbuf) -> ads::frame_count {
	return get_partition_size(m.chains.at(cbuf.chain).frame_count);
}

[[nodiscard]] inline
auto get_frame_count(const model& m, catch_buffer_id id) -> ads::frame_count {
	return get_frame_count(m, m.catch_buffers.at(id));
}

[[nodiscard]] inline
auto get_frame_count(ez::ui_t thread, service::model* service, catch_buffer_id id) -> ads::frame_count {
	return get_frame_count(service->model.read(thread), id);
}

[[nodiscard]] inline
auto get_playback_marker(const model& m, const catch_buffer::model& cbuf) -> ads::frame_idx {
	const auto& chain = m.chains.at(cbuf.chain);
	const auto marker = cbuf.service->critical.playback_marker.load(std::memory_order_relaxed);
	return {static_cast<int64_t>(marker % get_partition_size(chain.frame_count).value)};
}

[[nodiscard]] inline
auto get_write_marker(const model& m, const catch_buffer::model& cbuf) -> ads::frame_idx {
	const auto& chain = m.chains.at(cbuf.chain);
	const auto marker = cbuf.service->critical.write_marker.load(std::memory_order_relaxed);
	return {static_cast<int64_t>(marker % get_partition_size(chain.frame_count).value)};
}

[[nodiscard]] inline
auto is_record_active(const catch_buffer::model& cbuf) -> bool {
	return cbuf.service->critical.record_active.load(std::memory_order_relaxed);
}

[[nodiscard]] inline
auto is_playback_active(ez::ui_t, const catch_buffer::model& cbuf) -> bool {
	return cbuf.service->ui.playback_active;
}

[[nodiscard]] inline
auto get_playback_marker(const model& m, catch_buffer_id id) -> ads::frame_idx {
	return get_playback_marker(m, m.catch_buffers.at(id));
}

[[nodiscard]] inline
auto get_write_marker(const model& m, catch_buffer_id id) -> ads::frame_idx {
	return get_write_marker(m, m.catch_buffers.at(id));
}

[[nodiscard]] inline
auto is_record_active(const model& m, catch_buffer_id id) -> bool {
	return is_record_active(m.catch_buffers.at(id));
}

[[nodiscard]] inline
auto is_playback_active(ez::ui_t thread, const model& m, catch_buffer_id id) -> bool {
	return is_playback_active(thread, m.catch_buffers.at(id));
}

[[nodiscard]] inline
auto get_playback_marker(ez::ui_t thread, service::model* service, catch_buffer_id id) -> ads::frame_idx {
	return detail::get_playback_marker(service->model.read(thread), id);
}

[[nodiscard]] inline
auto get_write_marker(ez::ui_t thread, service::model* service, catch_buffer_id id) -> ads::frame_idx {
	return detail::get_write_marker(service->model.read(thread), id);
}

[[nodiscard]] inline
auto is_record_active(ez::ui_t thread, service::model* service, catch_buffer_id id) -> bool {
	return detail::is_record_active(service->model.read(thread), id);
}

[[nodiscard]] inline
auto is_playback_active(ez::ui_t thread, service::model* service, catch_buffer_id id) -> bool {
	return detail::is_playback_active(thread, service->model.read(thread), id);
}

[[nodiscard]] inline
auto read_mipmap(const model& m, const catch_buffer::model& cbuf, double bin_size, ads::channel_idx ch, double fr) -> ads::mipmap_minmax<uint8_t> {
	const auto& chain = m.chains.at(cbuf.chain);
	if (fr < 0)                                      { return {}; }
	if (fr >= get_partition_size(chain.frame_count)) { return {}; }
	fr = get_partitioned_read_frame(cbuf, chain.frame_count, fr);
	return detail::read_mipmap(m, cbuf.chain, bin_size, ch, fr);
}

[[nodiscard]] inline
auto read_mipmap(const model& m, catch_buffer_id id, double bin_size, ads::channel_idx ch, double fr) -> ads::mipmap_minmax<uint8_t> {
	return read_mipmap(m, m.catch_buffers.at(id), bin_size, ch, fr);
}

[[nodiscard]] inline
auto read_mipmap(ez::ui_t thread, service::model* service, catch_buffer_id id, double bin_size, ads::channel_idx ch, double fr) -> ads::mipmap_minmax<uint8_t> {
	return read_mipmap(service->model.read(thread), id, bin_size, ch, fr);
}

template <typename ReadFn>
	requires ads::concepts::is_single_channel_read_fn<float, ReadFn>
[[nodiscard]]
auto read(const model& m, const catch_buffer::model& cbuf, const chain::model& chain, ads::channel_idx ch, ads::frame_idx start, ads::frame_count frame_count, ReadFn read_fn) -> ads::frame_count {
	auto input = [&](float* chunk, ads::frame_idx start, ads::frame_count frame_count) -> ads::frame_count {
		auto transfer = [&](const float* buffer, ads::frame_idx start, ads::frame_count frame_count) -> ads::frame_count {
			std::copy(buffer, buffer + frame_count.value, chunk);
			return frame_count;
		};
		return scary_read_one_valid_sub_buffer_region(m, chain, ch, start, frame_count, transfer);
	};
	auto output = [&](const float* chunk, ads::frame_idx start, ads::frame_count frame_count) -> ads::frame_count {
		return read_fn(chunk, start, frame_count);
	};
	auto input_start_xform = [&](ads::frame_idx fr) -> ads::frame_idx {
		return get_partitioned_read_frame(cbuf, chain.frame_count, fr);
	};
	static constexpr auto input_region_alignment  = processor::input_region_alignment{BUFFER_SIZE};
	static constexpr auto output_region_alignment = processor::OUTPUT_REGION_ALIGNMENT_IGNORE;
	static constexpr auto chunk_size              = processor::chunk_size{kFloatsPerDSPVector};
	static constexpr auto fixed_chunk_size        = processor::fixed_chunk_size::off;
	const auto frames_processed =
		processor::process<input_region_alignment, output_region_alignment, chunk_size, fixed_chunk_size>(start, start, frame_count, input, output, input_start_xform);
	assert (frames_processed == frame_count);
	return frames_processed;
}

template <typename ReadFn>
	requires ads::concepts::is_multi_channel_read_fn<float, ReadFn>
[[nodiscard]]
auto read(const model& m, const catch_buffer::model& cbuf, const chain::model& chain, ads::frame_idx start, ads::frame_count frame_count, ReadFn read_fn) -> ads::frame_count {
	for (auto ch = ads::channel_idx{0}; ch < chain.channel_count; ++ch) {
		auto adapter = [read_fn, ch](const float* buffer, ads::frame_idx start, ads::frame_count frame_count) {
			return read_fn(buffer, ch, start, frame_count);
		};
		const auto frames_read = read(m, cbuf, ch, start, frame_count, adapter);
		assert (frames_read == frame_count);
	}
	return frame_count;
}

template <typename ReadFn>
	requires ads::concepts::is_single_channel_read_fn<float, ReadFn>
[[nodiscard]]
auto read(const model& m, const catch_buffer::model& cbuf, ads::channel_idx ch, ads::frame_idx start, ads::frame_count frame_count, ReadFn read_fn) -> ads::frame_count {
	return read(m, cbuf, m.chains.at(cbuf.chain), ch, start, frame_count, read_fn);
}

template <typename ReadFn>
	requires ads::concepts::is_multi_channel_read_fn<float, ReadFn>
[[nodiscard]]
auto read(const model& m, const catch_buffer::model& cbuf, ads::frame_idx start, ads::frame_count frame_count, ReadFn read_fn) -> ads::frame_count {
	return read(m, cbuf, m.chains.at(cbuf.chain), start, frame_count, read_fn);
}

template <typename ReadFn>
	requires ads::concepts::is_single_channel_read_fn<float, ReadFn>
[[nodiscard]]
auto read(const model& m, catch_buffer_id id, ads::channel_idx ch, ads::frame_idx start, ads::frame_count frame_count, ReadFn read_fn) -> ads::frame_count {
	return read(m, m.catch_buffers.at(id), ch, start, frame_count, read_fn);
}

template <typename ReadFn>
	requires ads::concepts::is_multi_channel_read_fn<float, ReadFn>
[[nodiscard]]
auto read(const model& m, catch_buffer_id id, ads::frame_idx start, ads::frame_count frame_count, ReadFn read_fn) -> ads::frame_count {
	return read(m, m.catch_buffers.at(id), start, frame_count, read_fn);
}

template <typename ReadFn>
	requires ads::concepts::is_single_channel_read_fn<float, ReadFn>
[[nodiscard]]
auto read(ez::nort_t thread, service::model* service, catch_buffer_id id, ads::channel_idx ch, ads::frame_idx start, ads::frame_count frame_count, ReadFn read_fn) -> ads::frame_count {
	return read(service->model.read(thread), id, ch, start, frame_count, read_fn);
}

template <typename ReadFn>
	requires ads::concepts::is_multi_channel_read_fn<float, ReadFn>
[[nodiscard]]
auto read(ez::nort_t thread, service::model* service, catch_buffer_id id, ads::frame_idx start, ads::frame_count frame_count, ReadFn read_fn) -> ads::frame_count {
	return read(service->model.read(thread), id, start, frame_count, read_fn);
}

inline
auto set_mipmaps_enabled(ez::nort_t thread, service::model* service, catch_buffer_id id, bool enabled) -> void {
	service->model.update_publish(thread, [id, enabled](detail::model x){
		const auto& cbuf = x.catch_buffers.at(id);
		return set_mipmaps_enabled(std::move(x), cbuf.chain, enabled);
	});
}

[[nodiscard]] inline
auto set_playback_region(model&& m, catch_buffer_id id, ads::region region) -> model {
	m.catch_buffers = m.catch_buffers.update(id, [region](detail::catch_buffer::model x){
		x.playback_region = region;
		return x;
	});
	return m;
}

inline
auto playback_start(ez::audio_t, const model& m, catch_buffer_id id) -> void {
	const auto& cbuf      = m.catch_buffers.at(id);
	auto& audio           = cbuf.service->audio;
	auto& critical        = cbuf.service->critical;
	audio.playback_active = true;
	critical.playback_marker.store(cbuf.playback_region.beg.value, std::memory_order_relaxed);
}

inline
auto playback_start(ez::audio_t thread, service::model* service, catch_buffer_id id) -> void {
	playback_start(thread, *service->model.read(thread), id);
}

inline
auto playback_start(ez::audio_t thread, catch_buffer_id id) -> void {
	playback_start(thread, &detail::service_, id);
}

inline
auto playback_start(ez::ui_t thread, service::model* service, catch_buffer_id id, ads::region region) -> void {
	const auto model = service->model.update_publish(thread, [id, region](detail::model x){
		return set_playback_region(std::move(x), id, region);
	});
	const auto& cbuf = model.catch_buffers.at(id);
	cbuf.service->ui.playback_active = true;
	// This is only written at this point so that the UI thread can immediately
	// see the value. It's not required from the audio thread's perspective.
	cbuf.service->critical.playback_marker.store(region.beg.value, std::memory_order_relaxed);
	// Tell the audio thread to start playback.
	service->critical.msgs_to_audio.v.enqueue(msg::to_audio::catch_buffer::playback_start{id});
}

inline
auto playback_stop(ez::audio_t, const model& m, catch_buffer_id id) -> void {
	const auto& cbuf = m.catch_buffers.at(id);
	auto& audio      = cbuf.service->audio;
	audio.playback_active = false;
}

inline
auto playback_stop(ez::ui_t thread, service::model* service, catch_buffer_id id) -> void {
	const auto model = service->model.read(thread);
	const auto& cbuf = model.catch_buffers.at(id);
	cbuf.service->ui.playback_active = false;
	service->critical.msgs_to_audio.v.enqueue(msg::to_audio::catch_buffer::playback_stop{id});
}

[[nodiscard]] inline
auto process(ez::audio_t thread, service::model* service, const model& m, catch_buffer_id id, const ml::DSPVector& in, float threshold, float gain, bool disable_recording) -> ml::DSPVectorArray<2> {
	return detail::process(thread, service, m, m.catch_buffers.at(id), in, threshold, gain, disable_recording);
}

[[nodiscard]] inline
auto process(ez::audio_t thread, service::model* service, const model& m, catch_buffer_id id, const ml::DSPVectorArray<2>& in, float threshold, float gain, bool disable_recording) -> ml::DSPVectorArray<2> {
	return detail::process(thread, service, m, m.catch_buffers.at(id), in, threshold, gain, disable_recording);
}

[[nodiscard]] inline
auto process(ez::audio_t thread, service::model* service, catch_buffer_id id, const ml::DSPVector& in, float threshold, float gain, bool disable_recording) -> ml::DSPVectorArray<2> {
	return detail::process(thread, service, *service->model.read(thread), id, in, threshold, gain, disable_recording);
}

[[nodiscard]] inline
auto process(ez::audio_t thread, service::model* service, catch_buffer_id id, const ml::DSPVectorArray<2>& in, float threshold, float gain, bool disable_recording) -> ml::DSPVectorArray<2> {
	return detail::process(thread, service, *service->model.read(thread), id, in, threshold, gain, disable_recording);
}

template <uint64_t DestChs, uint64_t DestFrs> inline
auto copy(const model& m, const catch_buffer::model& cbuf, ads::frame_idx src_start, ads::data<float, DestChs, DestFrs>* dest, ads::frame_idx dest_start, ads::frame_count frame_count) -> ads::frame_count {
	auto write_fn = [&](float* write_to, ads::channel_idx ch, ads::frame_idx start, ads::frame_count frame_count) {
		auto read_fn = [&](const float* read_from, ads::frame_idx start, ads::frame_count frame_count) {
			std::copy(read_from, read_from + frame_count.value, write_to);
			return frame_count;
		};
		return read(m, cbuf, ch, src_start, frame_count, read_fn);
	};
	return dest->write(dest_start, frame_count, write_fn);
}

template <uint64_t DestChs, uint64_t DestFrs> inline
auto copy(const model& m, catch_buffer_id id, ads::frame_idx start, ads::data<float, DestChs, DestFrs>* dest, ads::frame_idx dest_start, ads::frame_count frame_count) -> ads::frame_count {
	const auto& cbuf          = m.catch_buffers.at(id);
	const auto& chain         = m.chains.at(cbuf.chain);
	const auto partition_size = get_partition_size(chain);
	return detail::copy(m, cbuf, start % partition_size, dest, dest_start, frame_count);
}

template <uint64_t DestChs, uint64_t DestFrs> inline
auto copy(ez::nort_t thread, service::model* service, catch_buffer_id id, ads::frame_idx start, ads::data<float, DestChs, DestFrs>* dest, ads::frame_idx dest_start, ads::frame_count frame_count) -> ads::frame_count {
	return detail::copy(service->model.read(thread), id, start, dest, dest_start, frame_count);
}

[[nodiscard]] inline
auto reconfigure(ez::nort_t thread, model&& m, catch_buffer_id id, ads::channel_count chc, ads::frame_count frc) -> model {
	auto cbuf         = m.catch_buffers.at(id);
	const auto& chain = m.chains.at(cbuf.chain);
	std::tie(m, cbuf.chain) = make_chain(thread, std::move(m), chc, frc * 2, cbuf.chain_options, chain.client_data);
	m = erase(std::move(m), chain.id);
	m.catch_buffers = m.catch_buffers.insert(cbuf);
	cbuf.service->critical.playback_marker.store(0, std::memory_order_relaxed);
	cbuf.service->critical.write_marker.store(0, std::memory_order_relaxed);
	return m;
}

inline
auto reconfigure(ez::nort_t thread, service::model* service, catch_buffer_id id, ads::channel_count chc, ads::frame_count frc) -> void {
	service->model.update_publish(thread, [id, chc, frc](detail::model x){
		return reconfigure(ez::nort, std::move(x), id, chc, frc);
	});
}

inline
auto reconfigure(ez::nort_t thread, catch_buffer_id id, ads::channel_count chc, ads::frame_count frc) -> void {
	detail::reconfigure(thread, &detail::service_, id, chc, frc);
}

} // adrian::detail

namespace adrian {

template <uint64_t DestChs, uint64_t DestFrs>
auto copy(ez::nort_t thread, catch_buffer_id id, ads::frame_idx start, ads::data<float, DestChs, DestFrs>* dest, ads::frame_idx dest_start, ads::frame_count frame_count) -> ads::frame_count {
	return detail::copy(thread, &detail::service_, id, start, dest, dest_start, frame_count);
}

[[nodiscard]] inline
auto get_channel_count(ez::ui_t thread, catch_buffer_id id) -> ads::channel_count {
	return detail::get_channel_count(thread, &detail::service_, id);
}

[[nodiscard]] inline
auto get_frame_count(ez::ui_t thread, catch_buffer_id id) -> ads::frame_count {
	return detail::get_frame_count(thread, &detail::service_, id);
}

[[nodiscard]] inline
auto get_playback_marker(ez::ui_t thread, catch_buffer_id id) -> ads::frame_idx {
	return detail::get_playback_marker(thread, &detail::service_, id);
}

[[nodiscard]] inline
auto get_write_marker(ez::ui_t thread, catch_buffer_id id) -> ads::frame_idx {
	return detail::get_write_marker(thread, &detail::service_, id);
}

[[nodiscard]] inline
auto is_record_active(ez::ui_t thread, catch_buffer_id id) -> bool {
	return detail::is_record_active(thread, &detail::service_, id);
}

[[nodiscard]] inline
auto is_playback_active(ez::ui_t thread, catch_buffer_id id) -> bool {
	return detail::is_playback_active(thread, &detail::service_, id);
}

[[nodiscard]] inline
auto make_catch_buffer(ez::nort_t thread, ads::channel_count channel_count, ads::frame_count frame_count, chain_options options, std::any client_data) -> catch_buffer_id {
	return detail::make_catch_buffer(thread, &detail::service_, channel_count, frame_count, options, client_data);
}

inline
auto playback_start(ez::ui_t thread, catch_buffer_id id, ads::region region) -> void {
	return detail::playback_start(thread, &detail::service_, id, region);
}

inline
auto playback_stop(ez::ui_t thread, catch_buffer_id id) -> void {
	return detail::playback_stop(thread, &detail::service_, id);
}

[[nodiscard]] inline
auto process(ez::audio_t thread, catch_buffer_id id, const ml::DSPVector& in, float threshold, float gain, bool disable_recording = false) -> ml::DSPVectorArray<2> {
	return detail::process(thread, &detail::service_, id, in, threshold, gain, disable_recording);
}

[[nodiscard]] inline
auto process(ez::audio_t thread, catch_buffer_id id, const ml::DSPVectorArray<2>& in, float threshold, float gain, bool disable_recording = false) -> ml::DSPVectorArray<2> {
	return detail::process(thread, &detail::service_, id, in, threshold, gain, disable_recording);
}

template <typename ReadFn>
	requires ads::concepts::is_multi_channel_read_fn<float, ReadFn>
[[nodiscard]] auto read(ez::nort_t thread, catch_buffer_id id, ads::frame_idx start, ads::frame_count frame_count, ReadFn read_fn) -> ads::frame_count {
	return detail::read(thread, &detail::service_, id, start, read_fn);
}

template <typename ReadFn>
	requires ads::concepts::is_single_channel_read_fn<float, ReadFn>
[[nodiscard]] auto read(ez::nort_t thread, catch_buffer_id id, ads::channel_idx ch, ads::frame_idx start, ads::frame_count frame_count, ReadFn read_fn) -> ads::frame_count {
	return detail::read(thread, &detail::service_, id, ch, start, frame_count, read_fn);
}

[[nodiscard]] inline
auto read_mipmap(ez::ui_t thread, catch_buffer_id id, double bin_size, ads::channel_idx ch, double fr) -> ads::mipmap_minmax<uint8_t> {
	return detail::read_mipmap(thread, &detail::service_, id, bin_size, ch, fr);
}

inline
auto erase(ez::nort_t thread, catch_buffer_id id) -> void {
	detail::erase(thread, &detail::service_, id);
}

inline
auto reconfigure(ez::nort_t thread, catch_buffer_id id, ads::channel_count chc, ads::frame_count frc) -> void {
	return detail::reconfigure(thread, &detail::service_, id, chc, frc);
}

inline
auto set_mipmaps_enabled(ez::nort_t thread, catch_buffer_id id, bool enabled) -> void {
	return detail::set_mipmaps_enabled(thread, &detail::service_, id, enabled);
}

// RAII catch buffer wrapper
struct catch_buffer {
	catch_buffer()                               = default;
	catch_buffer(const catch_buffer&)            = delete;
	catch_buffer& operator=(const catch_buffer&) = delete;
	catch_buffer(ads::channel_count channel_count, ads::frame_count frame_count, chain_options options, std::any client_data)
		: id_{adrian::make_catch_buffer(ez::nort, channel_count, frame_count, options, client_data)}
	{
	}
	~catch_buffer() {
		erase();
	}
	catch_buffer(catch_buffer&& rhs) noexcept : id_{rhs.id_} { rhs.id_ = {}; }
	catch_buffer& operator=(catch_buffer&& rhs) noexcept {
		erase();
		id_ = rhs.id_;
		rhs.id_ = {};
		return *this;
	}
	auto playback_start(ez::ui_t thread, ads::region region) -> void                  { adrian::playback_start(thread, id_, region); }
	auto playback_stop(ez::ui_t thread) -> void                                       { adrian::playback_stop(thread, id_); }
	auto reconfigure(ez::nort_t thread, ads::channel_count chc, ads::frame_count frc) { adrian::reconfigure(thread, id_, chc, frc); }
	auto set_mipmaps_enabled(ez::nort_t thread, bool enabled) -> void                 { adrian::set_mipmaps_enabled(thread, id_, enabled); }
	[[nodiscard]] auto get_channel_count(ez::ui_t thread) const -> ads::channel_count { return adrian::get_channel_count(thread, id_); }
	[[nodiscard]] auto get_frame_count(ez::ui_t thread) const -> ads::frame_count     { return adrian::get_frame_count(thread, id_); }
	[[nodiscard]] auto get_playback_marker(ez::ui_t thread) const -> ads::frame_idx   { return adrian::get_playback_marker(thread, id_); }
	[[nodiscard]] auto get_write_marker(ez::ui_t thread) const -> ads::frame_idx      { return adrian::get_write_marker(thread, id_); }
	[[nodiscard]] auto id() const -> catch_buffer_id                                  { return id_; }
	[[nodiscard]] auto is_record_active(ez::ui_t thread) const -> bool                { return adrian::is_record_active(thread, id_); }
	[[nodiscard]] auto is_playback_active(ez::ui_t thread) const -> bool              { return adrian::is_playback_active(thread, id_); }
	[[nodiscard]]
	auto read_mipmap(ez::ui_t thread, double bin_size, ads::channel_idx ch, double fr) const -> ads::mipmap_minmax<uint8_t> {
		return adrian::read_mipmap(thread, id_, bin_size, ch, fr);
	}
	template <uint64_t DestChs, uint64_t DestFrs>
	auto copy(ez::nort_t thread, ads::frame_idx start, ads::data<float, DestChs, DestFrs>* dest, ads::frame_idx dest_start, ads::frame_count frame_count) -> ads::frame_count {
		return adrian::copy(thread, id_, start, dest, dest_start, frame_count);
	}
	template <typename ReadFn> requires ads::concepts::is_read_fn<float, ReadFn>
	auto read(ez::nort_t thread, ads::frame_idx start, ads::frame_count frame_count, ReadFn read_fn) -> ads::frame_count {
		return adrian::read(thread, id_, start, frame_count, read_fn);
	}
	template <typename ReadFn> requires ads::concepts::is_single_channel_read_fn<float, ReadFn>
	auto read(ez::nort_t thread, ads::channel_idx ch, ads::frame_idx start, ads::frame_count frame_count, ReadFn read_fn) -> ads::frame_count {
		return adrian::read(thread, id_, ch, start, frame_count, read_fn);
	}
private:
	auto erase() -> void {
		if (id_) {
			adrian::erase(ez::nort, id_);
		}
	}
	catch_buffer_id id_;
};

} // adrian
