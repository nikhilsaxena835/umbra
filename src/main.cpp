// g++ -o main main.cpp -lvulkan

/*
This program works as follows : 
    1) Using ffmpeg decompose video into images/frames.
    2) On each of these images, run the ML model and make shades.
    3) For each new masked image, run the Vulkan Compute code.
    
    Future Goals : Have a GUI using IMGUI for this system, rendering on screen is not an immediate goal.
    The syntax is ./main <path_to_video_file> <compiled_shader_path> <flag_object_detection> 
    Eg : ./main test/video.mp4 ghibli.spv false
*/

#include <cstdlib>
#include <stdexcept>
#include <iostream>
#include <filesystem>
#include <chrono>
#include "io/video_io.hpp"
#include "core/vulkan_engine.hpp"
#include "processing/frame_processor.hpp"


int main(int argc, char* argv[])
{
    auto appStart = std::chrono::high_resolution_clock::now();
    try{
        if (!checkFFMPEG()) 
                throw std::runtime_error("ffmpeg is not installed. Please install ffmpeg to continue.");
            
        std::string videoPath;
        std::string shaderName;
        bool objectDetection = false;
        std::cout << argc << std::endl;
        if (argc == 4) 
        {   
            videoPath = argv[1];
            shaderName = argv[2];
            std::string temp = argv[3];
            if(temp == "true")
            objectDetection = true;
        }
        else 
        {
            std::cout << "Incorrect syntax : ./main <path_to_video_file> <shader_name> <flag_object_detection> ";
            return EXIT_SUCCESS;
        }
    
        if (!std::filesystem::exists(videoPath)) 
            throw std::runtime_error("Input video file does not exist: " + videoPath);

        std::filesystem::path inputPath(videoPath);
        std::string baseDir = inputPath.parent_path().string();
        std::string tempFramesDir = baseDir + "/temp_frames";
        std::string processedFramesDir = baseDir + "/processed_frames";
        std::string outputVideo = baseDir + "/output_" + inputPath.filename().string();    
        
        std::cout << "Extracting frames from video ..." << std::endl;
        auto stageStart = std::chrono::high_resolution_clock::now();
        extractFrames(videoPath, tempFramesDir);
        auto stageEnd = std::chrono::high_resolution_clock::now();
        double duration = std::chrono::duration<double, std::milli>(stageEnd - stageStart).count();
        std::cout << "Frame extraction took: " << duration << " ms" << std::endl;
        
        VulkanEngine engine;
        
        stageStart = std::chrono::high_resolution_clock::now();
        if(objectDetection){
            std::cout << "Masking frames and applying shaders ..." << std::endl;
            FrameProcessor fp (engine, tempFramesDir, processedFramesDir);
            fp.processFramesWithMask();
        }
        else{
            std::cout << "Applying shaders ..." << std::endl;
            FrameProcessor fp (engine, tempFramesDir, processedFramesDir, shaderName);
            fp.processFrames();
        }
        stageEnd = std::chrono::high_resolution_clock::now();
        duration = std::chrono::duration<double, std::milli>(stageEnd - stageStart).count();
        std::cout << "Frame processing stage took: " << duration << " ms" << std::endl;


        std::cout << "Making video " << std::endl;
        stageStart = std::chrono::high_resolution_clock::now();
        createVideo(processedFramesDir, outputVideo, videoPath, 30);
        stageEnd = std::chrono::high_resolution_clock::now();
        duration = std::chrono::duration<double, std::milli>(stageEnd - stageStart).count();
        std::cout << "Video creation took: " << duration << " ms" << std::endl;

    }
    catch (const std::exception& e) 
    {
        std::cerr << e.what() << std::endl;
        return EXIT_FAILURE;
    }
    
    auto appEnd = std::chrono::high_resolution_clock::now();
    double totalDuration = std::chrono::duration<double, std::milli>(appEnd - appStart).count();
    std::cout << "Total application runtime: " << totalDuration << " ms" << std::endl;
    
    return EXIT_SUCCESS;
}