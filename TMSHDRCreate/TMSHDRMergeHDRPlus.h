#pragma once
#include "TMSHDRProgress.h"
#include <opencv2/opencv.hpp>
#include <vector>

// HDR+ method based on Hasinoff et al. SIGGRAPH Asia 2016.

class TMSHDRMergeHDRPlus
{
public:
    explicit TMSHDRMergeHDRPlus(float lambda_s = 0.001f,
                                float lambda_r = 0.005f,
                                int align_tile_size = 32,
                                int merge_tile_size = 16,
                                int search_radius = 64,
                                int n_levels = 4,
                                float c_temporal = 2.0f);

    cv::Mat process(std::vector<cv::Mat>& images, std::vector<float>& times,
                    ProgressFn progress = nullptr);

private:
    // Shot noise
    float lambda_s;
    // Read noise
    float lambda_r;
    // Wiener filter denoise strength - higher value denoises more
    float c_temporal;

    // Size of align matching tile
    int ALIGN_TILE;
    int MERGE_TILE;
    int SEARCH_RADIUS;
    int N_LEVELS;

    cv::Mat alignFrame(const cv::Mat& refGray, const cv::Mat& srcGray);

    cv::Mat calculateOffsets(const cv::Mat& refLevel,
                             const cv::Mat& srcLevel,
                             const cv::Mat& initOffsets,
                             int searchRadius);

    cv::Mat upsampleMultiHypothesis(const cv::Mat& coarseOffsets,
                                    const cv::Mat& refFine,
                                    const cv::Mat& srcFine);

    cv::Mat mergeFrames(const std::vector<cv::Mat>& images,
                        const std::vector<cv::Mat>& offsets,
                        const std::vector<float>&   times,
                        int refIdx);
};
