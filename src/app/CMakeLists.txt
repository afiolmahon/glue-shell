add_executable(crew-repl crew-repl.cpp)
target_link_libraries(crew-repl
    PRIVATE
        crew-common
        crew-terminal
        fmt
        nlohmann_json::nlohmann_json
)

add_custom_target(run_repl
    COMMAND crew-repl
    DEPENDS crew-repl
)
