/*
This includes CONSTANTS and GLOBAL IMPORTS 
*/

#pragma once

#include <string>

namespace Config {
    const std::string SHADER_DIR = "../bin/shaders";
    const std::string ASSET_DIR = "../assets";
    const std::string YOLO_MODEL_PATH = ASSET_DIR + "/models/yolov8s-seg.onnx";
}