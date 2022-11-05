cmake_minimum_required(VERSION 3.24)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

project(client)

find_package(glfw3	CONFIG REQUIRED)
find_package(glad	CONFIG REQUIRED)
find_package(glm 	CONFIG REQUIRED)
find_package(fmt 	CONFIG REQUIRED)

add_executable(${PROJECT_NAME})

target_compile_features(${PROJECT_NAME}
	PRIVATE
		cxx_std_20)

target_include_directories(${PROJECT_NAME}
	PRIVATE
		..)

target_sources(${PROJECT_NAME}
	PRIVATE
		main.cpp)

target_link_libraries(${PROJECT_NAME}
	PRIVATE
		glfw
		glad::glad
		glm::glm
		fmt::fmt-header-only)

