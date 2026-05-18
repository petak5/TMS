#pragma once
#include "TMSHDRProgress.h"
#include <opencv2/opencv.hpp>
#include <onnxruntime_cxx_api.h>
#include <string>
#include <vector>

// HDR merge using SAFNet Kong et al., 2024
// Runs exported ONNX model. Large images are processed in overlapping tiles
class TMSHDRMergeSAFNet
{
public:
    explicit TMSHDRMergeSAFNet(const std::string& modelPath, int tileSize = 2048, int overlap  = 128);

    cv::Mat process(const std::vector<cv::Mat>& images, const std::vector<float>& times,
                    ProgressFn progress = nullptr);

private:
    Ort::Env env;
    Ort::Session session;

    int tileSize;
    int overlap;

    std::vector<float> buildTensor(const cv::Mat& ldr, float exposureRatio) const;

    cv::Mat runTile(const std::vector<cv::Mat>& tiles, const std::vector<float>& expos);
};
