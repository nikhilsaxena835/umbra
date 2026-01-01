#include "pipeline.hpp"
#include "vulkan_engine.hpp"
#include "buffer_manager.hpp"
#include <stdexcept>
#include <cstring>
#include <fstream>

#define VK_CHECK(result) if (result != VK_SUCCESS) { \
    fprintf(stderr, "Error: %d at line %d\n", result, __LINE__); \
    exit(1); \
}

ComputePipeline::ComputePipeline(VulkanEngine& engine, const std::string& shaderPath, int width, int height)
    : engine(engine), width(width), height(height) 
{
    createDescriptorSetLayout();
    createDescriptorPool();
    createPipeline(shaderPath);
    createSyncObjects();

    if (width > 0 && height > 0) {
        createComputeBuffers();
        createDescriptorSets();
    }
}

ComputePipeline::~ComputePipeline() {
    vkDeviceWaitIdle(engine.getDevice());

    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        vkDestroyFence(engine.getDevice(), inFlightFences[i], nullptr);
    }

    if (!inputBuffers.empty()) {
        for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
            vkDestroyBuffer(engine.getDevice(), inputBuffers[i], nullptr);
            vkFreeMemory(engine.getDevice(), inputMemories[i], nullptr);
            vkDestroyBuffer(engine.getDevice(), outputBuffers[i], nullptr);
            vkFreeMemory(engine.getDevice(), outputMemories[i], nullptr);
            vkDestroyBuffer(engine.getDevice(), maskBuffers[i], nullptr);
            vkFreeMemory(engine.getDevice(), maskMemories[i], nullptr);
        }
    }
    
    vkDestroyPipeline(engine.getDevice(), pipeline, nullptr);
    vkDestroyPipelineLayout(engine.getDevice(), pipelineLayout, nullptr);
    vkDestroyDescriptorSetLayout(engine.getDevice(), descriptorSetLayout, nullptr);
    vkDestroyDescriptorPool(engine.getDevice(), descriptorPool, nullptr);
}

void ComputePipeline::setDimensions(int w, int h) {
    if (w == width && h == height) {
        return;
    }

    vkDeviceWaitIdle(engine.getDevice());

    if (!inputBuffers.empty()) {
        for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
            vkDestroyBuffer(engine.getDevice(), inputBuffers[i], nullptr);
            vkFreeMemory(engine.getDevice(), inputMemories[i], nullptr);
            vkDestroyBuffer(engine.getDevice(), outputBuffers[i], nullptr);
            vkFreeMemory(engine.getDevice(), outputMemories[i], nullptr);
            vkDestroyBuffer(engine.getDevice(), maskBuffers[i], nullptr);
            vkFreeMemory(engine.getDevice(), maskMemories[i], nullptr);
        }
    }

    width = w;
    height = h;

    if (width == 0 || height == 0) return;

    createComputeBuffers();
    createDescriptorSets();
}

double ComputePipeline::processImage(const std::vector<unsigned char>& inputData, std::vector<unsigned char>& outputData,
                                  const std::vector<unsigned char>& maskData) {
    if (inputBuffers.empty()) {
        throw std::runtime_error("Compute pipeline resources not initialized. Call setDimensions() before processing.");
    }
    
    VkDevice device = engine.getDevice();
    
    vkWaitForFences(device, 1, &inFlightFences[currentFrame], VK_TRUE, UINT64_MAX);
    vkResetFences(device, 1, &inFlightFences[currentFrame]);

    // --- Copy Data to GPU ---
    void* mappedMemory;
    vkMapMemory(device, inputMemories[currentFrame], 0, width * height * 4, 0, &mappedMemory);
    memcpy(mappedMemory, inputData.data(), width * height * 4);
    vkUnmapMemory(device, inputMemories[currentFrame]);

    if (!maskData.empty()) {
        vkMapMemory(device, maskMemories[currentFrame], 0, width * height * 4, 0, &mappedMemory);
        memcpy(mappedMemory, maskData.data(), width * height * 4);
        vkUnmapMemory(device, maskMemories[currentFrame]);
    }
    
    // --- Record and Submit Commands ---
    VkCommandBufferAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = engine.getCommandPool();
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer commandBuffer;
    VK_CHECK(vkAllocateCommandBuffers(device, &allocInfo, &commandBuffer));

    VkCommandBufferBeginInfo beginInfo = {};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    VK_CHECK(vkBeginCommandBuffer(commandBuffer, &beginInfo));
    
    VkQueryPool queryPool = engine.getQueryPool();
    vkCmdResetQueryPool(commandBuffer, queryPool, 0, 2);
    vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, queryPool, 0);

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout, 0, 1, &descriptorSets[currentFrame], 0, nullptr);

    int pushConstants[2] = { width, height };
    vkCmdPushConstants(commandBuffer, pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pushConstants), pushConstants);
    
    uint32_t groupSizeX = (width + 15) / 16;
    uint32_t groupSizeY = (height + 15) / 16;
    vkCmdDispatch(commandBuffer, groupSizeX, groupSizeY, 1);

    vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, queryPool, 1);
    VK_CHECK(vkEndCommandBuffer(commandBuffer));

    VkSubmitInfo submitInfo = {};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    VK_CHECK(vkQueueSubmit(engine.getComputeQueue(), 1, &submitInfo, inFlightFences[currentFrame]));
    
    // --- Wait and Retrieve Data ---
    vkWaitForFences(device, 1, &inFlightFences[currentFrame], VK_TRUE, UINT64_MAX);

    uint64_t timestamps[2];
    vkGetQueryPoolResults(device, queryPool, 0, 2, sizeof(timestamps), timestamps, sizeof(uint64_t), VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT);
    double gpuTime = (timestamps[1] - timestamps[0]) * engine.getTimestampPeriod() / 1e6; // Time in ms

    VkMappedMemoryRange range = {};
    range.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
    range.memory = outputMemories[currentFrame];
    range.offset = 0;
    range.size = width * height * 4;
    vkInvalidateMappedMemoryRanges(device, 1, &range);
    
    vkMapMemory(device, outputMemories[currentFrame], 0, width * height * 4, 0, &mappedMemory);
    outputData.resize(width * height * 4);
    memcpy(outputData.data(), mappedMemory, width * height * 4);
    vkUnmapMemory(device, outputMemories[currentFrame]);
    
    vkFreeCommandBuffers(device, engine.getCommandPool(), 1, &commandBuffer);

    currentFrame = (currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;

    return gpuTime;
}

double ComputePipeline::processImage(const std::vector<unsigned char>& inputData,
                                   std::vector<unsigned char>& outputData) {
    std::vector<unsigned char> dummyMask;
    return processImage(inputData, outputData, dummyMask);
}

void ComputePipeline::createDescriptorSetLayout() {
    std::vector<VkDescriptorSetLayoutBinding> bindings(3);

    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    bindings[2].binding = 2;
    bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[2].descriptorCount = 1;
    bindings[2].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo = {};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = bindings.size();
    layoutInfo.pBindings = bindings.data();

    VK_CHECK(vkCreateDescriptorSetLayout(engine.getDevice(), &layoutInfo, nullptr, &descriptorSetLayout));
}

void ComputePipeline::createDescriptorPool() {
    VkDescriptorPoolSize poolSize = {};
    poolSize.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    poolSize.descriptorCount = 3 * MAX_FRAMES_IN_FLIGHT;

    VkDescriptorPoolCreateInfo poolInfo = {};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.maxSets = MAX_FRAMES_IN_FLIGHT;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;

    VK_CHECK(vkCreateDescriptorPool(engine.getDevice(), &poolInfo, nullptr, &descriptorPool));
}

void ComputePipeline::createPipeline(const std::string& shaderPath) 
{
    std::ifstream file(shaderPath, std::ios::ate | std::ios::binary);
    if (!file.is_open()) 
    throw std::runtime_error("Failed to open shader file: " + shaderPath);

    size_t fileSize = file.tellg();
    std::vector<char> shaderCode(fileSize);
    file.seekg(0);
    file.read(shaderCode.data(), fileSize);
    file.close();

    VkShaderModuleCreateInfo shaderCreateInfo = {};
    shaderCreateInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    shaderCreateInfo.codeSize = shaderCode.size();
    shaderCreateInfo.pCode = reinterpret_cast<const uint32_t*>(shaderCode.data());

    VkShaderModule shaderModule;
    VK_CHECK(vkCreateShaderModule(engine.getDevice(), &shaderCreateInfo, nullptr, &shaderModule));

    VkPushConstantRange pushConstantRange = {};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(int) * 2; // Only width and height

    VkPipelineLayoutCreateInfo pipelineLayoutInfo = {};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &descriptorSetLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

    VK_CHECK(vkCreatePipelineLayout(engine.getDevice(), &pipelineLayoutInfo, nullptr, &pipelineLayout));

    VkComputePipelineCreateInfo pipelineInfo = {};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    pipelineInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    pipelineInfo.stage.module = shaderModule;
    pipelineInfo.stage.pName = "main";
    pipelineInfo.layout = pipelineLayout;

    VK_CHECK(vkCreateComputePipelines(engine.getDevice(), VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline));

    vkDestroyShaderModule(engine.getDevice(), shaderModule, nullptr);
}

void ComputePipeline::createComputeBuffers() {
    VkDeviceSize bufferSize = width * height * 4;
    BufferManager bufferManager(engine);

    inputBuffers.resize(MAX_FRAMES_IN_FLIGHT);
    inputMemories.resize(MAX_FRAMES_IN_FLIGHT);
    outputBuffers.resize(MAX_FRAMES_IN_FLIGHT);
    outputMemories.resize(MAX_FRAMES_IN_FLIGHT);
    maskBuffers.resize(MAX_FRAMES_IN_FLIGHT);
    maskMemories.resize(MAX_FRAMES_IN_FLIGHT);

    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        bufferManager.createBuffer(bufferSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                inputBuffers[i], inputMemories[i]);

        bufferManager.createBuffer(bufferSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                outputBuffers[i], outputMemories[i]);
        
        bufferManager.createBuffer(bufferSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                maskBuffers[i], maskMemories[i]);
    }
}

void ComputePipeline::createDescriptorSets() {
    std::vector<VkDescriptorSetLayout> layouts(MAX_FRAMES_IN_FLIGHT, descriptorSetLayout);
    VkDescriptorSetAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = descriptorPool;
    allocInfo.descriptorSetCount = MAX_FRAMES_IN_FLIGHT;
    allocInfo.pSetLayouts = layouts.data();

    descriptorSets.resize(MAX_FRAMES_IN_FLIGHT);
    VK_CHECK(vkAllocateDescriptorSets(engine.getDevice(), &allocInfo, descriptorSets.data()));

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        std::vector<VkDescriptorBufferInfo> bufferInfos(3);
        bufferInfos[0].buffer = inputBuffers[i];
        bufferInfos[0].offset = 0;
        bufferInfos[0].range = width * height * 4;

        bufferInfos[1].buffer = outputBuffers[i];
        bufferInfos[1].offset = 0;
        bufferInfos[1].range = width * height * 4;
        
        bufferInfos[2].buffer = maskBuffers[i];
        bufferInfos[2].offset = 0;
        bufferInfos[2].range = width * height * 4;

        std::vector<VkWriteDescriptorSet> descriptorWrites(3);
        for (int j = 0; j < 3; j++) {
            descriptorWrites[j].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            descriptorWrites[j].dstSet = descriptorSets[i];
            descriptorWrites[j].dstBinding = j;
            descriptorWrites[j].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            descriptorWrites[j].descriptorCount = 1;
            descriptorWrites[j].pBufferInfo = &bufferInfos[j];
        }
        vkUpdateDescriptorSets(engine.getDevice(), descriptorWrites.size(), descriptorWrites.data(), 0, nullptr);
    }
}

void ComputePipeline::createSyncObjects() {
    inFlightFences.resize(MAX_FRAMES_IN_FLIGHT);
    VkFenceCreateInfo fenceInfo = {};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT; // Start in signaled state

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        VK_CHECK(vkCreateFence(engine.getDevice(), &fenceInfo, nullptr, &inFlightFences[i]));
    }
}