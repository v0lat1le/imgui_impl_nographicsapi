project("imgui_impl_nographicsapi_examples_triangle")

add_executable(triangle "triangle.cpp")
target_link_libraries(triangle PRIVATE RGFW NoGraphicsAPI::NoGraphicsAPI imgui imgui_impl_rgfw imgui_impl_nographicsapi)
if(APPLE)
    target_link_libraries(triangle PRIVATE "-framework Cocoa")
endif()
