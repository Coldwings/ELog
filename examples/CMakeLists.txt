set(ELOG_EXAMPLES
    basic
    multi_logger
    aggregation
    custom_type
    async_sink_skeleton
)

foreach(e ${ELOG_EXAMPLES})
    add_executable(${e} ${e}.cpp)
    target_link_libraries(${e} PRIVATE elog)
    target_compile_options(${e} PRIVATE -Wall -Wextra)
endforeach()
