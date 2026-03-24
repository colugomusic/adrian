#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "adrian.hpp"
#include "doctest.h"
#include <vector>

TEST_CASE("basic catch buffer wraparound sanity") {
	auto options = adrian::chain_options{};
	options.allocate_now   = true;
	options.enable_mipmaps = false;
	options.silent         = false;
	auto cbuf = adrian::catch_buffer{{1}, {64}, options, {}};
	auto input0 = ml::DSPVector{};
	auto input1 = ml::DSPVector{};
	auto input2 = ml::DSPVector{};
	auto input3 = ml::DSPVector{};
	auto input4 = ml::DSPVector{};
	std::fill(input0.getBuffer() + 0 , input0.getBuffer() + 32, 1.0f);
	std::fill(input0.getBuffer() + 32, input0.getBuffer() + 64, 2.0f);
	std::fill(input1.getBuffer() + 0 , input1.getBuffer() + 32, 3.0f);
	std::fill(input1.getBuffer() + 32, input1.getBuffer() + 64, 4.0f);
	std::fill(input2.getBuffer() + 0 , input2.getBuffer() + 32, 5.0f);
	std::fill(input2.getBuffer() + 32, input2.getBuffer() + 64, 6.0f);
	std::fill(input3.getBuffer() + 0 , input3.getBuffer() + 32, 7.0f);
	std::fill(input3.getBuffer() + 32, input3.getBuffer() + 64, 8.0f);
	std::fill(input4.getBuffer() + 0 , input4.getBuffer() + 32, 9.0f);
	std::fill(input4.getBuffer() + 32, input4.getBuffer() + 64, 10.0f);
	auto output0 = ml::DSPVectorArray<2>{};
	auto output1 = ml::DSPVectorArray<2>{};
	auto output2 = ml::DSPVectorArray<2>{};
	auto output3 = ml::DSPVectorArray<2>{};
	auto output4 = ml::DSPVectorArray<2>{};
	output0 = adrian::process(ez::audio, cbuf.id(), input0, 0.0f, 1.0f);
	REQUIRE (output0 == ml::DSPVectorArray<2>{});
	cbuf.playback_start(ez::ui, {0, 64});
	adrian::update(ez::audio);
	output1 = adrian::process(ez::audio, cbuf.id(), input1, 0.0f, 1.0f);
	REQUIRE (output1.constRow(0) == input1);
	cbuf.playback_start(ez::ui, {0, 64});
	adrian::update(ez::audio);
	output2 = adrian::process(ez::audio, cbuf.id(), input2, 0.0f, 1.0f);
	REQUIRE (output2.constRow(0) == input2);
	cbuf.playback_start(ez::ui, {0, 64});
	adrian::update(ez::audio);
	output3 = adrian::process(ez::audio, cbuf.id(), input3, 0.0f, 1.0f, true);
	REQUIRE (output3.constRow(0) == input2);
	cbuf.playback_start(ez::ui, {0, 64});
	adrian::update(ez::audio);
	output4 = adrian::process(ez::audio, cbuf.id(), input4, 0.0f, 1.0f, true);
	REQUIRE (output4.constRow(0) == input2);
}

TEST_CASE("catch buffer copy") {
	auto options = adrian::chain_options{};
	options.allocate_now   = true;
	options.enable_mipmaps = false;
	options.silent         = false;
	auto cbuf = adrian::catch_buffer{{1}, {64}, options, {}};
	auto input0 = ml::DSPVector{};
	auto input1 = ml::DSPVector{};
	std::fill(input0.getBuffer() + 0 , input0.getBuffer() + 32, 1.0f);
	std::fill(input0.getBuffer() + 32, input0.getBuffer() + 64, 2.0f);
	std::fill(input1.getBuffer() + 0 , input1.getBuffer() + 32, 3.0f);
	std::fill(input1.getBuffer() + 32, input1.getBuffer() + 64, 4.0f);
	auto output0 = ml::DSPVectorArray<2>{};
	auto output1 = ml::DSPVectorArray<2>{};
	output0 = adrian::process(ez::audio, cbuf.id(), input0, 0.0f, 1.0f);
	output1 = adrian::process(ez::audio, cbuf.id(), input1, 0.0f, 1.0f);
	auto dest_buf = ads::data<float, 1, 64>{};
	auto result   = cbuf.copy(ez::ui, {0}, &dest_buf, {0}, {64});
	REQUIRE (result == 64);
	REQUIRE (dest_buf.at(ads::frame_idx{0})  == 3.0f);
	REQUIRE (dest_buf.at(ads::frame_idx{16}) == 3.0f);
	REQUIRE (dest_buf.at(ads::frame_idx{31}) == 3.0f);
	REQUIRE (dest_buf.at(ads::frame_idx{32}) == 4.0f);
	REQUIRE (dest_buf.at(ads::frame_idx{63}) == 4.0f);
}

// Test that markers wrap correctly when frame_count is not aligned to BUFFER_SIZE.
// The write marker should wrap using actual_frame_count (BUFFER_SIZE-aligned) to
// avoid landing on positions that would cause a 64-frame write to span buffer boundaries.
TEST_CASE("catch buffer with unaligned frame_count - marker wrapping") {
	auto options = adrian::chain_options{};
	options.allocate_now   = true;
	options.enable_mipmaps = false;
	options.silent         = true;
	// Use a frame_count that is NOT a multiple of BUFFER_SIZE (16384) or even 64
	// This would previously cause marker misalignment after wrapping
	// actual_frame_count will be 16384 (rounds up from 50000 / 16384 = 3.05 -> 4 buffers = 65536)
	// Wait, let's use a smaller value that still demonstrates the issue
	// frame_count = 100 -> actual = 16384
	// After 16384/64 = 256 iterations, marker wraps
	auto cbuf = adrian::catch_buffer{{1}, {100}, options, {}};
	auto input = ml::DSPVector{};
	std::fill(input.getBuffer(), input.getBuffer() + 64, 1.0f);
	// Process enough times to wrap the marker multiple times
	// actual_frame_count = 16384, so 256 * 64 = 16384 frames per cycle
	// Run 300 iterations to ensure we wrap at least once
	for (int i = 0; i < 300; ++i) {
		// This should not throw "sub-buffer region spans multiple buffers"
		std::ignore = adrian::process(ez::audio, cbuf.id(), input, 0.0f, 1.0f);
	}
	// If we get here without throwing, the test passes
	REQUIRE(true);
}

TEST_CASE("catch buffer with unaligned frame_count") {
	auto options = adrian::chain_options{};
	options.allocate_now   = true;
	options.enable_mipmaps = false;
	options.silent         = true;
	INFO("Testing frame_count = 10000");
	INFO("process");
	auto cbuf = adrian::catch_buffer{{1}, {10000}, options, {}};
	auto input = ml::DSPVector{};
	std::fill(input.getBuffer(), input.getBuffer() + 64, 1.0f);
	for (int i = 0; i < 1000; ++i) {
		std::ignore = adrian::process(ez::audio, cbuf.id(), input, 0.0f, 1.0f);
	}
	INFO("playback");
	cbuf.playback_start(ez::ui, {0, 64});
	adrian::update(ez::audio);
	for (int i = 0; i < 100; ++i) {
		cbuf.playback_start(ez::ui, {0, 64});
		adrian::update(ez::audio);
		std::ignore = adrian::process(ez::audio, cbuf.id(), input, 0.0f, 1.0f);
	}
	REQUIRE(true);
}
