cmake_minimum_required(VERSION 3.5)
enable_language(CXX)

# input_haiku_media.so — Haiku-only Media Kit video input plugin
if(CMAKE_SYSTEM_NAME STREQUAL "Haiku")
    find_library(MEDIA_LIB       media)
    find_library(TRANSLATION_LIB translation)
    find_library(BE_LIB          be)

    if(MEDIA_LIB AND TRANSLATION_LIB AND BE_LIB)
        message(STATUS " + input_haiku_media  (Haiku Media Kit)")

        add_library(input_haiku_media SHARED input_haiku_media.cpp)

        set_target_properties(input_haiku_media PROPERTIES
            PREFIX ""
            SUFFIX ".so"
            CXX_STANDARD 11
        )

        target_include_directories(input_haiku_media PRIVATE
            ${CMAKE_SOURCE_DIR}
            ${CMAKE_SOURCE_DIR}/plugins
        )

        target_link_libraries(input_haiku_media
            ${MEDIA_LIB}
            ${TRANSLATION_LIB}
            ${BE_LIB}
        )

        install(TARGETS input_haiku_media
            LIBRARY DESTINATION ${CMAKE_INSTALL_PREFIX}
        )
    else()
        message(STATUS " - input_haiku_media  (missing: media=${MEDIA_LIB} translation=${TRANSLATION_LIB} be=${BE_LIB})")
    endif()
endif()
