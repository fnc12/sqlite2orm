add_executable(example_custom_policy main.cpp)
target_link_libraries(example_custom_policy PRIVATE sqlite2orm_lib fmt::fmt)
