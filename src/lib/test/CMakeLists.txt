# Now simply link against gtest or gtest_main as needed. Eg
# add_executable(test_command test_command.cpp)
# target_link_libraries(test_command gtest_main)
# add_test(NAME name_test_command COMMAND test_command)

add_executable(test_command test_command.cpp)
target_link_libraries(test_command CrewLib GTest::gtest_main)

add_executable(test_module test_module.cpp)
target_link_libraries(test_module CrewLib GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(test_command)
gtest_discover_tests(test_module)
