#pragma once

#pragma warning(push, 0)
#include <DSP/MLDSPGens.h>
#pragma warning(pop)

namespace adrian::peak_gate {

struct channel {
	ml::LinearGlide glide;
	float peak = 0.0f;
};

struct model {
	std::vector<channel> channels;
};

[[nodiscard]] inline
auto init(model* m, ads::channel_count channel_count, float glide_time_in_samples) -> void {
	m->channels.resize(channel_count.value);
	for (auto& c : m->channels) {
		c.glide.setGlideTimeInSamples(glide_time_in_samples);
	}
}

[[nodiscard]] inline
auto process(channel* c, const ml::DSPVector& in, float threshold) -> bool {
	static constexpr auto EPSILON = 0.000001f;
	const auto low  = std::abs(ml::min(in));
	const auto high = std::abs(ml::max(in));
	auto peak = ml::max(low, high);
	if (peak > c->peak) {
		c->glide.setValue(peak);
	}
	const auto glide = c->glide(peak);
	c->peak = ml::max(glide);
	c->peak *= (c->peak >= EPSILON);
	return c->peak > threshold;
}

[[nodiscard]] inline
auto process(model* m, ads::channel_idx ch, const ml::DSPVector& in, float threshold) -> bool {
	return process(&m->channels[ch.value], in, threshold);
}

[[nodiscard]] inline
auto process(model* m, const ml::DSPVector& in, float threshold) -> bool {
	assert (m->channels.size() == 1);
	return process(m, ads::channel_idx{0}, in, threshold);
}

[[nodiscard]] inline
auto process(model* m, const ml::DSPVectorArray<2>& in, float threshold) -> bool {
	assert (m->channels.size() == 2);
	for (uint64_t i = 0; i < 2; i++) {
		if (process(m, ads::channel_idx{i}, in.constRow(static_cast<int>(i)), threshold)) {
			return true;
		}
	}
	return false;
}

} // adrian::peak_gate