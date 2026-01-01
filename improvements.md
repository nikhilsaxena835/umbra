# Umbra Performance Improvements

This document outlines potential improvements to the Umbra codebase, focusing on performance optimizations and architectural changes that would make the application more efficient.

## 1. Eliminate Intermediate File I/O

**Problem:** The current workflow is heavily dependent on disk I/O, which is a major performance bottleneck. The process is: `ffmpeg decode to .ppm files` -> `app reads .ppm files` -> `app processes frames` -> `app writes new .ppm files` -> `ffmpeg encodes .ppm files`. This reads and writes thousands of uncompressed image files from/to the disk.

**Suggestion:** Implement an in-memory video processing pipeline.

-   **Decode:** Use the `libavformat` and `libavcodec` libraries (from FFmpeg) to decode video frames directly into memory (`AVFrame`).
-   **Process:** Pass the raw frame data from memory directly to the `FrameProcessor`.
-   **Encode:** After the GPU processes a frame, the resulting image data in memory can be passed directly to `libavcodec` to be encoded and written to the output video container via `libavformat`.

**Benefit:** This would almost completely eliminate the disk I/O bottleneck for frame data, significantly improving performance and removing the need for `temp_frames` and `processed_frames` directories.

## 2. Implement an Asynchronous Processing Pipeline

**Problem:** The application processing is sequential. For each frame, the CPU prepares data, submits it to the GPU, and then waits idly for the GPU to finish (`vkQueueWaitIdle`) before starting the next frame. This leads to poor hardware utilization, as either the CPU or the GPU is idle at any given time.

**Suggestion:** Decouple the main stages into a multi-threaded pipeline that allows CPU and GPU work to overlap.

-   **Thread 1 (Decoder):** Reads frames from the input video and puts them into a thread-safe `frames_to_process` queue.
-   **Thread 2 (CPU Processor):** Fetches frames from the `frames_to_process` queue. It performs CPU-bound work like object detection and mask generation. It then builds a Vulkan command buffer for the GPU work and submits it to the compute queue. Instead of waiting, it uses a fence or semaphore to signal completion and immediately starts preparing the next frame.
-   **Thread 3 (Encoder):** Once the GPU signals that a frame is finished (via a fence), this thread reads back the processed data and sends it to the video encoder.

**Benefit:** This architecture allows multiple frames to be "in-flight" at once, ensuring that the CPU is busy preparing frame `N+1` while the GPU is busy rendering frame `N`. This dramatically improves throughput and hardware utilization.

## 3. Optimize Vulkan Resource Management

**Problem:** The `ComputePipeline` creates and destroys Vulkan buffers (`VkBuffer`, `VkDeviceMemory`) for every single frame processed (`cleanupBuffers()` is called on every `processImage`). Allocating and deallocating GPU memory and objects is an extremely expensive operation.

**Suggestion:** Pre-allocate and reuse resources.

-   At initialization, create a small pool of reusable resources (e.g., 2-3 sets of input/output/mask buffers). This is often called "triple buffering".
-   Use Vulkan fences to track when the GPU is finished using a set of buffers.
-   The CPU can then safely reuse a set of buffers once its corresponding fence is signaled.

**Benefit:** This avoids the massive performance overhead of constant resource allocation/deallocation, leading to much smoother and faster GPU processing.

## 4. Optimize Algorithm Frequency

**Problem:** Object detection and segmentation via the YOLO model is computationally expensive and is currently run on every single frame.

**Suggestion:** Run detection and segmentation intermittently.

-   Run the full `objectDetector->detect()` pass only every Nth frame (e.g., every 5th or 10th frame), as the commented-out code suggests.
-   For the intermediate frames, you can reuse the masks from the last keyframe where detection was run.
-   For a more advanced solution, implement a simple object tracking algorithm. After detecting objects in a keyframe, a faster tracking algorithm (e.g., based on optical flow or simple bounding box prediction) could update the mask positions for the intermediate frames without needing to run the full YOLO model.

**Benefit:** Reduces the CPU load from the neural network inference, freeing up the CPU for other tasks in the pipeline.

## 5. Correct Shader Application Logic

**Problem:** As identified previously, shaders for different detected objects are applied sequentially, with the output of one pass becoming the input for the next. This is incorrect if the goal is to apply different effects to different parts of the *same original image*.

**Suggestion:** Implement a multi-pass compositing approach.

1.  **Base Image**: Keep the original frame data in a dedicated, read-only buffer.
2.  **Shader Passes**: For each detected object class (e.g., "person"), run its specific shader. The shader should take the **original base image** and the object's mask as input and write its output to a temporary buffer.
3.  **Compositing Pass**: Run a final compositing shader. This shader would take the original image, all the temporary output buffers, and all the masks. For each pixel, it would look at the masks to decide which temporary buffer to sample from, effectively blending all the separate effects into one final image.

**Benefit:** This correctly implements the intended visual effect and avoids the sequential layering of shaders. While it may require more GPU passes, it achieves the correct visual result.
