#pragma once

#include "adrian-buffer.hpp"
#include "adrian-concepts.hpp"
#include "adrian-flags.hpp"
#pragma warning(push, 0)
#include <immer/algorithm.hpp>
#pragma warning(pop)


// TERMINOLOGY --------------------------------------------------------------------------------------------
//
// - "scary" 
//       This file does not do any multi-thread synchronization of
//       reading/writing to the same region of a chain.
//       A read or write operation is considered scary if the onus
//       of synchronization falls on the caller.
//
// - "valid sub-buffer region":
//       A 'frame start' / 'frame count' pair defining a region
//       which falls within the bounds of a single sub-buffer is
//       considered to be a "valid sub-buffer region".
//
//---------------------------------------------------------------------------------------------------------
namespace adrian::detail::chain::fn {

[[nodiscard]]
auto finish_loading(immer::vector<buffer_idx> buffers) {
	return [buffers](chain::model x){
		x.buffers       = buffers;
		x.load_progress = 1.0f;
		x.flags         = unset_flag(x.flags, x.flags.loading);
		return x;
	};
}

[[nodiscard]]
auto set_load_progress(float v) {
	return [v](chain::model x){
		x.load_progress = v;
		return x;
	};
}

} // adrian::detail::chain::fn

namespace adrian::detail {

[[nodiscard]]
auto update_chain(model x, chain_id id, auto fn) -> model {
	x.chains = std::move(x.chains).update(id, [fn](detail::chain::model x){
		return fn(std::move(x));
	});
	return x;
}

[[nodiscard]] inline
auto is_loading(const chain::model& c) -> bool {
	return is_flag_set(c.flags, c.flags.loading);
}

[[nodiscard]] inline
auto is_ready(const chain::model& c) -> bool {
	return c.buffers.has_value();
}

[[nodiscard]] inline
auto is_ready(const model& m, chain_id id) -> bool {
	return is_ready(m.chains.at(id));
}

[[nodiscard]] inline
auto should_generate_ui_events(const chain::model& c) -> bool {
	return !is_flag_set(c.flags, c.flags.silent);
}

[[nodiscard]] inline
auto should_generate_mipmaps(const chain::model& c) -> bool {
	return is_flag_set(c.flags, c.flags.generate_mipmaps);
}

[[nodiscard]] inline
auto clear(model m, chain_id id) -> model {
	m.chains = std::move(m.chains).update(id, [](chain::model x){
		x.buffers = std::nullopt;
		x.flags   = set_flag(x.flags, x.flags.loading);
		return x;
	});
	return m;
}

[[nodiscard]] inline
auto make_loading_chain(model m, adrian::chain_id chain_id, ads::channel_count channel_count) -> model {
	loading_chain lc;
	lc.idx           = m.loading_chains.size();
	lc.user          = chain_id;
	lc.channel_count = channel_count;
	m.loading_chains = std::move(m.loading_chains).push_back(std::move(lc));
	return m;
}

[[nodiscard]] inline
auto buffer_count(ads::frame_count frame_count) -> size_t {
	return (frame_count.value + BUFFER_SIZE - 1) / BUFFER_SIZE;
}

[[nodiscard]] inline
auto shrink(model m, chain_id id, size_t required_buffer_count) -> model {
	auto c = m.chains.at(id);
	assert (c.buffers);
	const auto unneeded_buffers_beg  = c.buffers->size() - required_buffer_count;
	const auto unneeded_buffers_end  = c.buffers->size();
	for (size_t i = unneeded_buffers_beg; i < unneeded_buffers_end; i++) {
		m = release(std::move(m), c.channel_count, (*c.buffers)[i]);
	}
	*c.buffers = c.buffers->take(required_buffer_count);
	m.chains = std::move(m.chains).insert(std::move(c));
	return m;
}

[[nodiscard]] inline
auto get_buffer_service(const model& m, const chain::model& chain, buffer_idx buffer_idx) -> buffer::service::ptr {
	return get_buffer_service(m, chain.channel_count, buffer_idx);
}

[[nodiscard]] inline
auto get_index_of_sub_buffer(const chain::model& chain, ads::frame_idx frame) -> buffer_idx {
	const auto sub_buffer_idx = frame.value / BUFFER_SIZE;
	return chain.buffers->at(sub_buffer_idx);
}

[[nodiscard]] inline
auto get_buffer_service(const model& m, const chain::model& chain, ads::frame_idx frame) -> buffer::service::ptr {
	return get_buffer_service(m, chain, get_index_of_sub_buffer(chain, frame));
}

[[nodiscard]] inline
auto allocate_entire_chain_now(ez::nort_t thread, model m, chain_id id) -> model {
	auto chain = m.chains.at(id);
	const auto required_buffer_count = buffer_count(chain.frame_count);
	auto buffers = immer::vector<buffer_idx>{};
	for (size_t i = 0; i < required_buffer_count; i++) {
		buffer_idx idx;
		std::tie(m, idx) = find_unused_or_create_new_buffer(thread, std::move(m), chain.channel_count);
		m = set_as_in_use(std::move(m), chain.channel_count, idx);
		buffers = buffers.push_back(idx);
	}
	chain.buffers = std::move(buffers);
	m.chains = std::move(m.chains).insert(chain);
	return m;
}

[[nodiscard]] inline
auto make_chain(ez::nort_t thread, model m, ads::channel_count channel_count, ads::frame_count frame_count, chain_options options, std::any client_data) -> std::tuple<model, chain_id> {
	chain::model chain;
	chain.id            = {++m.next_id};
	chain.flags         = set_flag(chain.flags, chain.flags.loading, !options.allocate_now);
	chain.flags         = set_flag(chain.flags, chain.flags.generate_mipmaps, options.enable_mipmaps);
	chain.flags         = set_flag(chain.flags, chain.flags.silent, options.silent);
	chain.channel_count = channel_count;
	chain.frame_count   = frame_count;
	chain.buffers       = std::nullopt;
	chain.client_data   = client_data;
	m.chains = std::move(m.chains).insert(chain);
	if (options.allocate_now) { m = allocate_entire_chain_now(thread, std::move(m), chain.id); }
	else                      { m = make_loading_chain(std::move(m), chain.id, channel_count); }
	return std::make_tuple(std::move(m), chain.id);
}

[[nodiscard]] inline
auto release_buffers(model m, chain_id id) -> model {
	if (const auto chain = m.chains.at(id); chain.buffers) {
		for (const auto buffer_idx : *chain.buffers) {
			m = release(std::move(m), chain.channel_count, buffer_idx);
		}
	}
	return m;
}

[[nodiscard]] inline
auto erase(model m, chain_id id) -> model {
	m = release_buffers(std::move(m), id);
	m.chains = std::move(m.chains).erase(id);
	return m;
}

inline
auto erase(ez::nort_t thread, service::model* service, chain_id id) -> void {
	service->model.update_publish(thread, [id](detail::model&& m){
		return erase(std::move(m), id);
	});
}

[[nodiscard]] inline
auto make_chain(ez::nort_t thread, service::model* service, ads::channel_count channel_count, ads::frame_count frame_count, chain_options options, std::any client_data) -> chain_id {
	chain_id id;
	service->model.update_publish(thread, [channel_count, frame_count, options, client_data, &id](detail::model&& m) mutable {
		std::tie(m, id) = detail::make_chain(ez::nort, std::move(m), channel_count, frame_count, options, client_data);
		return std::move(m);
	});
	return id;
}

[[nodiscard]] inline
auto resize(model m, chain_id id, ads::frame_count required_frame_count) -> model {
	const auto c = m.chains.at(id);
	const auto current_buffer_count  = buffer_count(c.frame_count);
	const auto required_buffer_count = buffer_count(required_frame_count);
	m.chains = std::move(m.chains).update(id, [required_frame_count](chain::model x){
		x.frame_count = required_frame_count;
		return x;
	});
	if (current_buffer_count == required_buffer_count) {
		return m;
	}
	if (c.buffers) {
		if (required_buffer_count < current_buffer_count) {
			m = shrink(std::move(m), id, required_buffer_count);
		}
		else {
			m = clear(std::move(m), id);
			m = make_loading_chain(std::move(m), id, c.channel_count);
		}
	}
	return m;
}

inline
auto resize(ez::nort_t thread, service::model* service, chain_id id, ads::frame_count frame_count) -> void {
	service->model.update_publish(thread, [id, frame_count](detail::model&& m){
		return resize(std::move(m), id, frame_count);
	});
}

[[nodiscard]] inline
auto grow_dirty_region(ads::mipmap_region region, ads::frame_idx start, ads::frame_idx end) -> ads::mipmap_region {
	if (start < region.beg) { region.beg = start; }
	if (end >= region.end)  { region.end = end; }
	assert (region.beg <= region.end);
	assert (region.beg <  static_cast<uint64_t>(BUFFER_SIZE));
	assert (region.end <= static_cast<uint64_t>(BUFFER_SIZE));
	return region;
}

namespace processor {

template <typename Fn>
concept chunk_input_fn = requires(Fn fn, float* chunk, ads::frame_idx start, ads::frame_count frame_count) {
	{ fn(chunk, start, frame_count) } -> std::same_as<ads::frame_count>;
};

template <typename Fn>
concept chunk_output_fn = requires(Fn fn, const float* chunk, ads::frame_idx start, ads::frame_count frame_count) {
	{ fn(chunk, start, frame_count) } -> std::same_as<ads::frame_count>;
};

template <typename Fn>
concept frame_idx_xform_fn = requires(Fn fn, ads::frame_idx fr) {
	{ fn(fr) } -> std::same_as<ads::frame_idx>;
};

struct region_alignment        { int64_t v = -1; };
struct input_region_alignment  { int64_t v = -1; };
struct output_region_alignment { int64_t v = -1; };
struct chunk_size              { size_t v; };
enum class fixed_chunk_size    { off, on };

static constexpr auto INPUT_REGION_ALIGNMENT_IGNORE  = input_region_alignment{-1};
static constexpr auto OUTPUT_REGION_ALIGNMENT_IGNORE = output_region_alignment{-1};
static constexpr auto FRAME_IDX_XFORM_IDENTITY       = [](ads::frame_idx fr) -> ads::frame_idx { return fr; };

[[nodiscard]] consteval static auto ignore(region_alignment v) -> bool  { return v.v < 0; }
[[nodiscard]] consteval static auto is_on(fixed_chunk_size v) -> bool   { return v == fixed_chunk_size::on; }

template <region_alignment REGION_ALIGNMENT, chunk_size CHUNK_SIZE> [[nodiscard]] static
auto calculate_sub_chunk_size(ads::frame_idx start, ads::frame_count frames_remaining) -> ads::frame_count {
	auto size = static_cast<uint64_t>(CHUNK_SIZE.v);
	if constexpr (!ignore(REGION_ALIGNMENT)) {
		const auto beg        = start;
		const auto end        = start + CHUNK_SIZE.v;
		const auto beg_region = beg / REGION_ALIGNMENT.v;
		const auto end_region = end / REGION_ALIGNMENT.v;
		assert (beg_region <= end_region);
		if (end_region > beg_region) {
			const auto boundary = (REGION_ALIGNMENT.v * end_region);
			assert (boundary <= end);
			const auto overflow = end - boundary;
			assert (overflow <= static_cast<int64_t>(size));
			size -= overflow.value;
		}
	}
	const auto buffer_overflow = static_cast<int64_t>(size) - static_cast<int64_t>(frames_remaining.value);
	if (buffer_overflow > 0) {
		assert (buffer_overflow <= static_cast<int64_t>(size));
		size -= buffer_overflow;
	}
	assert (size <= frames_remaining.value);
	assert (size <= CHUNK_SIZE.v);
	return ads::frame_count{size};
}

template <chunk_size CHUNK_SIZE>
struct chunk {
	std::array<float, CHUNK_SIZE.v> frames;
	float* write_pos      = frames.data();
	const float* read_pos = frames.data();
	ads::frame_count frames_written = 0;
	ads::frame_count frames_read    = 0;
	chunk() { frames.fill(0.0f); }
	auto advance_write(ads::frame_count count) {
		frames_written += count;
		write_pos      += count.value;
	}
	auto advance_read(ads::frame_count count) {
		frames_read += count;
		read_pos    += count.value;
	}
	auto is_full() const -> bool       { return frames_written == CHUNK_SIZE.v; }
	auto is_fully_read() const -> bool { return frames_read == CHUNK_SIZE.v; }
};

template <
	input_region_alignment  INPUT_REGION_ALIGNMENT,
	output_region_alignment OUTPUT_REGION_ALIGNMENT,
	chunk_size              CHUNK_SIZE,       // How big should the chunks be?
	fixed_chunk_size        FIXED_CHUNK_SIZE, // Are they always that size? (on) or can they be smaller? (off)
	chunk_input_fn          InputFn,
	chunk_output_fn         OutputFn,
	frame_idx_xform_fn      InputStartIdxXFormFn = decltype(FRAME_IDX_XFORM_IDENTITY)
>
[[nodiscard]]
auto process(ads::frame_idx input_start, ads::frame_idx output_start, ads::frame_count frame_count, InputFn input, OutputFn output, InputStartIdxXFormFn input_start_xform_fn = FRAME_IDX_XFORM_IDENTITY) -> ads::frame_count {
	if constexpr (is_on(FIXED_CHUNK_SIZE)) { assert (frame_count % CHUNK_SIZE.v == 0ULL); }
	if (frame_count == 0ULL) {
		return {0};
	}
	auto chunk                   = processor::chunk<CHUNK_SIZE>{};
	auto input_frames_remaining  = frame_count;
	auto output_frames_remaining = frame_count;
	for (;;) {
		const auto xformed_input_start  = input_start_xform_fn(input_start);
		const auto input_frames_to_read = calculate_sub_chunk_size<region_alignment{INPUT_REGION_ALIGNMENT.v}, CHUNK_SIZE>(xformed_input_start, input_frames_remaining);
		const auto input_frames_read    = input(chunk.write_pos, xformed_input_start, input_frames_to_read);
		assert (input_frames_read <= input_frames_to_read);
		assert (input_frames_read <= input_frames_remaining);
		chunk.advance_write(input_frames_read);
		input_start            += input_frames_read;
		input_frames_remaining -= input_frames_read;
		const auto couldnt_get_enough_input_frames = input_frames_read < input_frames_to_read;
		const auto should_write_output_chunk       = chunk.is_full() || couldnt_get_enough_input_frames || input_frames_remaining == 0ULL;
		if (should_write_output_chunk) {
			for (;;) {
				const auto output_frames_to_write = calculate_sub_chunk_size<region_alignment{OUTPUT_REGION_ALIGNMENT.v}, CHUNK_SIZE>(output_start, output_frames_remaining);
				const auto output_frames_written  = output(chunk.read_pos, output_start, output_frames_to_write);
				assert (output_frames_written <= output_frames_to_write);
				assert (output_frames_written <= output_frames_remaining);
				chunk.advance_read(output_frames_written);
				output_start            += output_frames_written;
				output_frames_remaining -= output_frames_written;
				const auto couldnt_write_enough_output_frames = output_frames_written < output_frames_to_write;
				if (couldnt_write_enough_output_frames) { return frame_count - output_frames_remaining; }
				if (output_frames_remaining == 0ULL)    { return frame_count - output_frames_remaining; }
				if (chunk.is_fully_read())              { break; }
			}
			chunk = {};
		}
		if (couldnt_get_enough_input_frames) { return frame_count - output_frames_remaining; }
		if (input_frames_remaining == 0ULL)  { return frame_count - output_frames_remaining; }
	}
}

} // processor

[[nodiscard]] static
auto get_actual_frame_count(const chain::model& chain) -> ads::frame_count {
	const auto buffers_required = std::ceil(static_cast<float>(chain.frame_count.value) / BUFFER_SIZE);
	return {static_cast<uint64_t>(buffers_required * BUFFER_SIZE)};
}

[[nodiscard]] static
auto is_valid_sub_buffer_region(const chain::model& chain, ads::frame_idx start, ads::frame_count frame_count) -> bool {
	return frame_count <= BUFFER_SIZE && start + frame_count <= get_actual_frame_count(chain) && start / BUFFER_SIZE == (start + frame_count - 1ULL) / BUFFER_SIZE;
}

template <typename ReadFn>
	requires ads::concepts::is_single_channel_read_fn<float, ReadFn>
auto scary_read_one_valid_sub_buffer_region(const model& m, const chain::model& chain, ads::channel_idx ch, ads::frame_idx start, ads::frame_count frame_count, ReadFn read) -> ads::frame_count {
	if (!chain.buffers) {
		return {0};
	}
	assert (ch < chain.channel_count);
	assert (is_valid_sub_buffer_region(chain, start, frame_count));
	const auto local_start     = start % BUFFER_SIZE;
	const auto& buffer_service = get_buffer_service(m, chain, start);
	auto& critical             = buffer_service->critical;
	return critical.storage.read(ch, local_start, frame_count, read);
}

template <typename ReadFn> [[nodiscard]]
auto scary_read_random(const model& m, const chain::model& chain, const std::array<ads::frame_idx, kFloatsPerDSPVector>& frames, ReadFn read_fn) -> void {
	if (!chain.buffers) {
		return;
	}
	const auto frame_count = get_actual_frame_count(chain);
	for (auto ch = ads::channel_idx{}; ch < chain.channel_count; ch++) {
		ads::frame_idx frame_counter;
		for (auto fr : frames) {
			if (fr < 0 || fr >= frame_count) {
				read_fn(0.0f, ch, frame_counter++);
				continue;
			}
			const auto local_frame     = fr % BUFFER_SIZE;
			const auto& buffer_service = get_buffer_service(m, chain, fr);
			const auto& critical       = buffer_service->critical;
			read_fn(critical.storage.at(ch, local_frame), ch, frame_counter++);
		}
	}
}

template <size_t CHUNK_SIZE, typename ReadFn>
	requires ads::concepts::is_single_channel_read_fn<float, ReadFn>
[[nodiscard]]
auto scary_read(const model& m, const chain::model& chain, ads::channel_idx ch, ads::frame_idx start, ads::frame_count frame_count, ReadFn read) -> ads::frame_count {
	auto input = [&](float* chunk, ads::frame_idx start, ads::frame_count frame_count) {
		auto transfer = [&](const float* buffer, ads::frame_idx start, ads::frame_count frame_count) {
			std::copy(buffer, buffer + frame_count.value, chunk);
			return frame_count;
		};
		return scary_read_one_valid_sub_buffer_region(m, chain, ch, start, frame_count, transfer);
	};
	auto output = [read](const float* chunk, ads::frame_idx start, ads::frame_count frame_count) {
		return read(chunk, start, frame_count);
	};
	static constexpr auto input_region_alignment  = processor::input_region_alignment{BUFFER_SIZE};
	static constexpr auto output_region_alignment = processor::OUTPUT_REGION_ALIGNMENT_IGNORE;
	static constexpr auto chunk_size              = processor::chunk_size{CHUNK_SIZE};
	static constexpr auto fixed_chunk_size        = processor::fixed_chunk_size::off;
	return processor::process<input_region_alignment, output_region_alignment, chunk_size, fixed_chunk_size>(start, start, frame_count, input, output);
}

template <size_t CHUNK_SIZE, typename ReadFn>
	requires ads::concepts::is_multi_channel_read_fn<float, ReadFn>
[[nodiscard]]
auto scary_read(const model& m, const chain::model& chain, ads::frame_idx start, ads::frame_count frame_count, ReadFn read) -> ads::frame_count {
	for (auto ch = ads::channel_idx{}; ch < chain.channel_count; ch++) {
		auto adapter = [read, ch](const float* buffer, ads::frame_idx start, ads::frame_count frame_count) {
			return read(buffer, ch, start, frame_count);
		};
		const auto frames_read = scary_read<CHUNK_SIZE>(m, chain, ch, start, frame_count, adapter);
		assert (frames_read == frame_count);
	}
	return frame_count;
}

template <typename WriteFn>
	requires ads::concepts::is_write_fn<float, WriteFn>
auto scary_write_one_valid_sub_buffer_region(const model& m, const chain::model& chain, ads::frame_idx start, ads::frame_count frame_count, WriteFn write) -> ads::frame_count {
	if (!chain.buffers) {
		return {0};
	}
	assert (is_valid_sub_buffer_region(chain, start, frame_count));
	const auto local_start      = ads::frame_idx{start.value % BUFFER_SIZE};
	const auto local_end        = local_start + frame_count;
	const auto& buffer_service  = get_buffer_service(m, chain, start);
	auto& critical              = buffer_service->critical;
	auto& storage               = buffer_service->critical.storage;
	auto& audio                 = buffer_service->audio;
	audio.mipmap_dirty_region = grow_dirty_region(audio.mipmap_dirty_region, local_start, local_end);
	const auto frames_written = storage.write(local_start, frame_count, write);
	assert (frames_written.value == frame_count.value);
	return frames_written;
}

template <typename ProviderFn>
	requires ads::concepts::is_multi_channel_provider_fn<float, ProviderFn>
auto scary_write_random(const model& m, const chain::model& chain, const std::array<ads::frame_idx, kFloatsPerDSPVector>& frames, ProviderFn provider_fn) -> void {
	namespace bc = boost::container;
	if (!chain.buffers) {
		return;
	}
	const auto frame_count = get_actual_frame_count(chain);
	for (auto ch = ads::channel_idx{}; ch < chain.channel_count; ch++) {
		auto frame_counter = ads::frame_idx{0};
		for (auto fr : frames) {
			if (fr < 0 || fr >= frame_count) {
				frame_counter++;
				continue;
			}
			const auto local_frame     = fr % BUFFER_SIZE;
			const auto buffer          = get_index_of_sub_buffer(chain, fr);
			const auto& buffer_service = get_buffer_service(m, chain, buffer);
			auto& critical             = buffer_service->critical;
			auto& audio                = buffer_service->audio;
			critical.storage.set(ch, local_frame, provider_fn(ch, frame_counter++));
			audio.mipmap_dirty_region = grow_dirty_region(audio.mipmap_dirty_region, local_frame, local_frame + 1ULL);
		}
	}
}

template <size_t CHUNK_SIZE, typename WriteFn>
	requires ads::concepts::is_single_channel_write_fn<float, WriteFn>
[[nodiscard]]
auto scary_write(const model& m, const chain::model& chain, ads::channel_idx ch, ads::frame_idx start, ads::frame_count frame_count, WriteFn write) -> ads::frame_count {
	ads::frame_count frames_remaining = frame_count;
	auto input = [write](float* chunk, ads::frame_idx start, ads::frame_count frame_count) {
		return write(chunk, start, frame_count);
	};
	auto output = [&](const float* chunk, ads::frame_idx start, ads::frame_count frame_count) {
		auto transfer = [&](float* buffer, ads::frame_idx start, ads::frame_count frame_count) {
			std::copy(chunk, chunk + frame_count.value, buffer);
			return frame_count;
		};
		return scary_write_one_valid_sub_buffer_region(m, chain, start, frame_count, transfer);
	};
	static constexpr auto input_region_alignment  = processor::INPUT_REGION_ALIGNMENT_IGNORE;
	static constexpr auto output_region_alignment = processor::output_region_alignment{BUFFER_SIZE};
	static constexpr auto chunk_size              = processor::chunk_size{CHUNK_SIZE};
	static constexpr auto fixed_chunk_size        = processor::fixed_chunk_size::off;
	return processor::process<input_region_alignment, output_region_alignment, chunk_size, fixed_chunk_size>(start, start, frame_count, input, output);
}

template <size_t CHUNK_SIZE, typename WriteFn>
	requires ads::concepts::is_multi_channel_write_fn<float, WriteFn>
[[nodiscard]]
auto scary_write(const model& m, const chain::model& chain, ads::frame_idx start, ads::frame_count frame_count, WriteFn write) -> ads::frame_count {
	for (auto ch = ads::channel_idx{}; ch < chain.channel_count; ch++) {
		auto adapter = [write, ch](float* buffer, ads::frame_idx start, ads::frame_count frame_count) {
			return write(buffer, ch, start, frame_count);
		};
		const auto frames_written = scary_write<CHUNK_SIZE>(m, chain, ch, start, frame_count, adapter);
		assert (frames_written == frame_count);
	}
	return frame_count;
}

template <typename ReadFn> [[nodiscard]]
auto scary_read_one_valid_sub_buffer_region(const model& m, chain_id id, ads::channel_idx ch, ads::frame_idx start, ads::frame_count frame_count, ReadFn read_fn) -> void {
	return scary_read_one_valid_sub_buffer_region(m, m.chains.at(id), ch, start, frame_count, read_fn);
}

template <typename ReadFn> [[nodiscard]]
auto scary_read_random(const model& m, chain_id id, const std::array<ads::frame_idx, kFloatsPerDSPVector>& frames, ReadFn read_fn) -> void {
	return scary_read_random(m, m.chains.at(id), frames, read_fn);
}

template <size_t CHUNK_SIZE, typename ReadFn>
	requires ads::concepts::is_single_channel_read_fn<float, ReadFn>
auto scary_read(const model& m, chain_id id, ads::channel_idx ch, ads::frame_idx start, ads::frame_count frame_count, ReadFn read) -> ads::frame_count {
	return scary_read<CHUNK_SIZE>(m, m.chains.at(id), ch, start, frame_count, read);
}

template <size_t CHUNK_SIZE, typename ReadFn>
	requires ads::concepts::is_multi_channel_read_fn<float, ReadFn>
auto scary_read(const model& m, chain_id id, ads::frame_idx start, ads::frame_count frame_count, ReadFn read) -> ads::frame_count {
	return scary_read<CHUNK_SIZE>(m, m.chains.at(id), start, frame_count, read);
}

template <typename WriteFn>
auto scary_write_one_valid_sub_buffer_region(const model& m, chain_id id, ads::frame_idx start, ads::frame_count frame_count, WriteFn write) -> ads::frame_count {
	return scary_write_one_valid_sub_buffer_region(m, m.chains.at(id), start, frame_count, write);
}

template <typename WriteFn>
auto scary_write_random(const model& m, chain_id id, const std::array<ads::frame_idx, kFloatsPerDSPVector>& frames, WriteFn write) -> void {
	return scary_write_random(m, m.chains.at(id), frames, write);
}

template <size_t CHUNK_SIZE, typename WriteFn>
	requires ads::concepts::is_single_channel_write_fn<float, WriteFn>
auto scary_write(const model& m, chain_id id, ads::channel_idx ch, ads::frame_idx start, ads::frame_count frame_count, WriteFn write) -> ads::frame_count {
	return scary_write<CHUNK_SIZE>(m, m.chains.at(id), ch, start, frame_count, write);
}

template <size_t CHUNK_SIZE, typename WriteFn>
	requires ads::concepts::is_multi_channel_write_fn<float, WriteFn>
auto scary_write(const model& m, chain_id id, ads::frame_idx start, ads::frame_count frame_count, WriteFn write) -> ads::frame_count {
	return scary_write<CHUNK_SIZE>(m, m.chains.at(id), start, frame_count, write);
}

template <typename ReadFn>
auto scary_read_one_valid_sub_buffer_region(ez::audio_t thread, service::model* service, chain_id id, ads::channel_idx ch, ads::frame_idx start, ads::frame_count frame_count, ReadFn read_fn) -> void {
	return scary_read_one_valid_sub_buffer_region(*service->model.read(thread), id, ch, start, frame_count, read_fn);
}

template <typename ReadFn>
auto scary_read_random(ez::audio_t thread, service::model* service, chain_id id, const std::array<ads::frame_idx, kFloatsPerDSPVector>& frames, ReadFn read_fn) -> void {
	return scary_read_random(*service->model.read(thread), id, frames, read_fn);
}

template <size_t CHUNK_SIZE, typename ReadFn>
	requires ads::concepts::is_single_channel_read_fn<float, ReadFn>
auto scary_read(ez::audio_t thread, service::model* service, chain_id id, ads::channel_idx ch, ads::frame_idx start, ads::frame_count frame_count, ReadFn read) -> ads::frame_count {
	return scary_read<CHUNK_SIZE>(*service->model.read(thread), id, ch, start, frame_count, read);
}

template <size_t CHUNK_SIZE, typename ReadFn>
	requires ads::concepts::is_multi_channel_read_fn<float, ReadFn>
auto scary_read(ez::audio_t thread, service::model* service, chain_id id, ads::frame_idx start, ads::frame_count frame_count, ReadFn read) -> ads::frame_count {
	return scary_read<CHUNK_SIZE>(*service->model.read(thread), id, start, frame_count, read);
}

template <typename WriteFn>
auto scary_write_one_valid_sub_buffer_region(ez::audio_t thread, service::model* service, chain_id id, ads::frame_idx start, ads::frame_count frame_count, WriteFn write) -> ads::frame_count {
	return scary_write_one_valid_sub_buffer_region(*service->model.read(thread), id, start, frame_count, write);
}

template <typename WriteFn>
auto scary_write_random(ez::audio_t thread, service::model* service, chain_id id, const std::array<ads::frame_idx, kFloatsPerDSPVector>& frames, WriteFn write) -> void {
	return scary_write_random(*service->model.read(thread), id, frames, write);
}

template <size_t CHUNK_SIZE, typename WriteFn>
	requires ads::concepts::is_single_channel_write_fn<float, WriteFn>
auto scary_write(ez::audio_t thread, service::model* service, chain_id id, ads::channel_idx ch, ads::frame_idx start, ads::frame_count frame_count, WriteFn write) -> ads::frame_count {
	return scary_write<CHUNK_SIZE>(*service->model.read(thread), id, ch, start, frame_count, write);
}

template <size_t CHUNK_SIZE, typename WriteFn>
	requires ads::concepts::is_multi_channel_write_fn<float, WriteFn>
auto scary_write(ez::audio_t thread, service::model* service, chain_id id, ads::frame_idx start, ads::frame_count frame_count, WriteFn write) -> ads::frame_count {
	return scary_write<CHUNK_SIZE>(*service->model.read(thread), id, start, frame_count, write);
}

inline
auto diff(ez::ui_t thread, chains was, chains now, concepts::push_ui_event auto push_ui_event) -> void {
	auto on_added = [push_ui_event](const chain::model& v) {
		if (should_generate_ui_events(v) && is_loading(v)) {
			push_ui_event(ui::events::chain::load_begin{v.id, v.client_data});
		}
	};
	auto on_erased = [push_ui_event](const chain::model& v) {
		if (should_generate_ui_events(v) && is_loading(v)) {
			push_ui_event(ui::events::chain::load_end{v.id, v.client_data});
		}
	};
	auto on_changed = [push_ui_event](const chain::model& was, const chain::model& now) {
		if (should_generate_ui_events(now)) {
			const auto loading = flag_diff(was.flags, now.flags, now.flags.loading);
			if (was.load_progress != now.load_progress) {
				push_ui_event(ui::events::chain::load_progress{now.id, now.load_progress, now.client_data});
			}
			if (loading.was != loading.now) {
				if (loading.now) { push_ui_event(ui::events::chain::load_begin{now.id, now.client_data}); }
				else             { push_ui_event(ui::events::chain::load_end{was.id, was.client_data}); }
			}
		}
	};
	immer::diff(was, now, immer::make_differ(on_added, on_erased, on_changed));
}

[[nodiscard]] inline
auto set_mipmaps_enabled(model&& m, chain_id id, bool enabled) -> model {
	m.chains = std::move(m.chains).update(id, [enabled](detail::chain::model x){
		x.flags = set_flag(x.flags, x.flags.generate_mipmaps, enabled);
		return x;
	});
	return m;
}

inline
auto set_mipmaps_enabled(ez::nort_t thread, service::model* service, chain_id id, bool enabled) -> void {
	service->model.update_publish(thread, [id, enabled](detail::model x){
		return set_mipmaps_enabled(std::move(x), id, enabled);
	});
}

[[nodiscard]] inline
auto read_mipmap(const model& m, chain_id id, float bin_size, ads::channel_idx ch, float fr) -> ads::mipmap_minmax<uint8_t> {
	if (fr < 0.0f) { return {}; }
	const auto& chain = m.chains.at(id);
	if (!chain.buffers) { return {}; }
	const auto index_a = static_cast<uint64_t>(std::floor(fr));
	const auto index_b = static_cast<uint64_t>(std::ceil(fr));
	const auto t       = fr - index_a;
	auto buffer_index_a = detail::buffer_idx{static_cast<int32_t>(index_a / detail::BUFFER_SIZE)};
	auto buffer_index_b = detail::buffer_idx{static_cast<int32_t>(index_b / detail::BUFFER_SIZE)};
	buffer_index_a            = chain.buffers->at(buffer_index_a.value);
	buffer_index_b            = chain.buffers->at(buffer_index_b.value);
	const auto local_frame_a  = ads::frame_idx{index_a % detail::BUFFER_SIZE};
	const auto local_frame_b  = ads::frame_idx{index_b % detail::BUFFER_SIZE};
	auto service_a = detail::get_buffer_service(m, chain, buffer_index_a);
	auto service_b = detail::get_buffer_service(m, chain, buffer_index_b);
	auto& ui_a = service_a->ui;
	auto& ui_b = service_b->ui;
	auto lod_a = ui_a.mipmap.bin_size_to_lod(bin_size);
	auto lod_b = ui_b.mipmap.bin_size_to_lod(bin_size);
	const auto value_a = ui_a.mipmap.read(lod_a, ch, local_frame_a);
	const auto value_b = ui_b.mipmap.read(lod_b, ch, local_frame_b);
	return ads::lerp(value_a, value_b, t);
}

[[nodiscard]] inline
auto update_mipmap(ez::ui_t thread, const model& m, const chain::model& chain) -> bool {
	if (!should_generate_mipmaps(chain)) { return false; }
	if (!chain.buffers)                  { return false; }
	const auto services = m.buffers.at(chain.channel_count.value).service;
	bool mipmap_changed = false;
	for (const auto buffer_idx : *chain.buffers) {
		mipmap_changed |= update_mipmap(thread, services.at(buffer_idx.value).get());
	}
	return mipmap_changed;
}

} // adrian::detail

// public interface ----------------------------------------------------------------
namespace adrian {

inline
auto clear_mipmap(ez::ui_t thread, chain_id id) -> void {
	const auto& model = detail::service_.model.read(thread);
	const auto& chain = model.chains.at(id);
	if (!chain.buffers) {
		return;
	}
	for (const auto buffer_idx : *chain.buffers) {
		const auto& buffer_service = get_buffer_service(model, chain, buffer_idx);
		buffer_service->ui.mipmap.clear();
	}
}

[[nodiscard]] inline
auto get_frame_count(ez::ui_t thread, chain_id id) -> ads::frame_count {
	const auto& model = detail::service_.model.read(thread);
	const auto& chain = model.chains.at(id);
	return chain.frame_count;
}

[[nodiscard]] inline
auto is_ready(ez::ui_t thread, chain_id id) -> bool {
	return detail::is_ready(detail::service_.model.read(thread), id);
}

[[nodiscard]] inline
auto make_chain(ez::nort_t thread, ads::channel_count channel_count, ads::frame_count frame_count, chain_options options, std::any client_data) -> chain_id {
	return detail::make_chain(thread, &detail::service_, channel_count, frame_count, options, client_data);
}

[[nodiscard]] inline
auto read_mipmap(ez::ui_t thread, chain_id id, float bin_size, ads::channel_idx ch, float fr) -> ads::mipmap_minmax<uint8_t> {
	return detail::read_mipmap(detail::service_.model.read(thread), id, bin_size, ch, fr);
}

inline
auto erase(ez::nort_t thread, chain_id id) -> void {
	detail::erase(thread, &detail::service_, id);
}

inline
auto resize(ez::nort_t thread, chain_id id, ads::frame_count frame_count) -> void {
	detail::resize(thread, &detail::service_, id, frame_count);
}

// Unsychronized buffer read.
// - You may only read from a part of the buffer which is
//   not currently being written to. It is the caller's
//   responsibility to coordinate this.
// - The read region must be within the bounds of a single
//   sub-buffer.
// - If the chain has not been fully allocated yet then
//   this is a no-op and zero is returned.
template <typename ReadFn>
	requires ads::concepts::is_read_fn<float, ReadFn>
auto scary_read_one_valid_sub_buffer_region(ez::rt_t thread, chain_id id, ads::frame_idx start, ads::frame_count frame_count, ReadFn read) -> ads::frame_count {
	return detail::scary_read_one_valid_sub_buffer_region(thread, &detail::service_, id, start, frame_count, read);
}

// Unsychronized buffer write.
// - You may only read from a part of the buffer which is
//   not currently being written to. It is the caller's
//   responsibility to coordinate this.
// - The write region must be within the bounds of a single
//   sub-buffer.
// - Only one simultaneous writer is supported.
// - If the chain has not been fully allocated yet then
//   this is a no-op and zero is returned.
template <typename WriteFn>
	requires ads::concepts::is_write_fn<float, WriteFn>
auto scary_write_one_valid_sub_buffer_region(ez::rt_t thread, chain_id id, ads::frame_idx start, ads::frame_count frame_count, WriteFn write) -> ads::frame_count {
	return detail::scary_write_one_valid_sub_buffer_region(thread, &detail::service_, id, start, frame_count, write);
}

// Read "random" frames.
// The frames to read are specified by the frames array.
// The frames can be in any order. This is less efficient than the other
// read functions but is the most flexible.
// The worst-case scenario is that the provided frames are in a completely
// random order.
template <typename ReadFn>
auto scary_read_random(ez::rt_t thread, chain_id id, const std::array<ads::frame_idx, kFloatsPerDSPVector>& frames, ReadFn read_fn) -> void {
	return detail::scary_read_random(thread, &detail::service_, id, frames, read_fn);
}

// Write "random" frames.
// The frames to write are specified by the frames array.
// The frames can be in any order. This is less efficient than the other
// write functions but is the most flexible.
// The worst-case scenario is that the provided frames are in a completely
// random order.
template <typename ProviderFn>
auto scary_write_random(ez::audio_t thread, chain_id id, const std::array<ads::frame_idx, kFloatsPerDSPVector>& frames, ProviderFn provider_fn) -> void {
	return detail::scary_write_random(thread, &detail::service_, id, frames, provider_fn);
}

// Read a region of the buffer which may not necessarily fall
// within the bounds of a single sub-buffer.
// Reads will happen in chunks of <= MAX_CHUNK_SIZE.
template <size_t MAX_CHUNK_SIZE, typename ReadFn>
	requires ads::concepts::is_read_fn<float, ReadFn>
auto scary_read(ez::rt_t thread, chain_id id, ads::frame_idx start, ads::frame_count frame_count, ads::frame_count chunk_size, ReadFn read) -> ads::frame_count {
	return detail::scary_read<MAX_CHUNK_SIZE>(thread, &detail::service_, id, start, frame_count, chunk_size, read);
}

// Write a region of the buffer which may not necessarily fall
// within the bounds of a single sub-buffer.
// Writes will happen in chunks of <= MAX_CHUNK_SIZE.
template <size_t MAX_CHUNK_SIZE, typename WriteFn>
	requires ads::concepts::is_write_fn<float, WriteFn>
auto scary_write(ez::rt_t thread, chain_id id, ads::frame_idx start, ads::frame_count frame_count, ads::frame_count chunk_size, WriteFn write) -> ads::frame_count {
	return detail::scary_write<MAX_CHUNK_SIZE>(thread, &detail::service_, id, start, frame_count, chunk_size, write);
}

inline
auto set_mipmaps_enabled(ez::nort_t thread, chain_id id, bool enabled) -> void {
	detail::set_mipmaps_enabled(thread, &detail::service_, id, enabled);
}

// RAII chain wrapper
struct chain {
	chain()                        = default;
	chain(const chain&)            = delete;
	chain& operator=(const chain&) = delete;
	chain(ads::channel_count channel_count, ads::frame_count frame_count, chain_options options, std::any client_data)
		: id_{adrian::make_chain(ez::nort, channel_count, frame_count, options, client_data)}
	{
	}
	~chain() {
		erase();
	}
	chain(chain&& rhs) noexcept : id_{rhs.id_} { rhs.id_ = {}; }
	chain& operator=(chain&& rhs) noexcept {
		erase();
		id_ = rhs.id_;
		rhs.id_ = {};
		return *this;
	}
	auto clear_mipmap(ez::ui_t thread) -> void {
		adrian::clear_mipmap(thread, id_);
	}
	auto is_ready(ez::ui_t thread) -> bool                                           { return adrian::is_ready(thread, id_); }
	auto resize(ez::nort_t thread, ads::frame_count frame_count) -> void             { return adrian::resize(thread, id_, frame_count); }
	auto read_mipmap(ez::ui_t thread, float bin_size, ads::channel_idx ch, float fr) { return adrian::read_mipmap(thread, id_, bin_size, ch, fr); }
	auto set_mipmaps_enabled(ez::nort_t thread, bool enabled) -> void                { return adrian::set_mipmaps_enabled(thread, id_, enabled); }
	[[nodiscard]] auto get_frame_count(ez::ui_t thread) const                        { return adrian::get_frame_count(thread, id_); }
	[[nodiscard]] auto id() const -> chain_id                                        { return id_; }
	template <typename ReadFn>
	auto scary_read_random(ez::rt_t thread, const std::array<ads::frame_idx, kFloatsPerDSPVector>& frames, ReadFn read_fn) -> void {
		return adrian::scary_read_random(thread, id_, frames, read_fn);
	}
	template <typename ProviderFn>
		requires ads::concepts::is_multi_channel_provider_fn<float, ProviderFn>
	auto scary_write_random(ez::rt_t thread, const std::array<ads::frame_idx, kFloatsPerDSPVector>& frames, ProviderFn provider_fn) -> void {
		return adrian::scary_write_random(thread, id_, frames, provider_fn);
	}
	template <typename ReadFn>
		requires ads::concepts::is_read_fn<float, ReadFn>
	auto scary_read_one_valid_sub_buffer_region(ez::rt_t thread, ads::frame_idx start, ads::frame_count frame_count, ReadFn read) -> ads::frame_count {
		return adrian::scary_read_one_valid_sub_buffer_region(thread, id_, start, frame_count, read);
	}
	template <typename WriteFn>
		requires ads::concepts::is_write_fn<float, WriteFn>
	auto scary_write_one_valid_sub_buffer_region(ez::rt_t thread, ads::frame_idx start, ads::frame_count frame_count, WriteFn write) -> ads::frame_count {
		return adrian::scary_write_one_valid_sub_buffer_region(thread, id_, start, frame_count, write);
	}
	template <typename ReadFn>
		requires ads::concepts::is_read_fn<float, ReadFn>
	auto scary_read(ez::rt_t thread, ads::frame_idx start, ads::frame_count frame_count, ads::frame_count chunk_size, ReadFn read) -> ads::frame_count {
		return adrian::scary_read(thread, id_, start, frame_count, chunk_size, read);
	}
	template <typename WriteFn>
		requires ads::concepts::is_write_fn<float, WriteFn>
	auto scary_write(ez::rt_t thread, ads::frame_idx start, ads::frame_count frame_count, ads::frame_count chunk_size, WriteFn write) -> ads::frame_count {
		return adrian::scary_write(thread, id_, start, frame_count, chunk_size, write);
	}
private:
	auto erase() -> void {
		if (id_) {
			adrian::erase(ez::nort, id_);
		}
	}
	chain_id id_;
};

} // adrian