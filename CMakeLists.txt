cmake_minimum_required(VERSION 3.20)
project(adrian)
set(ADRIAN_BUILD_TESTS   OFF CACHE BOOL "Build Adrian tests")
set(immer_BUILD_TESTS    OFF CACHE BOOL "immer: Build tests")
set(immer_BUILD_EXAMPLES OFF CACHE BOOL "immer: Build examples")
set(immer_BUILD_DOCS     OFF CACHE BOOL "immer: Build docs")
set(immer_BUILD_EXTRAS   OFF CACHE BOOL "immer: Build extras")
if (NOT TARGET ads::ads)
	add_subdirectory(extern/submodules/ads)
endif()
if (NOT TARGET ez::ez)
	add_subdirectory(extern/submodules/ez)
endif()
if (NOT TARGET immer)
	add_subdirectory(extern/submodules/immer)
endif()
if (NOT TARGET readerwriterqueue)
	add_subdirectory(extern/submodules/readerwriterqueue)
endif()
add_library(adrian INTERFACE)
add_library(adrian::adrian ALIAS adrian)
target_sources(adrian INTERFACE
	FILE_SET adrian
	TYPE HEADERS
	BASE_DIRS
		include
		extern/jthread
		extern/submodules/madronalib/source
		extern/submodules/madronalib/source/app
		$<$<PLATFORM_ID:Darwin>:extern/submodules/madronalib/external/sse2neon>
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
target_link_libraries(adrian INTERFACE
	ads::ads
	ez::ez
	immer
	readerwriterqueue
)
if (ADRIAN_BUILD_TESTS)
	add_subdirectory(test)
endif()