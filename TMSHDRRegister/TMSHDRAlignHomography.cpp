// TMSHDRAlignHomography.cpp: implementation of the TMSHDRAlignHomography class.
//
//////////////////////////////////////////////////////////////////////

#include "TMSHDRAlignHomography.h"
#include <algorithm>

TMSHDRAlignHomography::TMSHDRAlignHomography()
{
}

TMSHDRAlignHomography::~TMSHDRAlignHomography()
{
}

std::vector<cv::Mat> TMSHDRAlignHomography::align(std::vector<cv::Mat> images, int resizeRatio)
{
    int numImages = images.size();
    std::vector<cv::Mat> alignedImages;
    std::vector<cv::Mat> bw_images;

    for (int i = 0; i < numImages; i++)
    {
        cv::Mat bw_image;
        cv::cvtColor(images[i], bw_image, cv::COLOR_BGR2GRAY);
        cv::equalizeHist(bw_image, bw_image);
        cv::resize(bw_image, bw_image, cv::Size(bw_image.cols / resizeRatio, bw_image.rows / resizeRatio));
        bw_images.push_back(bw_image);
    }

    std::cout << "Initializing SIFT..." << std::endl;
    cv::Ptr<cv::SIFT> sift = cv::SIFT::create();

    std::vector<std::vector<cv::KeyPoint>> keypoints(numImages);
    std::vector<cv::Mat> descriptors(numImages);

    for (int i = 0; i < numImages; i++)
    {
        std::cout << "Detecting features in image " << i << "..." << std::endl;
        sift->detectAndCompute(bw_images[i], cv::noArray(), keypoints[i], descriptors[i]);
        std::cout << "Features: " << keypoints[i].size() << std::endl;
    }

    cv::FlannBasedMatcher flann(
        cv::makePtr<cv::flann::KDTreeIndexParams>(5),
        cv::makePtr<cv::flann::SearchParams>(50));

    const int MIN_MATCH_COUNT = 10;
    const float w = (float)images[0].cols;
    const float h = (float)images[0].rows;

    float crop_left = 0, crop_top = 0, crop_right = w, crop_bottom = h;

    std::vector<cv::Mat> warpedImages;
    warpedImages.push_back(images[0]);

    for (int i = 1; i < numImages; i++)
    {
        std::cout << "Matching image " << i << " against reference..." << std::endl;

        std::vector<std::vector<cv::DMatch>> matches;
        flann.knnMatch(descriptors[i], descriptors[0], matches, 2);

        std::vector<cv::DMatch> goodMatches;
        for (const auto& m : matches)
            if (m[0].distance < 0.7 * m[1].distance)
                goodMatches.push_back(m[0]);

        std::cout << "Good matches: " << goodMatches.size() << std::endl;

        if ((int)goodMatches.size() <= MIN_MATCH_COUNT)
        {
            std::cout << "Not enough matches for image " << i << " - " << goodMatches.size() << "/" << MIN_MATCH_COUNT << std::endl;
            return alignedImages;
        }

        std::vector<cv::Point2f> src_pts, dst_pts;
        for (const auto& m : goodMatches)
        {
            src_pts.push_back(keypoints[i][m.queryIdx].pt * resizeRatio);
            dst_pts.push_back(keypoints[0][m.trainIdx].pt * resizeRatio);
        }

        cv::Mat mask;
        cv::Mat M = cv::findHomography(src_pts, dst_pts, cv::RANSAC, 5.0, mask);
        if (M.empty())
        {
            std::cout << "Homography failed for image " << i << std::endl;
            return alignedImages;
        }

        cv::Mat warped;
        cv::warpPerspective(images[i], warped, M, cv::Size(images[0].cols, images[0].rows),
                            cv::INTER_LINEAR, cv::BORDER_REPLICATE);
        warpedImages.push_back(warped);

        // Find crop region
        std::vector<cv::Point2f> corners = {
            {0.f, 0.f},
            {0.f, (float)(images[i].rows - 1)},
            {(float)(images[i].cols - 1), 0.f},
            {(float)(images[i].cols - 1), (float)(images[i].rows - 1)}
        };
        std::vector<cv::Point2f> tc;
        cv::perspectiveTransform(corners, tc, M);

        crop_left   = std::max(crop_left,   std::ceil(std::max({0.f, tc[0].x, tc[1].x})));
        crop_right  = std::min(crop_right,  std::floor(std::min({w,  tc[2].x, tc[3].x})));
        crop_top    = std::max(crop_top,    std::ceil(std::max({0.f, tc[0].y, tc[2].y})));
        crop_bottom = std::min(crop_bottom, std::floor(std::min({h,  tc[1].y, tc[3].y})));
    }

    const int margin = 1;
    cv::Rect cropRect(
        (int)crop_left + margin,
        (int)crop_top + margin,
        (int)(crop_right - crop_left) - 2 * margin,
        (int)(crop_bottom - crop_top) - 2 * margin);

    for (const auto& img : warpedImages)
        alignedImages.push_back(img(cropRect));

    return alignedImages;
}

