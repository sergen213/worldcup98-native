# OpenAL mixer (streams + one-shots). Ported from fifa99-native audio/; the
# match-audio muxer is not ported (WC98 has no .gam match audio pipeline yet).
find_package(OpenAL CONFIG REQUIRED)

add_library(wc98_audio STATIC src/mixer.cpp src/speech_queue.cpp)
target_include_directories(wc98_audio PUBLIC include)
target_link_libraries(wc98_audio PUBLIC wc98_formats OpenAL::OpenAL PRIVATE wc98::core)
