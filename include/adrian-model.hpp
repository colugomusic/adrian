#pragma once

#include "adrian-messages.hpp"
#include "adrian-peak-gate.hpp"
#include "adrian-pp.hpp"
#include "adrian-ui-events.hpp"
#include <ads-mipmap.hpp>
#include <ads.hpp>
#include <any>
#include <ez-extra.hpp>
#include <ez.hpp>
#include <jthread.hpp>
#pragma warning(push, 0)
#include <immer/map.hpp>
#include <immer/table.hpp>
#include <immer/vector.hpp>
#pragma warning(pop)

namespace adrian {

struct chain_options {
	bool allocate_now   = false; // Immediately allocate the entire chain (blocks the thread until done.)
	bool enable_mipmaps = false;
	bool silent         = false; // If true, don't produce any UI events.
};

} // adrian

namespace adrian::detail {

static constexpr uint64_t BUFFER_SIZE = 1 << 14;

[[nodiscard]] constexpr auto is_power_of_two(uint64_t x) -> bool { return (x & (x - 1)) == 0; }

static_assert (is_power_of_two(BUFFER_SIZE));

struct buffer_idx { DEFAULT_EQUALITY(buffer_idx); int32_t value = -1; explicit operator bool() const { return value >= 0; } };

// thread annotations --------------------------------------------------------------
namespace th {

using alloc_t = ez::nort_t;
static constexpr auto alloc = ez::nort;

} // th

// buffer --------------------------------------------------------------------------
namespace buffer::service {

struct audio {
	ads::mipmap_region mipmap_dirty_region;
};

struct critical {
	ads::data<float, ads::DYNAMIC_EXTENT, BUFFER_SIZE> storage;
	ads::data<uint8_t, ads::DYNAMIC_EXTENT, BUFFER_SIZE> mipmap_staging_buffer;
	ads::mipmap_region mipmap_dirty_region;
};

struct ui {
	ads::mipmap<uint8_t, ads::DYNAMIC_EXTENT, BUFFER_SIZE> mipmap;
};

struct model {
	service::audio audio;
	service::critical critical;
	service::ui ui;
};

using ptr = std::shared_ptr<model>;

} // buffer::service

namespace buffer {

struct info {
	bool in_use = false;
};

struct table {
	immer::vector<info> info;
	immer::vector<service::ptr> service;
};

} // buffer

// chain ---------------------------------------------------------------------------
namespace chain {

struct flags {
	DEFAULT_EQUALITY(flags);
	enum e {
		loading          = 1 << 1,
		generate_mipmaps = 1 << 2,
		silent           = 1 << 3,
	};
	int value = 0;
};

struct model {
	chain_id id;
	chain::flags flags;
	float load_progress = 0.0f;
	ads::channel_count channel_count;
	ads::frame_count frame_count;
	std::optional<immer::vector<buffer_idx>> buffers;
	std::any client_data;
};

inline
auto operator==(const model& a, const model& b) -> bool {
	return a.flags         == b.flags &&
		   a.load_progress == b.load_progress &&
		   a.channel_count == b.channel_count &&
		   a.frame_count   == b.frame_count &&
		   a.buffers       == b.buffers;
}

} // chain

// loading_chain -------------------------------------------------------------------
struct loading_chain {
	DEFAULT_EQUALITY(loading_chain);
	size_t idx;
	chain_id user;
	ads::channel_count channel_count;
	immer::vector<buffer_idx> buffers;
};

// catch buffer --------------------------------------------------------------------
namespace catch_buffer::service {

struct critical {
	std::atomic<uint64_t> write_marker    = 0;
	std::atomic<uint64_t> playback_marker = 0;
	std::atomic<bool>     record_active   = false;
};

struct audio {
	peak_gate::model peak_gate;
	ads::frame_idx   record_start;
	bool             playback_active = false;
};

struct ui {
	bool playback_active = false;
};

struct model {
	service::critical critical;
	service::audio    audio;
	service::ui       ui;
};

using ptr = std::shared_ptr<model>;

} // catch_buffer::service

namespace catch_buffer {

struct model {
	catch_buffer_id       id;
	chain_id              chain;
	adrian::chain_options chain_options;
	std::any              client_data;
	service::ptr          service;
	ads::region           playback_region;
};

} // catch_buffer

// model ---------------------------------------------------------------------------
// Buffers are grouped by channel count
using buffers        = immer::map<uint64_t, detail::buffer::table>;
using catch_buffers  = immer::table<detail::catch_buffer::model>;
using chains         = immer::table<detail::chain::model>;
using loading_chains = immer::vector<loading_chain>;

struct model {
	detail::buffers        buffers;
	detail::catch_buffers  catch_buffers;
	detail::chains         chains;
	detail::loading_chains loading_chains;
	int32_t next_id = 0;
};

namespace service {

static constexpr auto MIPMAP_AUDIO_CATCHER = ez::catcher{0};
static constexpr auto MIPMAP_UI_CATCHER     = ez::catcher{1};
using mipmap_beach_ball   = ez::beach_ball<ez::player_count{2}>;
using mipmap_player_audio = mipmap_beach_ball::player<MIPMAP_AUDIO_CATCHER.v>;
using mipmap_player_ui    = mipmap_beach_ball::player<MIPMAP_UI_CATCHER.v>;

struct beach {
	mipmap_beach_ball ball    = {MIPMAP_AUDIO_CATCHER};
	mipmap_player_audio audio = ball.make_player<MIPMAP_AUDIO_CATCHER.v>();
	mipmap_player_ui ui       = ball.make_player<MIPMAP_UI_CATCHER.v>();
};

struct critical {
	std::condition_variable cv_allocation_thread_wait;
	std::mutex mut_allocation_thread_wait;
	msg::to_ui::msg_queue msgs_to_ui;
	msg::to_audio::msg_queue msgs_to_audio;
};

struct ui {
	detail::model prev_frame;
};

struct model {
	service::beach beach;
	service::critical critical;
	service::ui ui;
	ez::sync<detail::model> model;
};

} // service

inline std::jthread allocation_thread_;
inline service::model service_;

} // adrian::detail