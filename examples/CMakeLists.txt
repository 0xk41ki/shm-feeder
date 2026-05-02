file(GLOB EXAMPLE_SOURCES "*.cpp")

foreach(example_source ${EXAMPLE_SOURCES})
    # Derive a target name from the filename e.g. basic_usage.cpp -> basic_usage
    get_filename_component(example_name ${example_source} NAME_WE)

    add_executable(${example_name} ${example_source})
    target_link_libraries(${example_name} PRIVATE shmfeeder)
endforeach()
