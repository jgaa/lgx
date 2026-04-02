add_executable(lgx_unit_tests
  app_engine_tests.cpp
  file_source_tests.cpp
  filter_model_tests.cpp
  main.cpp
  page_cache_tests.cpp
  util_tests.cpp
)

target_link_libraries(lgx_unit_tests
  PRIVATE
    lgx::lib
    GTest::gtest
)

include(GoogleTest)
gtest_discover_tests(lgx_unit_tests)
