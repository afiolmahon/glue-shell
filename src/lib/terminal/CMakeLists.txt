add_library(crew-terminal STATIC
    terminal.cpp
)
target_include_directories(crew-terminal PUBLIC include)
target_link_libraries(crew-terminal
    PRIVATE
        crew-common
        fmt
)

