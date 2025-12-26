#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "adrian.hpp"
#include "doctest.h"

TEST_CASE("test test") {
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
