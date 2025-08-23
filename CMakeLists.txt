cmake_minimum_required(VERSION 3.20)
project(adrian)
set(ADRIAN_BUILD_TESTS   OFF CACHE BOOL "Build Adrian tests")
set(immer_BUILD_TESTS    OFF CACHE BOOL "immer: Build tests")
set(immer_BUILD_EXAMPLES OFF CACHE BOOL "immer: Build examples")
set(immer_BUILD_DOCS     OFF CACHE BOOL "immer: Build docs")
set(immer_BUILD_EXTRAS   OFF CACHE BOOL "immer: Build extras")
find_package(ads REQUIRED CONFIG)
find_package(ez REQUIRED CONFIG)
find_package(immer REQUIRED CONFIG)
find_package(madronalib REQUIRED CONFIG)
find_package(readerwriterqueue REQUIRED CONFIG)
add_library(adrian INTERFACE)
add_library(adrian::adrian ALIAS adrian)
target_sources(adrian INTERFACE
	FILE_SET HEADERS
	BASE_DIRS
		include
		extern/jthread
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
	madronalib::madronalib
	readerwriterqueue::readerwriterqueue
)
if (ADRIAN_BUILD_TESTS)
	add_subdirectory(test)
endif()
include(CMakePackageConfigHelpers)
install(TARGETS adrian EXPORT adrianTargets FILE_SET HEADERS)
install(EXPORT adrianTargets FILE adrianTargets.cmake NAMESPACE adrian:: DESTINATION lib/cmake/adrian)
configure_package_config_file(
    "${CMAKE_CURRENT_LIST_DIR}/cmake/adrianConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/adrianConfig.cmake"
    INSTALL_DESTINATION lib/cmake/adrian
)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/adrianConfig.cmake" DESTINATION lib/cmake/adrian)