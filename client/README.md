# ESP32 Remote Render Client

## Dependencies

- [CMake 3.24](https://cmake.org/)
- [vcpkg](https://vcpkg.io/en/index.html)
- C++20 compiler
- OpenGL 4.6 capable GPU

```
$ vcpkg install glfw3 glad glm fmt
```

## Building

```
$ cmake -B [build directory] -S . -DCMAKE_TOOLCHAIN_FILE=[path to vcpkg]/scripts/buildsystems/vcpkg.cmake ..
$ cmake --build [build directory]
```

