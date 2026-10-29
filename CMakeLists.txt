cmake_minimum_required(VERSION 3.22)
project(Rovno VERSION 0.9.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")   # static CRT: the effect DLL runs inside audiodg

# JUCE (the app's UI): a local checkout via -DJUCE_DIR=..., otherwise downloaded at configure time.
set(JUCE_DIR "" CACHE PATH "Local JUCE 8 checkout (optional)")
if(JUCE_DIR)
    add_subdirectory(${JUCE_DIR} ${CMAKE_BINARY_DIR}/juce)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE GIT_REPOSITORY https://github.com/juce-framework/JUCE.git GIT_TAG 8.0.16-security GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

add_compile_options(/utf-8 /EHsc)
add_compile_definitions(_CRT_SECURE_NO_WARNINGS UNICODE _UNICODE NOMINMAX)
set(STRICT /W4 /permissive-)

# --- the app ----------------------------------------------------------------------------------
juce_add_binary_data(RovnoData SOURCES
    Resources/fonts/Onest-Regular.ttf Resources/fonts/Onest-Medium.ttf Resources/fonts/Onest-SemiBold.ttf
    Resources/fonts/Onest-Bold.ttf Resources/icon-256.png Resources/icon-32.png)
juce_add_gui_app(Rovno PRODUCT_NAME "Rovno" COMPANY_NAME "Rovno" VERSION ${PROJECT_VERSION}
    ICON_BIG Resources/icon-256.png ICON_SMALL Resources/icon-32.png)
target_sources(Rovno PRIVATE
    Source/app/Main.cpp Source/app/Controller.cpp Source/app/SourceDetector.cpp Source/app/Devices.cpp
    Source/app/KeepAlive.cpp Source/app/MainComponent.cpp Source/app/QuickMenu.cpp Source/app/I18n.cpp)
target_compile_definitions(Rovno PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_APPLICATION_NAME_STRING="Rovno" JUCE_APPLICATION_VERSION_STRING="${PROJECT_VERSION}")
target_compile_options(Rovno PRIVATE /bigobj)
target_link_libraries(Rovno PRIVATE RovnoData juce::juce_gui_extra juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags ole32 windowsapp dwmapi)

# --- offline renderer (tests, tuning) ---------------------------------------------------------
add_executable(rovno_render Source/tools/RenderMain.cpp)
target_compile_options(rovno_render PRIVATE ${STRICT})

# --- offline tone corrector (as the app drives it) ---------------------------------------------
add_executable(rovno_tone Source/tools/ToneMain.cpp)

# --- speech/music features (classifier training) -----------------------------------------------
add_executable(rovno_features Source/tools/FeaturesMain.cpp)

# --- what the app sees in the devices ----------------------------------------------------------
add_executable(rovno_devices Source/tools/DevicesMain.cpp Source/app/Devices.cpp)
target_link_libraries(rovno_devices PRIVATE ole32)

# --- what is playing (detector test) -----------------------------------------------------------
add_executable(rovno_detect Source/tools/DetectMain.cpp Source/app/SourceDetector.cpp)
target_link_libraries(rovno_detect PRIVATE ole32 windowsapp)
target_compile_options(rovno_detect PRIVATE /bigobj)

# --- the system effect (loaded by audiodg) ----------------------------------------------------
add_library(RovnoApo SHARED Source/apo/RovnoApo.cpp Source/apo/DllMain.cpp Source/apo/RovnoApo.def)
target_link_libraries(RovnoApo PRIVATE ole32 oleaut32 advapi32 propsys)
target_compile_options(RovnoApo PRIVATE /EHa ${STRICT})
target_compile_definitions(RovnoApo PRIVATE WIN32_LEAN_AND_MEAN)

# --- the effect hosted the way audiodg does it (run before installing anything) --------------
add_executable(apo_host_test Source/tests/ApoHostTest.cpp)
target_link_libraries(apo_host_test PRIVATE ole32 oleaut32 propsys)

# --- a stand-in for the effect (tests the app without sound hardware) --------------------------
add_executable(rovno_fakefx Source/tests/FakeEffectMain.cpp)
target_link_libraries(rovno_fakefx PRIVATE advapi32)

# --- install / remove / talk to the effect ----------------------------------------------------
add_executable(rovno_ctl Source/tools/CtlMain.cpp)
target_link_libraries(rovno_ctl PRIVATE advapi32 ole32 oleaut32)

