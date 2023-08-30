add_library(CrewLib STATIC
    command.cpp
)
target_include_directories(CrewLib PUBLIC include)

add_subdirectory(test)
