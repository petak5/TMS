#pragma once
#include <opencv2/opencv.hpp>
#include <vector>

// Calculate gradient strength on a grayscale version of the image. Higher value means sharper image
inline float imageSharpness(const cv::Mat& image)
{
    cv::Mat gray;
    cv::Mat gx;
    cv::Mat gy;
    cv::Mat mag;
    cv::cvtColor(image, gray, cv::COLOR_BGR2GRAY);
    gray.convertTo(gray, CV_32F);
    cv::Sobel(gray, gx, CV_32F, 1, 0);
    cv::Sobel(gray, gy, CV_32F, 0, 1);
    cv::magnitude(gx, gy, mag);
    return (float)cv::mean(mag)[0];
}

// Returns the index of the sharpest frame
inline int fusionSelectReference(const std::vector<cv::Mat>& images)
{
    int best = 0;
    float bestScore = -1.0f;
    for (int i = 0; i < (int)images.size(); i++)
    {
        float s = imageSharpness(images[i]);
        if (s > bestScore) { bestScore = s; best = i; }
    }
    return best;
}
