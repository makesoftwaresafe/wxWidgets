#############################################################################
# Name:        build/cmake/lib/webview/CMakeLists.txt
# Purpose:     CMake file for webview library
# Author:      Tobias Taschner
# Created:     2016-10-03
# Copyright:   (c) 2016 wxWidgets development team
# Licence:     wxWindows licence
#############################################################################

macro(wx_set_webview2_arch)
    if(wxPLATFORM_ARCH)
        set(WEBVIEW2_ARCH ${wxPLATFORM_ARCH})
    else()
        set(WEBVIEW2_ARCH x86)
    endif()
endmacro()

function(wx_webview_copy_webview2_loader target)
    if(NOT WXMSW OR NOT wxUSE_WEBVIEW_EDGE OR NOT TARGET ${target})
        return()
    endif()

    wx_set_webview2_arch()
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy
        "${WEBVIEW2_PACKAGE_DIR}/build/native/${WEBVIEW2_ARCH}/WebView2Loader.dll"
        "$<TARGET_FILE_DIR:${target}>/WebView2Loader.dll")
endfunction()


wx_append_sources(WEBVIEW_FILES WEBVIEW_CMN)
if(WXMSW)
    wx_append_sources(WEBVIEW_FILES WEBVIEW_MSW)
elseif(WXGTK)
    wx_append_sources(WEBVIEW_FILES WEBVIEW_GTK)
elseif(APPLE)
    wx_append_sources(WEBVIEW_FILES WEBVIEW_OSX_SHARED)
endif()

wx_add_library(wxwebview ${WEBVIEW_FILES})

if(APPLE)
    wx_lib_link_libraries(wxwebview PUBLIC "-framework WebKit")
elseif(WXMSW)
    if(wxUSE_WEBVIEW_EDGE)
        # Update the following variables if updating WebView2 SDK
        set(WEBVIEW2_VERSION "1.0.1722.45")
        set(WEBVIEW2_URL "https://www.nuget.org/api/v2/package/Microsoft.Web.WebView2/${WEBVIEW2_VERSION}")
        set(WEBVIEW2_SHA256 "10d78e9e11c7851f0a8bf0dd4c22bbf6383de45bd337e1be42240b103812947d")

        set(WEBVIEW2_DEFAULT_PACKAGE_DIR "${CMAKE_CURRENT_BINARY_DIR}/packages/Microsoft.Web.WebView2.${WEBVIEW2_VERSION}")

        if(NOT EXISTS ${WEBVIEW2_PACKAGE_DIR})
            unset(WEBVIEW2_PACKAGE_DIR CACHE)
        endif()
        find_path(WEBVIEW2_PACKAGE_DIR
            NAMES build/native/include/WebView2.h
            PATHS
                "${PROJECT_SOURCE_DIR}/3rdparty/webview2"
                ${WEBVIEW2_DEFAULT_PACKAGE_DIR}
        )

        if (NOT WEBVIEW2_PACKAGE_DIR)
            message(STATUS "WebView2 SDK not found locally, downloading...")
            set(WEBVIEW2_PACKAGE_DIR ${WEBVIEW2_DEFAULT_PACKAGE_DIR} CACHE PATH "WebView2 SDK PATH" FORCE)
            file(DOWNLOAD
                ${WEBVIEW2_URL}
                ${CMAKE_CURRENT_BINARY_DIR}/webview2.nuget
                EXPECTED_HASH SHA256=${WEBVIEW2_SHA256}
                STATUS DOWNLOAD_RESULT)
            # Check that the download was successful.
            list(GET DOWNLOAD_RESULT 0 DOWNLOAD_RESULT_NUM)
            if(NOT ${DOWNLOAD_RESULT_NUM} EQUAL 0)
                list(GET DOWNLOAD_RESULT 1 DOWNLOAD_RESULT_STR)
                message(FATAL_ERROR "Error ${DOWNLOAD_RESULT_NUM} downloading WebView2 SDK: ${DOWNLOAD_RESULT_STR}.")
            endif()
            file(MAKE_DIRECTORY ${WEBVIEW2_PACKAGE_DIR})
            execute_process(COMMAND
                "${CMAKE_COMMAND}" -E tar x "${CMAKE_CURRENT_BINARY_DIR}/webview2.nuget"
                WORKING_DIRECTORY "${WEBVIEW2_PACKAGE_DIR}"
            )
        endif()
        set(WEBVIEW2_PACKAGE_DIR ${WEBVIEW2_PACKAGE_DIR} CACHE INTERNAL "" FORCE)

        wx_lib_include_directories(wxwebview "${WEBVIEW2_PACKAGE_DIR}/build/native/include")
        if(NOT MSVC)
            wx_lib_include_directories(wxwebview "${wxSOURCE_DIR}/include/wx/msw/wrl")
            if (NOT wxBUILD_MONOLITHIC)
                target_compile_options(wxwebview PRIVATE -Wno-unknown-pragmas)
            endif()
        endif()

        if (wxUSE_WEBVIEW_EDGE_STATIC)
            wx_set_webview2_arch()
            wx_lib_link_directories(wxwebview PUBLIC
                $<BUILD_INTERFACE:${WEBVIEW2_PACKAGE_DIR}/build/native/${WEBVIEW2_ARCH}/>
            )
        else()
            wx_webview_copy_webview2_loader(wxwebview)
        endif()

    endif()
elseif(WXGTK)
    if(LIBSOUP_FOUND)
        wx_lib_include_directories(wxwebview ${LIBSOUP_INCLUDE_DIRS})
        wx_lib_link_libraries(wxwebview PUBLIC ${LIBSOUP_LIBRARIES})
    endif()
    if(wxUSE_WEBVIEW_WEBKIT2)
        wx_lib_include_directories(wxwebview ${WEBKIT2_INCLUDE_DIR})
        wx_lib_link_libraries(wxwebview PUBLIC ${WEBKIT2_LIBRARIES})
    elseif(wxUSE_WEBVIEW_WEBKIT)
        wx_lib_include_directories(wxwebview ${WEBKIT_INCLUDE_DIR})
        wx_lib_link_libraries(wxwebview PUBLIC ${WEBKIT_LIBRARIES})
    endif()
endif()

# webkit extension plugin
# we can't use (all of the) macros and functions because this library should
# always be build as a shared libary, and not included in the monolithic build.
if(WXGTK AND wxUSE_WEBVIEW_WEBKIT2)
    wx_append_sources(WEBKIT2_EXT_FILES WEBVIEW_WEBKIT2_EXTENSION)
    add_library(wxwebkit2_ext SHARED ${WEBKIT2_EXT_FILES})
    wx_set_target_properties(wxwebkit2_ext IS_PLUGIN)
    set_target_properties(wxwebkit2_ext PROPERTIES NO_SONAME 1)

    # Change output name to match expected name in webview_webkit2.cpp: webkit2_ext*
    set(lib_unicode "u")

    set(lib_rls)
    set(lib_dbg)
    if(WIN32_MSVC_NAMING)
        set(lib_dbg "d")
    endif()

    if(wxVERSION_IS_DEV)
        set(WX_WEB_EXT_VERSION "${wxMAJOR_VERSION}.${wxMINOR_VERSION}.${wxRELEASE_NUMBER}")
    else()
        set(WX_WEB_EXT_VERSION "${wxMAJOR_VERSION}.${wxMINOR_VERSION}")
    endif()

    set_target_properties(wxwebkit2_ext PROPERTIES
        OUTPUT_NAME       "webkit2_ext${lib_unicode}${lib_rls}-${WX_WEB_EXT_VERSION}"
        OUTPUT_NAME_DEBUG "webkit2_ext${lib_unicode}${lib_dbg}-${WX_WEB_EXT_VERSION}"
        PREFIX ""
    )

    target_include_directories(wxwebkit2_ext PRIVATE
        ${LIBSOUP_INCLUDE_DIRS}
        ${WEBKIT2_INCLUDE_DIR}
        )
    target_link_libraries(wxwebkit2_ext PUBLIC
        ${LIBSOUP_LIBRARIES}
        ${WEBKIT2_LIBRARIES}
        )

    wx_install(TARGETS wxwebkit2_ext LIBRARY DESTINATION "lib/wx/${WX_WEB_EXT_VERSION}/web-extensions")

    wx_add_dependencies(wxwebview wxwebkit2_ext)
endif()
