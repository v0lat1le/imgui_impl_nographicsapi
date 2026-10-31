cmake_minimum_required (VERSION 3.20)

project ("imgui_impl_nographicsapi")


# External libs
set(EXTERNAL_DIR ${CMAKE_CURRENT_SOURCE_DIR}/external)

add_subdirectory(${EXTERNAL_DIR}/NoGraphicsAPI)

add_library(imgui)
file(GLOB IMGUI_SOURCES ${EXTERNAL_DIR}/imgui/imgui*.cpp)
file(GLOB IMGUI_HEADERS ${EXTERNAL_DIR}/imgui/imgui*.h)
target_sources(
    imgui
    PRIVATE
        ${IMGUI_SOURCES}
    PUBLIC
        FILE_SET HEADERS
		FILES
			${IMGUI_HEADERS}
)
target_include_directories(imgui PUBLIC ${EXTERNAL_DIR}/imgui ${EXTERNAL_DIR}/imgui/backends)


add_executable (main "main.cpp" "imgui_impl_nographicsapi.cpp")
target_include_directories(main PRIVATE ${EXTERNAL_DIR}/RGFW ${EXTERNAL_DIR}/imgui_impl_rgfw)
target_link_libraries(main PRIVATE imgui NoGraphicsAPI::NoGraphicsAPI)
