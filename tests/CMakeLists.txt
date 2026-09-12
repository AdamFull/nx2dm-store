
add_executable(nx_store_tests
        test_store_scripting.cpp
        ../store_module.cpp
        ../store_scripting.cpp)

nx_module_registry(nx_store_tests)

target_include_directories(nx_store_tests PRIVATE
        "${NX_ENGINE_ROOT}/engine" "${NX_ENGINE_ROOT}/modules")

target_link_libraries(nx_store_tests
        PRIVATE nx::test nx::foundation nx::engine nx::compile_options)

set_target_properties(nx_store_tests PROPERTIES FOLDER "tests")
nxtest_discover_tests(nx_store_tests LABELS store)
