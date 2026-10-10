# M1.0 tray spike. Throwaway code: see docs/M1-tray-spike.md.

qt_add_executable(aqtruncat-tray-spike main.cpp)

target_link_libraries(aqtruncat-tray-spike PRIVATE Qt6::Widgets)
target_compile_options(aqtruncat-tray-spike PRIVATE -Wall -Wextra)

# Embed the licensed upstream frames as :/runners/<name>/<name>-frame-N.png
set(runner_root "${PROJECT_SOURCE_DIR}/third_party/runcatneo/runners")
file(GLOB runner_frames "${runner_root}/*/*.png")

qt_add_resources(aqtruncat-tray-spike runners
    PREFIX /runners
    BASE "${runner_root}"
    FILES ${runner_frames})
