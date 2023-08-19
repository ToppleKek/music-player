
#-Wall -Wextra -Wpedantic -Wconversion
CXX_FLAGS="-fno-exceptions -g -std=c++20 -fdeclspec -Wall -Wextra -Wno-format"
CXX_FILES="song_table_view.cpp play_queue.cpp main.cpp tags.cpp db.cpp vulkan.cpp linux_ichigo.cpp ./thirdparty/imgui/imgui.cpp ./thirdparty/imgui/imgui_draw.cpp ./thirdparty/imgui/imgui_tables.cpp ./thirdparty/imgui/imgui_widgets.cpp ./thirdparty/imgui/imgui_impl_sdl2.cpp ./thirdparty/imgui/imgui_impl_vulkan.cpp"
LIBS="stdc++ -lvulkan -lSDL2 -lm -lasound"
# LIBS="user32  ${VULKAN_SDK}/Lib/vulkan-1.lib -lgdi32 -lwinmm -lsetupapi -loleaut32 -lole32 -limm32.lib -lversion -ladvapi32 -lshell32 -lWs2_32 -lSecur32 -lBcrypt -lole32 -ldxguid -lMfplat -lMfuuid -lstrmiids -ldsound ./lib/libavcodec.a ./lib/libavformat.a ./lib/libavutil.a ./lib/libswresample.a ./lib/SDL2main.lib ./lib/SDL2main.lib ./lib/SDL2.lib"
#./lib/libavcodec.a ./lib/libavformat.a ./lib/libavutil.a ./lib/libswresample.a
EXE_NAME="music"
INCLUDE="./include"

if [ "${1}" = "run" ]; then
    cd build
    ./$EXE_NAME
    exit 0
fi

if [ "${1}" = "br" ]; then
    clang ${CXX_FLAGS} -l ${LIBS} -I ${INCLUDE} ${CXX_FILES} -o build/${EXE_NAME}
    cd build
    ./$EXE_NAME
    exit 0
fi

# clang ${CXX_FLAGS} -l ${LIBS} -I ${INCLUDE} ${CXX_FILES} -o ${EXE_NAME}
clang ${CXX_FLAGS} -I ${INCLUDE} ${CXX_FILES} -l ${LIBS} -o build/${EXE_NAME}
