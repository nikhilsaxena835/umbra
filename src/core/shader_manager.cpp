#include "shader_manager.hpp"
#include "config.h"
#include <stdexcept>
#include <fstream>
#include <sstream>
#include <algorithm>

ShaderManager::ShaderManager(VulkanEngine& engine) : engine(engine) {}

ShaderManager::~ShaderManager() {}

void ShaderManager::loadShadersFromDirectory() 
{
    const std::string shaderDir = Config::SHADER_DIR;

    for (const auto& entry : fs::directory_iterator(shaderDir)) 
    {
        if (entry.is_regular_file()) 
        {
            std::string shaderName = entry.path().stem().string();
            std::string shaderPath = entry.path().string();
            shadersAvailable.insert(shaderName);
            pipelines[shaderName] = std::make_shared<ComputePipeline>(engine, shaderPath, 0, 0);
        }
    }
}

void ShaderManager::loadShader(const std::string& shaderName) 
{
    const std::string shaderDir = Config::SHADER_DIR;
    std::string shaderPath = shaderDir + "/" + shaderName + ".spv";
    pipelines[shaderName] = std::make_shared<ComputePipeline>(engine, shaderPath, 0, 0);
}

std::shared_ptr<ComputePipeline> ShaderManager::getPipeline(const std::string& name) 
{
    if (pipelines.find(name) != pipelines.end()) 
        return pipelines[name];
    
    throw std::runtime_error("Shader not found: " + name);
}

void ShaderManager::setDimensions(int width, int height) 
{
    for (auto& pair : pipelines) 
        pair.second->setDimensions(width, height);
    
}

std::set<std::string> ShaderManager::getAvailableClasses()
{
    return ShaderManager::shadersAvailable;
}