# umbra
umbra is a Vulkan-based video processing engine.

## Build

To build the project, you need to have CMake and Vulkan SDK installed.



```bash

mkdir build

cd build

cmake ..

make

```



## Usage

```bash

./umbra <path_to_video_file> <shader_name> <flag_object_detection>

```

Example:

```bash

./umbra test.mp4 ghibli true

```
