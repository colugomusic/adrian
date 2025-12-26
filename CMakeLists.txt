cmake_minimum_required(VERSION 3.20)
project(adrian)
enable_testing()
set(immer_BUILD_TESTS    OFF CACHE BOOL "immer: Build tests")
set(immer_BUILD_EXAMPLES OFF CACHE BOOL "immer: Build examples")
set(immer_BUILD_DOCS     OFF CACHE BOOL "immer: Build docs")
set(immer_BUILD_EXTRAS   OFF CACHE BOOL "immer: Build extras")
find_package(ads REQUIRED CONFIG)
find_package(ez REQUIRED CONFIG)
find_package(Immer REQUIRED CONFIG)
find_package(madronalib REQUIRED CONFIG)
find_package(readerwriterqueue REQUIRED CONFIG)
add_library(adrian INTERFACE)
add_library(adrian::adrian ALIAS adrian)
target_sources(adrian INTERFACE
	FILE_SET HEADERS
	BASE_DIRS
		include
	FILES
		include/adrian.hpp
		include/adrian-allocation-thread.hpp
		include/adrian-buffer.hpp
		include/adrian-catch-buffer.hpp
		include/adrian-chain.hpp
		include/adrian-concepts.hpp
		include/adrian-flags.hpp
		include/adrian-ids.hpp
		include/adrian-messages.hpp
		include/adrian-model.hpp
		include/adrian-peak-gate.hpp
		include/adrian-pp.hpp
		include/adrian-ui-events.hpp
		include/adrian-vocab.hpp
)
target_sources(adrian INTERFACE
	FILE_SET HEADERS
	BASE_DIRS
		${CMAKE_CURRENT_LIST_DIR}/extern/jthread
	FILES
		${CMAKE_CURRENT_LIST_DIR}/extern/jthread/jthread.hpp
		${CMAKE_CURRENT_LIST_DIR}/extern/jthread/jthread-impl.hpp
		${CMAKE_CURRENT_LIST_DIR}/extern/jthread/jthread-impl-stop-token.hpp
		${CMAKE_CURRENT_LIST_DIR}/extern/jthread/jthread-legal.txt
)
target_compile_features(adrian INTERFACE cxx_std_20)
target_link_libraries(adrian INTERFACE
	ads::ads
	ez::ez
	immer
	madronalib::madronalib
	readerwriterqueue::readerwriterqueue
)
if (BUILD_TESTING)
	add_subdirectory(test)
endif()
include(CMakePackageConfigHelpers)
install(TARGETS adrian EXPORT adrianTargets FILE_SET HEADERS DESTINATION include/adrian)
install(EXPORT adrianTargets FILE adrianTargets.cmake NAMESPACE adrian:: DESTINATION lib/cmake/adrian)
configure_package_config_file(
    "${CMAKE_CURRENT_LIST_DIR}/cmake/adrianConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/adrianConfig.cmake"
    INSTALL_DESTINATION lib/cmake/adrian
)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/adrianConfig.cmake" DESTINATION lib/cmake/adrian)
