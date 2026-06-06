file(GLOB EXAMPLE_SOURCES "*.cpp")



foreach(example_source ${EXAMPLE_SOURCES})
    # Derive a target name from the filename e.g. basic_usage.cpp -> basic_usage
    get_filename_component(example_name ${example_source} NAME_WE)

    add_executable(${example_name} ${example_source})
    target_link_libraries(${example_name} PRIVATE shmfeeder)

    target_compile_options(${example_name} PRIVATE
        -Wall -Wextra -Wpedantic
        -Wshadow
        -Wconversion -Wsign-conversion
        -Wold-style-cast
        -Wnull-dereference
        -Wdouble-promotion
        -Wformat=2
        -Wimplicit-fallthrough
        -Wcast-align
    )
endforeach()
