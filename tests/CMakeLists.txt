
add_executable(nx_store_tests
        test_store_scripting.cpp
        ../store_module.cpp
        ../store_scripting.cpp)

nx_module_registry(nx_store_tests)

target_include_directories(nx_store_tests PRIVATE
        "${NX_ENGINE_ROOT}/engine" "${NX_ENGINE_ROOT}/modules"
        "${NX_ENGINE_ROOT}/engine/tests")

target_link_libraries(nx_store_tests
        PRIVATE nx::test nx::foundation nx::engine nx::script_luau
        nx::compile_options)

set_target_properties(nx_store_tests PROPERTIES FOLDER "tests")
nxtest_discover_tests(nx_store_tests LABELS store)

target_compile_definitions(nx_store_tests PRIVATE
        NX_MODULE_SERVICES_MANIFEST="${CMAKE_CURRENT_SOURCE_DIR}/../script-services.json")
