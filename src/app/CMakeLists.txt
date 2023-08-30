add_executable(crew-cli crew.cpp)
target_link_libraries(crew-cli
    PRIVATE
        CrewLib
        fmt
        nlohmann_json::nlohmann_json
)
