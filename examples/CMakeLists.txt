include(FetchContent)

FetchContent_Declare(
    NoGraphicsAPI
    GIT_REPOSITORY https://github.com/sebbbi/NoGraphicsAPI.git
    GIT_TAG        main
)
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        v1.93.0
)
FetchContent_Declare(
    RGFW
    GIT_REPOSITORY https://github.com/ColleagueRiley/RGFW.git
    GIT_TAG        main
)
FetchContent_Declare(
    imgui_impl_rgfw
    GIT_REPOSITORY https://github.com/Hedgehogsoft/imgui_impl_rgfw.h.git
    GIT_TAG        main
)
FetchContent_MakeAvailable(NoGraphicsAPI imgui RGFW imgui_impl_rgfw)

add_library(imgui)
file(GLOB IMGUI_SOURCES ${FETCHCONTENT_BASE_DIR}/imgui-src/imgui*.cpp)
file(GLOB IMGUI_HEADERS ${FETCHCONTENT_BASE_DIR}/imgui-src/imgui*.h)
target_sources(
    imgui
    PRIVATE
        ${IMGUI_SOURCES}
    PUBLIC
        FILE_SET HEADERS
		FILES
			${IMGUI_HEADERS}
        BASE_DIRS
            ${FETCHCONTENT_BASE_DIR}/imgui-src
)
target_include_directories(imgui PUBLIC ${FETCHCONTENT_BASE_DIR}/imgui-src ${FETCHCONTENT_BASE_DIR}/imgui-src/backends)

add_library(RGFW INTERFACE)
target_include_directories(RGFW INTERFACE ${FETCHCONTENT_BASE_DIR}/rgfw-src)

add_library(imgui_impl_rgfw INTERFACE)
target_include_directories(imgui_impl_rgfw INTERFACE ${FETCHCONTENT_BASE_DIR}/imgui_impl_rgfw-src)

add_library(imgui_impl_nographicsapi)
target_sources(
    imgui_impl_nographicsapi
    PRIVATE
        ${PROJECT_SOURCE_DIR}/imgui_impl_nographicsapi.cpp
    PUBLIC
        FILE_SET HEADERS
		FILES
			"${PROJECT_SOURCE_DIR}/imgui_impl_nographicsapi.h"
        BASE_DIRS
            ${PROJECT_SOURCE_DIR}
)
target_include_directories(imgui_impl_nographicsapi PUBLIC ${PROJECT_SOURCE_DIR})
target_link_libraries(imgui_impl_nographicsapi PRIVATE imgui NoGraphicsAPI)

add_subdirectory(triangle)
add_subdirectory(cube)
