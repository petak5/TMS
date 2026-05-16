#pragma once
#include <opencv2/opencv.hpp>
#include <vector>

class TMSHDRMergeDIS
{
public:
    explicit TMSHDRMergeDIS(float noise_variance = 0.05f) : noiseVariance(noise_variance) {}
    cv::Mat process(std::vector<cv::Mat>& images, std::vector<float>& times);

private:
    cv::Mat alignFrame(const cv::Mat& refGray, const cv::Mat& src);
    cv::Mat mergeFrames(const std::vector<cv::Mat>& aligned, const std::vector<float>& times, int refIdx);

    static constexpr int TILE_SIZE = 32;
    static constexpr int SEARCH_RADIUS = 64;
    float noiseVariance = 0.05f;
};
