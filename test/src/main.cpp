#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "adrian.hpp"
#include "doctest.h"

TEST_CASE("test test") {
	auto options = adrian::chain_options{};
	options.allocate_now   = true;
	options.enable_mipmaps = false;
	options.silent         = false;
	auto idx = adrian::make_chain(ez::ui, ads::channel_count{2}, ads::frame_count{1024}, options, {});
	REQUIRE(bool(idx));
	adrian::erase(ez::ui, idx);
}