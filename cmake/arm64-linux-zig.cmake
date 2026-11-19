set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

find_program(ZIIRO_ZIG NAMES zig REQUIRED)
list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES ZIIRO_ZIG)
set(CMAKE_C_COMPILER "${ZIIRO_ZIG};cc;-target;aarch64-linux-gnu.2.34")
set(CMAKE_CXX_COMPILER "${ZIIRO_ZIG};c++;-target;aarch64-linux-gnu.2.34")
set(CMAKE_ASM_COMPILER "${ZIIRO_ZIG};cc;-target;aarch64-linux-gnu.2.34")

# The host's archive tools may expect Mach-O members.
foreach(language C CXX)
    set(CMAKE_${language}_ARCHIVE_CREATE "\"${ZIIRO_ZIG}\" ar qc <TARGET> <LINK_FLAGS> <OBJECTS>")
    set(CMAKE_${language}_ARCHIVE_APPEND "\"${ZIIRO_ZIG}\" ar q <TARGET> <LINK_FLAGS> <OBJECTS>")
    set(CMAKE_${language}_ARCHIVE_FINISH "\"${ZIIRO_ZIG}\" ranlib <TARGET>")
endforeach()
