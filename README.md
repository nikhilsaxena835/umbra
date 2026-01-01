# Umbra

Umbra is a C++ Vulkan-based video processing application. It uses GPU-accelerated compute shaders to apply effects to video files, with an optional feature to apply effects selectively to objects detected by a YOLOv8 model.

## Features

-   **GPU Acceleration:** Uses the Vulkan API for high-performance, cross-platform compute operations.
-   **Video Processing:** Decomposes videos into frames, processes them, and reassembles them into a new video file using FFmpeg, preserving the original audio track.
-   **Shader Effects:** Applies custom SPIR-V compute shaders to video frames.
-   **AI-Powered Object Masking:** Can optionally use a YOLOv8 segmentation model (via ONNX Runtime) to apply shaders only to specific detected objects (e.g., a "person").

---

## Architecture

The application is designed around three main components: `core`, `io`, and `processing`.

```
[Input Video] -> IO (ffmpeg) -> Frames
     |
     +-> Processing (CPU) -> Object Detection (ONNX) -> Masks
     |
     +-> Core (Vulkan GPU) -> Shader Execution -> Processed Frames
     |
     +-> IO (ffmpeg) -> [Output Video]
```

### 1. `core` - The Vulkan Engine
-   **Purpose:** Manages all low-level Vulkan objects and GPU operations.
-   `VulkanEngine`: Initializes the Vulkan instance, selects a physical device, and creates the logical device and compute queue.
-   `BufferManager`: A helper class to create and manage Vulkan buffers.
-   `ComputePipeline`: A stateful class that encapsulates a complete Vulkan compute pipeline. It manages a pool of reusable buffers and synchronization fences to efficiently process frames on the GPU without constant resource reallocation.
-   `ShaderManager`: Loads compiled `.spv` shader files from disk and manages their corresponding `ComputePipeline` objects.

### 2. `io` - Input/Output Handling
-   **Purpose:** Handles all interaction with the filesystem and external tools.
-   `video_io`: Contains functions that wrap `ffmpeg` command-line calls to extract frames from a video and create a new video from processed frames.
-   `ppm_handler`: Contains helper functions to load and save image data from/to the simple `.ppm` (Portable Pixmap) file format, which acts as the intermediary format between `ffmpeg` and the Vulkan engine.

### 3. `processing` - The Control Layer
-   **Purpose:** Orchestrates the entire video processing workflow.
-   `FrameProcessor`: The central class that drives the application. It gets a list of frames, and for each frame, it coordinates the other components.
-   `ObjectDetector`: If enabled, this class uses the ONNX Runtime to load and execute the YOLOv8 model, producing a set of object classes and segmentation masks for a given frame.
-   `MaskGenerator`: Takes the raw mask data from the `ObjectDetector` and converts it into a format usable by the GPU as a shader mask.

---

## Workflow

The application proceeds in the following steps:

1.  **Argument Parsing:** `main.cpp` parses command-line arguments to get the input video, the default shader name, and the object detection flag.
2.  **Frame Extraction:** `video_io::extractFrames` is called. It uses `ffmpeg` to decode the input video and save every frame as a `.ppm` image in a temporary directory.
3.  **Initialization:** A `VulkanEngine` is created, and a `FrameProcessor` is initialized.
    - If object detection is enabled, the `FrameProcessor` loads *all* available shaders from the `shaders/` directory.
    - If disabled, it only loads the single shader specified in the command-line arguments.
4.  **Frame Processing Loop:** The `FrameProcessor` iterates through each extracted `.ppm` frame.
    - **With Object Detection:**
        - Detection is run every 5th frame to find objects.
        - For frames where detection runs, a mask is generated for each object class that has a corresponding shader file (e.g., a "person" is detected and `person.spv` exists). These masks are cached.
        - For intermediate frames, the cached masks from the last detection are reused.
        - For each detected object, the corresponding shader is executed on the GPU, applied only to the masked area of the frame.
    - **Without Object Detection:**
        - The single user-specified shader is executed on the GPU, applied to the entire frame.
5.  **Save Output:** The processed frame data is saved as a new `.ppm` file in another temporary directory.
6.  **Video Creation:** After all frames are processed, `video_io::createVideo` is called. It uses `ffmpeg` to combine the processed `.ppm` frames and the audio from the original input video into a final `output_*.mp4` file.

---

## Dependencies

-   **Vulkan SDK:** Required for GPU compute capabilities.
-   **FFmpeg:** Required for video decoding and encoding. Must be installed and available in the system's PATH.
-   **ONNX Runtime:** Required for running the YOLO model for object detection.
-   **CMake:** Used for building the project.
-   **A C++17 compliant compiler** (e.g., GCC, Clang).

---

## How to Build

The project uses CMake to generate build files.

```bash
# 1. Create a build directory
mkdir build

# 2. Navigate into it
cd build

# 3. Run CMake to configure the project
cmake ..

# 4. Run make to compile the code
make
```

---

## How to Run

The executable is located in the `build/` directory after compilation. It is run from the command line.

**Syntax:**
```bash
./umbra <path_to_video> <shader_name> <use_object_detection>
```

-   `<path_to_video>`: Relative or absolute path to the input video file.
-   `<shader_name>`: The name of the shader to use (without extension). This is only used if object detection is `false`.
-   `<use_object_detection>`: A boolean flag (`true` or `false`) to enable or disable object detection.

**Examples:**

-   **To run with object detection (and apply shaders like `person.spv` automatically):**
    ```bash
    ./build/umbra test/a.mp4 person true
    ```
    *(Note: the `<shader_name>` `person` is ignored here but is still a required argument)*

-   **To apply a single shader (`ghibli`) to the whole video:**
    ```bash
    ./build/umbra test/a.mp4 ghibli false
    ```

---

## Project Structure

```
.
├── assets/         # Model files and other assets.
│   └── models/
│       ├── coco.names
│       └── yolov8s-seg.onnx
├── build/          # Build directory (ignored by git).
├── CMakeLists.txt  # The main CMake build script.
├── improvements.md # Documentation on potential future optimizations.
├── LICENSE
├── README.md       # This file.
├── shaders/        # Source compute shaders.
│   ├── grayscale.comp
│   ├── person.spv  # Note: This is a compiled shader and should be in bin/
│   └── mask/
│       └── ghibli.comp
└── src/            # C++ source code.
    ├── core/       # Vulkan engine components.
    ├── io/         # Input/Output and ffmpeg wrappers.
    ├── processing/ # High-level processing logic.
    ├── config.h    # Global configuration paths.
    └── main.cpp    # Application entry point.
```
