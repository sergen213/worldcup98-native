add_executable(test_byte_reader unit/test_byte_reader.cpp)
target_link_libraries(test_byte_reader PRIVATE wc98::core)
target_compile_options(test_byte_reader PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Werror>
    # Keep asserts live in Release so CI tests are real.
    $<$<CONFIG:Release>:-UNDEBUG>)

add_test(NAME byte_reader COMMAND test_byte_reader)
