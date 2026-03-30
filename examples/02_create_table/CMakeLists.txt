add_executable(example_create_table main.cpp)
target_link_libraries(example_create_table PRIVATE sqlite2orm_lib fmt::fmt)
