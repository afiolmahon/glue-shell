add_library(crew-common STATIC
    command.cpp
    interpreter.cpp
    module.cpp
    util.cpp
)
target_include_directories(crew-common PUBLIC include)
target_link_libraries(crew-common
    PUBLIC
        fmt
        nlohmann_json::nlohmann_json
)

add_subdirectory(test)
