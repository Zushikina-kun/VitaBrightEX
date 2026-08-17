cmake_minimum_required(VERSION 3.5)

if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    if(DEFINED ENV{VITASDK})
        set(CMAKE_TOOLCHAIN_FILE "$ENV{VITASDK}/share/vita.toolchain.cmake"
            CACHE PATH "toolchain file")
    else()
        message(FATAL_ERROR "Please define VITASDK to point to your SDK path!")
    endif()
endif()

project(vitabright)
include("${VITASDK}/share/vita.cmake" REQUIRED)

# -----------------------------------------------------------------------
# Build options
# -----------------------------------------------------------------------
option(ENABLE_LOGGING "Enable debug logging to ur0:data/vitabright_log.txt" OFF)
if(ENABLE_LOGGING)
    add_definitions(-DENABLE_LOGGING=1)
endif()

# -----------------------------------------------------------------------
# Compiler flags
# -----------------------------------------------------------------------
set(CMAKE_C_FLAGS   "${CMAKE_C_FLAGS}   -Wl,-q -Wall -Wextra -O3 -fno-pic -fno-PIC -std=gnu99")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11 -fno-rtti -fno-exceptions")

# -----------------------------------------------------------------------
# Sources
# -----------------------------------------------------------------------
add_executable(vitabright
    main.c
    main.h
    log.c
    log.h
    config.c
    config.h
    screen_filter.c
    screen_filter.h
    taihen_extra.h
    oled/parser.c
    oled/parser.h
    oled/hooks.c
    oled/hooks.h
    oled/lut.h
    lcd/hooks.c
    lcd/hooks.h
    lcd/lcd_lut.h
)

# -----------------------------------------------------------------------
# Link libraries
# -----------------------------------------------------------------------
target_link_libraries(vitabright
    SceDisplayForDriver_stub
    taihenForKernel_stub
    taihenModuleUtils_stub
    SceIofilemgrForDriver_stub
    gcc
    SceSysclibForDriver_stub
    SceSysmemForDriver_stub
    SceSysrootForKernel_stub
    SceSysrootForDriver_stub
)

set_target_properties(vitabright
    PROPERTIES
        LINK_FLAGS    "-nostdlib"
        COMPILE_FLAGS "-D__VITA_KERNEL__"
)

# -----------------------------------------------------------------------
# Package
# -----------------------------------------------------------------------
vita_create_self(vitabright.skprx vitabright
    CONFIG ${CMAKE_SOURCE_DIR}/module.yml
    UNSAFE
)

vita_create_stubs(vitabright-stubs vitabright module.yml
    KERNEL
)

install(DIRECTORY ${CMAKE_BINARY_DIR}/vitabright-stubs/
    DESTINATION lib
    FILES_MATCHING PATTERN "*.a"
)

# -----------------------------------------------------------------------
# Install helper: copy LUT files + config template to vita via FTP
# -----------------------------------------------------------------------
set(PSVITAIP "192.168.1.10" CACHE STRING "PS Vita IP address for FTP deploy")

add_custom_target(send
    COMMAND curl --ftp-method nocwd -T vitabright.skprx
            ftp://${PSVITAIP}:1337/ur0:/tai/vitabright.skprx
    COMMAND curl --ftp-method nocwd -T ${CMAKE_SOURCE_DIR}/oled/luts/vitabright_lut.txt
            ftp://${PSVITAIP}:1337/ur0:/tai/vitabright_lut.txt
    COMMAND curl --ftp-method nocwd -T ${CMAKE_SOURCE_DIR}/oled/luts/vitabright_lut_p4.txt
            ftp://${PSVITAIP}:1337/ur0:/tai/vitabright_lut_p4.txt
    COMMAND curl --ftp-method nocwd -T ${CMAKE_SOURCE_DIR}/oled/luts/vitabright_lut_p5.txt
            ftp://${PSVITAIP}:1337/ur0:/tai/vitabright_lut_p5.txt
    COMMAND curl --ftp-method nocwd -T ${CMAKE_SOURCE_DIR}/oled/luts/vitabright_lut_p6.txt
            ftp://${PSVITAIP}:1337/ur0:/tai/vitabright_lut_p6.txt
    COMMAND curl --ftp-method nocwd -T ${CMAKE_SOURCE_DIR}/lcd/luts/vitabright_lcd_lut.txt
            ftp://${PSVITAIP}:1337/ur0:/tai/vitabright_lcd_lut.txt
    COMMAND curl --ftp-method nocwd -T ${CMAKE_SOURCE_DIR}/vitabrightex.cfg
            ftp://${PSVITAIP}:1337/ur0:/tai/vitabrightex.cfg
    DEPENDS vitabright.skprx
)
