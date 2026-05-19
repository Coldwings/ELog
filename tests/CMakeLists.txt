include(FetchContent)

set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    googletest
    URL      https://github.com/google/googletest/archive/refs/tags/v1.14.0.tar.gz
    URL_HASH SHA256=8ad598c73ad796e0d8280b082cebd82a630d73e73cd3c70057938a6501bba5d7
)
FetchContent_MakeAvailable(googletest)

include(GoogleTest)

set(ELOG_TESTS
    test_render
    test_render_extra
    test_format
    test_lazy
    test_logger
    test_writev_atomicity
    test_rotating_sink
    test_buffered_file_sink
    test_iov_pack
    test_registry
    test_tie
    test_aggregate
)

foreach(t ${ELOG_TESTS})
    add_executable(${t} ${t}.cpp)
    target_link_libraries(${t} PRIVATE elog gtest gtest_main)
    target_compile_options(${t} PRIVATE -Wall -Wextra)
    gtest_discover_tests(${t})
endforeach()
