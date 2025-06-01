#pragma once

#include "adrian-chain.hpp"

namespace adrian::detail::allocation_thread {

namespace fn {

inline
auto work_or_stop(th::alloc_t, detail::service::model* service, std::stop_token stop) {
	return [service, stop] {
		return stop.stop_requested() || !service->model.read(th::alloc).loading_chains.empty();
	};
}

} // fn

inline
auto cancel_loading(model x, const loading_chain& lc) -> model {
	for (auto buffer_idx : lc.buffers) {
		x = release(std::move(x), lc.channel_count, buffer_idx);
	}
	x.loading_chains = x.loading_chains.take(lc.idx);
	return x;
}

inline
auto do_one_allocation(model x, loading_chain lc, const chain::model& chain) -> model {
	assert (lc.channel_count == chain.channel_count);
	const auto required_buffer_count = buffer_count(chain.frame_count);
	buffer_idx idx;
	std::tie(x, idx) = find_unused_or_create_new_buffer(ez::nort, std::move(x), chain.channel_count);
	x = set_as_in_use(std::move(x), chain.channel_count, idx);
	lc.buffers = lc.buffers.push_back(idx);
	if (lc.buffers.size() < required_buffer_count) {
		const auto load_progress = float(lc.buffers.size()) / float(required_buffer_count);
		x.loading_chains = x.loading_chains.set(lc.idx, std::move(lc));
		x = update_chain(std::move(x), lc.user, chain::fn::set_load_progress(load_progress));
		return x;
	}
	x.loading_chains = x.loading_chains.take(lc.idx);
	x = update_chain(std::move(x), lc.user, chain::fn::finish_loading(std::move(lc.buffers)));
	return x;
}

inline
auto do_one_allocation(th::alloc_t thread, detail::service::model* service) -> bool {
	if (service->model.read(thread).loading_chains.empty()) {
		return false;
	}
	service->model.update_publish(thread, [](model&& x){
		assert (!x.loading_chains.empty());
		auto lc = x.loading_chains.back();
		if (const auto c = x.chains.find(lc.user)) {
			return do_one_allocation(std::move(x), std::move(lc), *c);
		}
		else {
			// chain has been released before loading finished.
			// release any allocated buffers and abandon loading.
			return cancel_loading(std::move(x), lc);
		}
	});
	return true;
}

inline
auto work_or_stop(th::alloc_t, detail::service::model* service, std::stop_token stop) {
	return fn::work_or_stop(th::alloc, service, stop)();
}

inline
auto wait_for_work_or_stop(th::alloc_t, detail::service::model* service, std::stop_token stop) -> void {
	auto lock = std::unique_lock{service->critical.mut_allocation_thread_wait};
	service->critical.cv_allocation_thread_wait.wait(lock, fn::work_or_stop(th::alloc, service, stop));
}

inline
auto func(std::stop_token stop, detail::service::model* service) -> void {
	for (;;) {
		if (work_or_stop(th::alloc, service, stop)) {
			if (stop.stop_requested()) {
				return;
			}
			do_one_allocation(th::alloc, service);
		}
		else {
			wait_for_work_or_stop(th::alloc, service, stop);
		}
	}
}

} // adrian::detail::allocation_thread