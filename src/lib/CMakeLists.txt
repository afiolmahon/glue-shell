add_library(CrewLib STATIC
    command.cpp
    module.cpp
    util.cpp
)
target_link_libraries(CrewLib
    PUBLIC
        fmt
        nlohmann_json::nlohmann_json
)
target_include_directories(CrewLib PUBLIC include)

add_subdirectory(test)
