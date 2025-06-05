#pragma once

#include "adrian-model.hpp"

namespace adrian::detail {

[[nodiscard]] inline
auto make_buffer_service(ads::channel_count channel_count) -> buffer::service::ptr {
	auto ptr = std::make_shared<buffer::service::model>();
	ptr->critical.storage               = ads::make<float, BUFFER_SIZE>(channel_count);
	ptr->critical.mipmap_staging_buffer = ads::make<uint8_t, BUFFER_SIZE>(channel_count);
	ptr->ui.mipmap                      = ads::mipmap<uint8_t, ads::DYNAMIC_EXTENT, BUFFER_SIZE>{channel_count, {}, {}};
	return ptr;
}

[[nodiscard]] inline
auto find_unused_buffer(const model& m, ads::channel_count channel_count) -> std::optional<buffer_idx> {
	if (const auto buffer_table = m.buffers.find(channel_count.value)) {
		for (int32_t i = 0; i < buffer_table->info.size(); i++) {
			if (!buffer_table->info[i].in_use) {
				return buffer_idx{i};
			}
		}
	}
	return std::nullopt;
}

[[nodiscard]] inline
auto get_buffer_service(const model& m, ads::channel_count channel_count, buffer_idx buffer_idx) -> buffer::service::ptr {
	return m.buffers.at(channel_count.value).service.at(buffer_idx.value);
}

inline
auto clear(ez::nort_t, model m, ads::channel_count channel_count, buffer_idx idx) -> void {
	const auto& service = get_buffer_service(m, channel_count, idx);
	service->critical.storage.fill(0.0f);
	service->ui.mipmap.clear();
}

[[nodiscard]] inline
auto find_unused_or_create_new_buffer(ez::nort_t thread, model m, ads::channel_count channel_count) -> std::tuple<model, buffer_idx> {
	if (const auto idx = find_unused_buffer(m, channel_count)) {
		clear(thread, m, channel_count, *idx);
		return std::make_tuple(std::move(m), *idx);
	}
	if (m.buffers.count(channel_count.value) == 0) {
		m.buffers = std::move(m.buffers).set(channel_count.value, {});
	}
	m.buffers = std::move(m.buffers).update(channel_count.value, [channel_count](buffer::table x){
		x.info    = x.info.push_back({});
		x.service = x.service.push_back(make_buffer_service(channel_count));
		return x;
	});
	const auto idx = buffer_idx{int32_t(m.buffers.at(channel_count.value).info.size() - 1)};
	return std::make_tuple(std::move(m), idx);
}

[[nodiscard]] inline
auto set_as_in_use(buffer::table table, buffer_idx idx) -> buffer::table {
	table.info = table.info.update(idx.value, [](buffer::info x){
		x.in_use = true;
		return x;
	});
	return table;
}

[[nodiscard]] inline
auto set_as_in_use(model m, ads::channel_count channel_count, buffer_idx idx) -> model {
	m.buffers = std::move(m.buffers).update(channel_count.value, [idx](buffer::table x){
		return set_as_in_use(std::move(x), idx);
	});
	return m;
}

[[nodiscard]] inline
auto release(model m, ads::channel_count channel_count, buffer_idx idx) -> model {
	m.buffers = std::move(m.buffers).update(channel_count.value, [idx](buffer::table x){
		x.info = std::move(x.info).update(idx.value, [](buffer::info x){
			x.in_use = false;
			return x;
		});
		return x;
	});
	return m;
}

inline
auto update_mipmap(ez::audio_t, buffer::service::model* service) -> void {
	auto& critical              = service->critical;
	auto& storage               = critical.storage;
	auto& mipmap_staging_buffer = critical.mipmap_staging_buffer;
	auto& audio                 = service->audio;
	if (audio.mipmap_dirty_region.is_empty()) {
		return;
	}
	const auto local_start = audio.mipmap_dirty_region.beg;
	const auto local_end   = audio.mipmap_dirty_region.end;
	const auto frame_count = ads::frame_count{static_cast<uint64_t>((local_end - local_start).value)};
	auto write_mipmap = [&storage](uint8_t* buffer, ads::channel_idx ch, ads::frame_idx start, ads::frame_count frame_count) -> ads::frame_count {
		for (ads::frame_idx i = {0}; i < frame_count; i++) {
			const auto storage_frame = ads::frame_idx{start.value + i.value};
			const auto storage_value = storage.at(ch, storage_frame);
			buffer[i.value] = ads::encode<uint8_t>(storage_value);
		}
		return frame_count;
	};
	const auto frames_written = mipmap_staging_buffer.write(local_start, frame_count, write_mipmap);
	assert (frames_written.value == frame_count.value);
	critical.mipmap_dirty_region = audio.mipmap_dirty_region;
	audio.mipmap_dirty_region   = {};
}

[[nodiscard]] inline
auto update_mipmap(ez::ui_t, buffer::service::model* service) -> bool {
	auto& critical = service->critical;
	auto& ui       = service->ui;
	if (critical.mipmap_dirty_region.is_empty()) {
		return false;
	}
	auto get_value_from_staging_buffer = [&critical](ads::channel_idx ch, ads::frame_idx fr) {
		return critical.mipmap_staging_buffer.at(ch, fr);
	};
	const auto beg = critical.mipmap_dirty_region.beg;
	const auto end = critical.mipmap_dirty_region.end;
	assert (beg <= end);
	const auto size = ads::frame_count{static_cast<uint64_t>((end - beg).value)};
	ui.mipmap.write(beg, size, get_value_from_staging_buffer);
	ui.mipmap.update(critical.mipmap_dirty_region);
	critical.mipmap_dirty_region = {};
	return true;
}

} // adrian::detail
