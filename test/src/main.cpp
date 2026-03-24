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
		auto output = adrian::process(ez::audio, cbuf.id(), input, 0.0f, 1.0f);
		(void)output;
	}
	// If we get here without throwing, the test passes
	REQUIRE(true);
}

// Test that playback works correctly after many cycles when frame_count is unaligned.
// The read marker (playback_marker) should also wrap using actual_frame_count.
TEST_CASE("catch buffer playback with unaligned frame_count") {
	auto options = adrian::chain_options{};
	options.allocate_now   = true;
	options.enable_mipmaps = false;
	options.silent         = true;
	// Use unaligned frame_count
	auto cbuf = adrian::catch_buffer{{1}, {100}, options, {}};
	auto input = ml::DSPVector{};
	std::fill(input.getBuffer(), input.getBuffer() + 64, 1.0f);
	// Record some data
	std::ignore = adrian::process(ez::audio, cbuf.id(), input, 0.0f, 1.0f);
	// Start playback and process many times to wrap the playback marker
	cbuf.playback_start(ez::ui, {0, 50});
	adrian::update(ez::audio);
	for (int i = 0; i < 300; ++i) {
		// Keep restarting playback to exercise the playback marker wrapping
		cbuf.playback_start(ez::ui, {0, 50});
		adrian::update(ez::audio);
		auto output = adrian::process(ez::audio, cbuf.id(), input, 0.0f, 1.0f);
		(void)output;
	}
	REQUIRE(true);
}

// Test the input_start_xform fix: reading should work even when read_marker
// is large (close to actual_frame_count). The % partition_size ensures
// we don't exceed actual_frame_count after get_partitioned_read_frame adds
// the partition offset.
TEST_CASE("catch buffer playback with large read marker") {
	auto options = adrian::chain_options{};
	options.allocate_now   = true;
	options.enable_mipmaps = false;
	options.silent         = true;
	// Use a larger frame_count to have more room to test
	// frame_count = 32768 -> actual = 32768 (already aligned)
	// partition_size = 16384
	auto cbuf = adrian::catch_buffer{{1}, {32768}, options, {}};
	auto input = ml::DSPVector{};
	std::fill(input.getBuffer(), input.getBuffer() + 64, 1.0f);
	// Record data to fill the buffer
	for (int i = 0; i < 512; ++i) {  // 512 * 64 = 32768 frames
		std::ignore = adrian::process(ez::audio, cbuf.id(), input, 0.0f, 1.0f);
	}
	// Start playback from near the end of the partition
	// This exercises the case where read_marker is large
	cbuf.playback_start(ez::ui, {16000, 16064});
	adrian::update(ez::audio);
	// This should not throw "sub-buffer region end exceeds actual frame count"
	auto output = adrian::process(ez::audio, cbuf.id(), input, 0.0f, 1.0f);
	(void)output;
	REQUIRE(true);
}

// Stress test: many iterations with various unaligned frame counts
TEST_CASE("catch buffer stress test with unaligned frame_count") {
	auto options = adrian::chain_options{};
	options.allocate_now   = true;
	options.enable_mipmaps = false;
	options.silent         = true;
	// Test with several unaligned frame counts
	std::vector<uint64_t> frame_counts = {100, 1000, 5000, 10000, 20000, 50000};
	for (auto fc : frame_counts) {
		INFO("Testing frame_count = " << fc);
		auto cbuf = adrian::catch_buffer{{1}, {fc}, options, {}};
		auto input = ml::DSPVector{};
		std::fill(input.getBuffer(), input.getBuffer() + 64, 1.0f);
		// Process enough to wrap multiple times
		// actual_frame_count is ceil(fc/16384)*16384
		// We want to wrap several times, so do many iterations
		for (int i = 0; i < 1000; ++i) {
			auto output = adrian::process(ez::audio, cbuf.id(), input, 0.0f, 1.0f);
			(void)output;
		}
		// Also test playback
		cbuf.playback_start(ez::ui, {0, static_cast<int64_t>(std::min(fc / 2, (uint64_t)64))});
		adrian::update(ez::audio);
		for (int i = 0; i < 100; ++i) {
		cbuf.playback_start(ez::ui, {0, static_cast<int64_t>(std::min(fc / 2, (uint64_t)64))});
			adrian::update(ez::audio);
			auto output = adrian::process(ez::audio, cbuf.id(), input, 0.0f, 1.0f);
			(void)output;
		}
	}
	REQUIRE(true);
}
