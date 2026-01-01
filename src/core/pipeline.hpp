#pragma once

#include <vulkan/vulkan.h>
#include <vector>
#include <string>

class VulkanEngine;

const int MAX_FRAMES_IN_FLIGHT = 2;

class ComputePipeline {
public:
    ComputePipeline(VulkanEngine& engine, const std::string& shaderPath, int width, int height);
    ~ComputePipeline();

    double processImage(const std::vector<unsigned char>& inputData, std::vector<unsigned char>& outputData,
                      const std::vector<unsigned char>& maskData);
    double processImage(const std::vector<unsigned char>& inputData, std::vector<unsigned char>& outputData);
    void setDimensions(int width, int height);

private:
    VulkanEngine& engine;
    VkDescriptorPool descriptorPool;
    VkDescriptorSetLayout descriptorSetLayout;
    VkPipelineLayout pipelineLayout;
    VkPipeline pipeline;

    std::vector<VkBuffer> inputBuffers;
    std::vector<VkDeviceMemory> inputMemories;
    std::vector<VkBuffer> outputBuffers;
    std::vector<VkDeviceMemory> outputMemories;
    std::vector<VkBuffer> maskBuffers;
    std::vector<VkDeviceMemory> maskMemories;
    std::vector<VkDescriptorSet> descriptorSets;

    std::vector<VkFence> inFlightFences;
    size_t currentFrame = 0;

    int width, height;

    void createDescriptorSetLayout();
    void createDescriptorPool();
    void createPipeline(const std::string& shaderPath);
    void createSyncObjects();
    void createComputeBuffers();
    void createDescriptorSets();
};