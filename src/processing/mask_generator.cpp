#include "mask_generator.hpp"
#include <vector>
#include <string>
#include <cmath>
#include <stdexcept>
#include <iostream>
#include <fstream>

MaskGenerator::MaskGenerator() {}
MaskGenerator::~MaskGenerator() {}

void MaskGenerator::generateMasks(
    const std::map<std::string, std::vector<std::vector<unsigned char>>>& classMasks,
    std::vector<std::pair<std::string, std::vector<unsigned char>>>& maskDataList,
    int width, int height)
{
    maskDataList.clear();

    for (const auto& [classLabel, maskList] : classMasks)
    {
        std::vector<unsigned char> combinedMask(width * height, 0);

        for (const auto& mask : maskList)
        {
            if (mask.size() != static_cast<size_t>(width * height)) {
                throw std::runtime_error("Invalid mask size for class: " + classLabel +
                    ", expected: " + std::to_string(width * height) +
                    ", got: " + std::to_string(mask.size()));
            }

            for (int i = 0; i < width * height; ++i) {
                combinedMask[i] |= (mask[i] > 0 ? 1 : 0);  // Pixel-wise OR
            }
        }

        std::vector<unsigned char> rgbaMask(width * height * 4, 0);
        for (int i = 0; i < width * height; ++i) {
            unsigned char value = (combinedMask[i] > 0) ? 0 : 255; // Invert mask
            rgbaMask[i * 4 + 0] = rgbaMask[i * 4 + 1] = rgbaMask[i * 4 + 2] = value;
            rgbaMask[i * 4 + 3] = (value > 0) ? 255 : 0;
        }

        maskDataList.emplace_back(classLabel, std::move(rgbaMask));
    }
}


void MaskGenerator::saveMaskForDebug(const std::string& className, const std::vector<unsigned char>& maskData, 
                                    int width, int height, const std::string& outputDir) {
    // Convert RGBA mask to grayscale PPM for visualization
    std::vector<unsigned char> grayMask(width * height * 3);
    int non_zero_count = 0;
    
    for (int i = 0; i < width * height; ++i) {
        unsigned char maskValue = maskData[i * 4]; // Get red channel as mask value
        grayMask[i * 3] = grayMask[i * 3 + 1] = grayMask[i * 3 + 2] = maskValue;
        if (maskValue > 0) non_zero_count++;
    }
    
    std::string filename = outputDir + "/mask_" + className + ".ppm";
    
    // Simple PPM writer
    std::ofstream file(filename, std::ios::binary);
    file << "P6\n" << width << " " << height << "\n255\n";
    file.write(reinterpret_cast<const char*>(grayMask.data()), grayMask.size());
    file.close();
    
    std::cout << "Saved mask debug image: " << filename << ", non-zero pixels: " << non_zero_count << std::endl;
}