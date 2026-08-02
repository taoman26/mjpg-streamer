cmake_minimum_required(VERSION 3.5)
enable_language(CXX)

# input_haiku_media.so — Haiku-only Media Kit video input plugin.
# Bridges to BubiCam's WebcamKit (WebcamRoster/WebcamDevice) rather than
# talking to the Media Kit directly - see BUBICAM_DIR below.
if(CMAKE_SYSTEM_NAME STREQUAL "Haiku")
    find_library(MEDIA_LIB       media)
    find_library(TRANSLATION_LIB translation)
    find_library(BE_LIB          be)

    set(BUBICAM_DIR "$ENV{HOME}/git/BubiCam")
    set(WEBCAM_LIB_DIR "${BUBICAM_DIR}/lib/libwebcam/objects.x86_64-cc13-release")
    find_library(WEBCAM_LIB webcam PATHS "${WEBCAM_LIB_DIR}" NO_DEFAULT_PATH)

    if(MEDIA_LIB AND TRANSLATION_LIB AND BE_LIB AND WEBCAM_LIB)
        message(STATUS " + input_haiku_media  (Haiku Media Kit, via BubiCam WebcamKit)")

        add_library(input_haiku_media SHARED input_haiku_media.cpp)

        set_target_properties(input_haiku_media PROPERTIES
            PREFIX ""
            SUFFIX ".so"
            CXX_STANDARD 11
            BUILD_RPATH "${WEBCAM_LIB_DIR}"
        )

        target_include_directories(input_haiku_media PRIVATE
            ${CMAKE_SOURCE_DIR}
            ${CMAKE_SOURCE_DIR}/plugins
            "${BUBICAM_DIR}/lib/libwebcam/include"
            "${BUBICAM_DIR}/src/webcam"
        )

        target_link_libraries(input_haiku_media
            ${WEBCAM_LIB}
            ${MEDIA_LIB}
            ${TRANSLATION_LIB}
            ${BE_LIB}
        )

        install(TARGETS input_haiku_media
            LIBRARY DESTINATION ${CMAKE_INSTALL_PREFIX}
        )
    else()
        message(STATUS " - input_haiku_media  (missing: media=${MEDIA_LIB} translation=${TRANSLATION_LIB} be=${BE_LIB} webcam=${WEBCAM_LIB})")
    endif()
endif()
