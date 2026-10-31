include(FetchContent)
FetchContent_Declare(googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG v1.15.2
  GIT_SHALLOW TRUE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

include(GoogleTest)

add_executable(fc_tests
  core/test_version.cpp)
target_link_libraries(fc_tests PRIVATE fc::core GTest::gtest_main)
fc_set_warnings(fc_tests)
gtest_discover_tests(fc_tests)
