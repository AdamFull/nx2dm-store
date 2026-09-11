
option(NX_MODULE_STORE "Build Store" OFF)

if (NOT NX_MODULE_STORE)
    return()
endif ()

nx_module(store
        DISPLAY "Neutral storefront interface"
        SOURCES store_module.cpp store_scripting.cpp)
