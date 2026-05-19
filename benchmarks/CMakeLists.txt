add_executable(bench_basic bench_basic.cpp)
target_link_libraries(bench_basic PRIVATE elog)
target_compile_options(bench_basic PRIVATE -O3 -Wall -Wextra -Wpedantic)

add_executable(bench_perf bench_perf.cpp)
target_link_libraries(bench_perf PRIVATE elog)
target_compile_options(bench_perf PRIVATE -O3 -Wall -Wextra -Wpedantic)

add_executable(bench_ablation bench_ablation.cpp)
target_link_libraries(bench_ablation PRIVATE elog)
target_compile_options(bench_ablation PRIVATE -O3 -Wall -Wextra -Wpedantic)

add_executable(bench_everyn bench_everyn.cpp)
target_link_libraries(bench_everyn PRIVATE elog)
target_compile_options(bench_everyn PRIVATE -O3 -Wall -Wextra -Wpedantic)

option(ELOG_BUILD_COMPARE
    "Build cross-library comparison benchmark (fetches spdlog and PhotonLibOS)"
    OFF)
if(ELOG_BUILD_COMPARE)
    include(FetchContent)

    # ---- spdlog ----
    FetchContent_Declare(spdlog
        GIT_REPOSITORY https://github.com/gabime/spdlog.git
        GIT_TAG        v1.14.1
        GIT_SHALLOW    TRUE)
    FetchContent_GetProperties(spdlog)
    if(NOT spdlog_POPULATED)
        FetchContent_Populate(spdlog)
    endif()

    # ---- PhotonLibOS (for ALog) ----
    set(PHOTON_BUILD_TESTING OFF CACHE BOOL "" FORCE)
    set(PHOTON_ENABLE_LIBCURL OFF CACHE BOOL "" FORCE)
    set(PHOTON_GLOBAL_INIT_OPENSSL OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(photon
        GIT_REPOSITORY https://github.com/alibaba/PhotonLibOS.git
        GIT_TAG        main
        GIT_SHALLOW    TRUE)
    FetchContent_GetProperties(photon)
    if(NOT photon_POPULATED)
        FetchContent_Populate(photon)
        add_subdirectory("${photon_SOURCE_DIR}" "${photon_BINARY_DIR}" EXCLUDE_FROM_ALL)
    endif()
    if(TARGET photon_obj)
        # Skip photon::__update_now() in alog.cpp's Prologue path so the
        # benchmark measures ALog itself, not photon's coroutine clock.
        target_compile_definitions(photon_obj PRIVATE LOG_BENCHMARK=1)
    endif()

    # ---- bench_compare itself ----
    add_executable(bench_compare bench_compare.cpp)
    target_link_libraries(bench_compare PRIVATE elog)
    target_compile_options(bench_compare PRIVATE -O3 -Wall -Wextra -Wno-pedantic)

    target_include_directories(bench_compare PRIVATE "${spdlog_SOURCE_DIR}/include")
    target_compile_definitions(bench_compare PRIVATE
        ELOG_HAVE_SPDLOG=1 SPDLOG_HEADER_ONLY=1 SPDLOG_NO_EXCEPTIONS=1)

    target_include_directories(bench_everyn PRIVATE "${spdlog_SOURCE_DIR}/include")
    target_compile_definitions(bench_everyn PRIVATE
        ELOG_HAVE_SPDLOG=1 SPDLOG_HEADER_ONLY=1 SPDLOG_NO_EXCEPTIONS=1)

    if(TARGET photon_static)
        target_include_directories(bench_compare PRIVATE
            "${photon_SOURCE_DIR}/include"
            "${photon_SOURCE_DIR}/common")
        target_link_libraries(bench_compare PRIVATE photon_static)
        target_compile_definitions(bench_compare PRIVATE ELOG_HAVE_ALOG=1 LOG_BENCHMARK=1)
    endif()
endif()
