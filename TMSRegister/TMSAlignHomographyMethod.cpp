// TMSAlignHomographyMethod.cpp: implementation of the TMSCreateHDR class.
//
//////////////////////////////////////////////////////////////////////

#include "TMSAlignHomographyMethod.h"
#include <algorithm>

TMSAlignHomographyMethod::TMSAlignHomographyMethod()
{
}

TMSAlignHomographyMethod::~TMSAlignHomographyMethod()
{
}

std::vector<cv::Mat> TMSAlignHomographyMethod::align(std::vector<cv::Mat> images, int resizeRatio, std::string debugOutput, bool isDebug)
{
    int numImages = images.size();
    std::vector<cv::Mat> alignedImages;
    std::vector<cv::Mat> bw_images;

    for (int i = 0; i < numImages; i++)
    {
        std::cout << "Original size: " << images[i].size() << std::endl;
        std::cout << "Adjusting image for SIFT" << std::endl;

        cv::Mat bw_image;

        cv::cvtColor(images[i], bw_image, cv::COLOR_BGR2GRAY);

        cv::equalizeHist(bw_image, bw_image);

        cv::resize(bw_image, bw_image, cv::Size(bw_image.cols / resizeRatio, bw_image.rows / resizeRatio));
        std::cout << "Resized size: " << bw_image.size() << std::endl;

        bw_images.push_back(bw_image);
    }

    std::cout << "Initializing SIFT..." << std::endl;
    cv::Ptr<cv::SIFT> sift = cv::SIFT::create();
    std::cout << "Done" << std::endl;

    std::vector<std::vector<cv::KeyPoint>> keypoints;
    std::vector<cv::Mat> descriptors;

    for (int i = 0; i < numImages; i++)
    {
        std::cout << "Detecting features in image " << i << " ..." << std::endl;

        std::vector<cv::KeyPoint> keypoint;
        cv::Mat descriptor;

        sift->detectAndCompute(bw_images[i], cv::noArray(), keypoint, descriptor);

        std::cout << "Done\nFeatures: " << keypoint.size() << std::endl;

        keypoints.push_back(keypoint);
        descriptors.push_back(descriptor);
    }

    std::cout << "Matching features..." << std::endl;
    cv::FlannBasedMatcher flann(
        cv::makePtr<cv::flann::KDTreeIndexParams>(5),
        cv::makePtr<cv::flann::SearchParams>(50));

    std::vector<std::vector<cv::DMatch>> matches;
    flann.knnMatch(descriptors[0], descriptors[1], matches, 2);
    std::cout << "Done\nMatches: " << matches.size() << std::endl;

    std::vector<cv::DMatch> goodMatches;
    for (const auto &m : matches)
    {
        if (m[0].distance < 0.7 * m[1].distance)
        {
            goodMatches.push_back(m[0]);
        }
    }

    cv::Mat mask;
    cv::Mat M;
    const int MIN_MATCH_COUNT = 10;
    if (goodMatches.size() > MIN_MATCH_COUNT)
    {
        std::vector<cv::Point2f> src_pts, dst_pts;

        for (const auto &m : goodMatches)
        {
            src_pts.push_back(keypoints[0][m.queryIdx].pt * resizeRatio);
            dst_pts.push_back(keypoints[1][m.trainIdx].pt * resizeRatio);
        }

        M = cv::findHomography(src_pts, dst_pts, cv::RANSAC, 5.0, mask);
    }
    else
    {
        std::cout << "Not enough matches are found - " << goodMatches.size() << "/" << MIN_MATCH_COUNT << std::endl;
        return alignedImages;
    }

    if (isDebug)
    {
        std::cout << "Drawing matches..." << std::endl;

        cv::Mat imgKeypoints1, imgKeypoints2;
        drawKeypoints(bw_images[0], keypoints[0], imgKeypoints1, cv::Scalar::all(-1), cv::DrawMatchesFlags::NOT_DRAW_SINGLE_POINTS);
        drawKeypoints(bw_images[1], keypoints[1], imgKeypoints2, cv::Scalar::all(-1), cv::DrawMatchesFlags::NOT_DRAW_SINGLE_POINTS);
        cv::imwrite(debugOutput + "/keypoints1.jpg", imgKeypoints1);
        cv::imwrite(debugOutput + "/keypoints2.jpg", imgKeypoints2);

        cv::Mat imgMatches;
        cv::drawMatches(bw_images[0], keypoints[0], bw_images[1], keypoints[1], goodMatches, imgMatches, cv::Scalar::all(-1), cv::Scalar::all(-1), mask, cv::DrawMatchesFlags::NOT_DRAW_SINGLE_POINTS);

        cv::imwrite(debugOutput + "/matches.jpg", imgMatches);

        std::cout << "Done" << std::endl;
    }

    // Source image (index 0) is warped to match Target image (index 1)

    // Cropped and aligned
    cv::Mat uncropped_aligned_s = align_image(images[0], M);
    cv::Mat cropped_aligned_s = crop_image(uncropped_aligned_s, M);
    cv::Mat cropped_t = crop_image(images[1], M);

    if (isDebug)
    {
        // Save uncropped and aligned images
        cv::imwrite(debugOutput + "/uncropped_target.jpg", images[1]);
        cv::imwrite(debugOutput + "/uncropped_aligned_s.jpg", uncropped_aligned_s);
    }

    if (isDebug)
    {
        // Extended and aligned images
        cv::Mat extended_aligned_s, extended_t;
        std::tie(extended_aligned_s, extended_t) = align_and_resize_images(images[0], images[1], M);

        // Save extended aligned images
        cv::imwrite(debugOutput + "/extended_aligned_s.jpg", extended_aligned_s);
        cv::imwrite(debugOutput + "/extended_t.jpg", extended_t);

        // Diff images
        cv::Mat diff_orig, diff_cropped_1, diff_cropped_2, diff_extended;
        cv::absdiff(images[0], images[1], diff_orig);
        cv::absdiff(cropped_aligned_s, cropped_t, diff_cropped_1);
        cv::absdiff(cropped_t, cropped_aligned_s, diff_cropped_2);
        cv::absdiff(extended_aligned_s, extended_t, diff_extended);

        // Save diff images
        cv::imwrite(debugOutput + "/diff_orig.jpg", diff_orig);
        cv::imwrite(debugOutput + "/diff_cropped_1.jpg", diff_cropped_1);
        cv::imwrite(debugOutput + "/diff_cropped_2.jpg", diff_cropped_2);
        cv::imwrite(debugOutput + "/diff_extended.jpg", diff_extended);
    }

    alignedImages.push_back(cropped_aligned_s);
    alignedImages.push_back(cropped_t);

    return alignedImages;
}

cv::Mat TMSAlignHomographyMethod::align_image(const cv::Mat &image, const cv::Mat &homography)
{
    cv::Mat aligned;
    cv::warpPerspective(image, aligned, homography, cv::Size(image.cols, image.rows));
    return aligned;
}

std::pair<cv::Mat, cv::Mat> TMSAlignHomographyMethod::align_and_resize_images(const cv::Mat &image_s, const cv::Mat &image_t, const cv::Mat &homography)
{
    // Get the dimensions of the source image
    float h = image_s.rows;
    float w = image_s.cols;

    // Define the four corners of the source image
    std::vector<cv::Point2f> points = {cv::Point2f(0, 0), cv::Point2f(0, h - 1), cv::Point2f(w - 1, 0), cv::Point2f(w - 1, h - 1)};

    // Transform the points using the homography matrix
    std::vector<cv::Point2f> points_transformed;
    cv::perspectiveTransform(points, points_transformed, homography);

    // Compute the bounding box of the transformed points
    int top = std::ceil(std::abs(0 + std::min({0.f, points_transformed[0].y, points_transformed[1].y, points_transformed[2].y, points_transformed[3].y})));
    int bottom = std::ceil(std::abs(h - std::max({h, points_transformed[0].y, points_transformed[1].y, points_transformed[2].y, points_transformed[3].y})));
    int left = std::ceil(std::abs(0 - std::min({0.f, points_transformed[0].x, points_transformed[1].x, points_transformed[2].x, points_transformed[3].x})));
    int right = std::ceil(std::abs(w - std::max({w, points_transformed[0].x, points_transformed[1].x, points_transformed[2].x, points_transformed[3].x})));

    // Extend the source image by adding borders
    cv::Mat extended_image;
    cv::copyMakeBorder(image_s, extended_image, top, bottom, left, right, cv::BORDER_CONSTANT, cv::Scalar(0));

    // Adjust the points by adding the offsets (left, top)
    for (auto &pt : points)
    {
        pt.x += left;
        pt.y += top;
    }
    for (auto &pt : points_transformed)
    {
        pt.x += left;
        pt.y += top;
    }

    // Compute the new homography using the transformed points
    cv::Mat new_homography = cv::findHomography(points, points_transformed, cv::RANSAC, 5.0);

    // Warp the extended source image using the new homography
    cv::Mat aligned_image;
    cv::warpPerspective(extended_image, aligned_image, new_homography, cv::Size(w + left + right, h + top + bottom));

    // Extend the target image by adding borders
    cv::Mat extended_t;
    cv::copyMakeBorder(image_t, extended_t, top, bottom, left, right, cv::BORDER_CONSTANT, cv::Scalar(0));

    // Return the aligned image and extended target image
    return std::make_pair(aligned_image, extended_t);
}

cv::Mat TMSAlignHomographyMethod::crop_image(const cv::Mat &image, const cv::Mat &homography)
{
    // Get the dimensions of the image
    float h = image.rows;
    float w = image.cols;

    // Define the four corners of the image
    std::vector<cv::Point2f> points = {cv::Point2f(0, 0), cv::Point2f(0, h - 1), cv::Point2f(w - 1, 0), cv::Point2f(w - 1, h - 1)};

    // Transform the points using the homography matrix
    std::vector<cv::Point2f> points_transformed;
    cv::perspectiveTransform(points, points_transformed, homography);

    // Calculate the bounding box of the transformed points
    float left_x = std::ceil(std::max(std::max(0.f, points_transformed[0].x), points_transformed[1].x));
    float right_x = std::floor(std::min(std::min(w, points_transformed[2].x), points_transformed[3].x));
    float top_y = std::ceil(std::max(std::max(0.f, points_transformed[0].y), points_transformed[2].y));
    float bottom_y = std::floor(std::min(std::min(h, points_transformed[1].y), points_transformed[3].y));

    // Calculate how much to crop from the right and bottom
    int from_right = w - right_x;
    int from_bottom = h - bottom_y;

    // Crop the image using the calculated bounding box
    cv::Rect crop_region(left_x, top_y, right_x - left_x, bottom_y - top_y);
    cv::Mat cropped_image = image(crop_region);

    return cropped_image;
}