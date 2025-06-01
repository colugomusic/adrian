cmake_minimum_required(VERSION 3.20)
project(adrian)
add_library(adrian INTERFACE)
add_library(adrian::adrian ALIAS adrian)
target_sources(adrian INTERFACE
	FILE_SET adrian
	TYPE HEADERS
	BASE_DIRS include
	FILES
		include/adrian.hpp
		include/adrian-allocation-thread.hpp
		include/adrian-buffer.hpp
		include/adrian-catch-buffer.hpp
		include/adrian-chain.hpp
		include/adrian-concepts.hpp
		include/adrian-ids.hpp
		include/adrian-messages.hpp
		include/adrian-model.hpp
		include/adrian-peak-gate.hpp
		include/adrian-ui-events.hpp
)
target_compile_features(adrian INTERFACE cxx_std_20)
