#include <onnxruntime_cxx_api.h>
#include <map>
#include <set>


struct BBox {
    int x, y, w, h;
};

class ObjectDetector {
private:
    Ort::Env env;
    Ort::SessionOptions session_options;
    Ort::Session session;
    std::vector<std::string> classLabels;
    float confidenceThreshold;
    float nmsThreshold;

public:
    ObjectDetector(const std::string& modelPath, const std::string& classLabelsPath);
    ~ObjectDetector();

    void detect(const uint8_t* frame, int frameWidth, int frameHeight, int frameChannels,
                           const std::set<std::string>& shaderClasses,
                           std::map<std::string, std::vector<std::vector<unsigned char>>>& classMasks,
                           int outputWidth, int outputHeight);

    const float CONF_THRESH = 0.5f;
    const int TARGET_SIZE = 640;

};


struct Detection {
        float x1, y1, x2, y2;
        int class_id;
        float prob;
        std::vector<float> mask_data;
        std::string label;
    };
