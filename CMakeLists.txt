cmake_minimum_required (VERSION 3.31)

project ("imgui_impl_nographicsapi")

option(IMGUI_IMPL_NOGRAPHICSAPI_BUILD_EXAMPLES "Build imgui_impl_nographicsapi examples" OFF)

if (IMGUI_IMPL_NOGRAPHICSAPI_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()
