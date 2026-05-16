// TMSHDRAlignOpticalFlow.cpp: implementation of the TMSHDRAlignOpticalFlow class.
//
//////////////////////////////////////////////////////////////////////

#include "TMSHDRAlignOpticalFlow.h"
#include <algorithm>
#include <opencv2/optflow.hpp>

TMSHDRAlignOpticalFlow::TMSHDRAlignOpticalFlow()
{
}

TMSHDRAlignOpticalFlow::~TMSHDRAlignOpticalFlow()
{
}

std::vector<cv::Mat> TMSHDRAlignOpticalFlow::align(std::vector<cv::Mat> images, int resizeRatio, std::string debugOutput, bool isDebug)
{
    int numImages = images.size();
    std::vector<cv::Mat> alignedImages;
    std::vector<cv::Mat> bwImages;

    // TODO: resizing currently affect output image size, change it to just internal speed optimization
    for (int i = 0; i < numImages; i++)
    {
        std::cout << "Original size: " << images[i].size() << std::endl;
        std::cout << "Adjusting image for OpticalFlow" << std::endl;

        cv::Mat bwImage;

        cv::cvtColor(images[i], bwImage, cv::COLOR_BGR2GRAY);

        cv::equalizeHist(bwImage, bwImage);

        cv::resize(images[i], images[i], cv::Size(images[i].cols / resizeRatio, images[i].rows / resizeRatio));
        cv::resize(bwImage, bwImage, cv::Size(bwImage.cols / resizeRatio, bwImage.rows / resizeRatio));
        std::cout << "Resized size: " << bwImage.size() << std::endl;

        bwImages.push_back(bwImage);
    }

    cv::Mat flow;

    // Farneback - unusable
    // calcOpticalFlowFarneback(bwImages[0], bwImages[1], flow, 0.5, 3, 15, 3, 5, 1.2, 0);

    // IDK - unusable
    // cv::Ptr<cv::optflow::DualTVL1OpticalFlow> tvl1 = cv::optflow::createOptFlow_DualTVL1();
    // tvl1->calc(bwImages[0], bwImages[1], flow);

    // Lucas-Kanade
    // std::vector<uchar> status;
    // std::vector<float> err;
    // cv::goodFeaturesToTrack(bwImages[0], prevPoints, 100, 0.01, 10);
    // cv::calcOpticalFlowPyrLK(bwImages[0], bwImages[1], prevPoints, nextPoints, status, err);

    // DIS - really good
    cv::Ptr<cv::DISOpticalFlow> dis = cv::DISOpticalFlow::create(cv::DISOpticalFlow::PRESET_MEDIUM);
    dis->calc(bwImages[0], bwImages[1], flow);

    // PCAFlow - very broad patches, not good at all
    // cv::Ptr<cv::DenseOpticalFlow> pcaFlow = cv::optflow::createOptFlow_PCAFlow();
    // pcaFlow->calc(bwImages[0], bwImages[1], flow);

    // DeepFlow - really good
    // cv::Ptr<cv::DenseOpticalFlow> deepFlow = cv::optflow::createOptFlow_DeepFlow();
    // deepFlow->calc(bwImages[0], bwImages[1], flow);

    // SparseToDense - unusable
    // cv::optflow::calcOpticalFlowSparseToDense(bwImages[0], bwImages[1], flow);

    // Visualization part
    cv::Mat flow_parts[2];
    cv::split(flow, flow_parts);

    // Convert the algorithm's output into Polar coordinates
    cv::Mat magnitude, angle, magn_norm;
    cv::cartToPolar(flow_parts[0], flow_parts[1], magnitude, angle, true);
    cv::normalize(magnitude, magn_norm, 0.0f, 1.0f, cv::NORM_MINMAX);
    angle *= ((1.f / 360.f) * (180.f / 255.f));

    // Build hsv image
    cv::Mat _hsv[3], hsv, hsv8, bgr;
    _hsv[0] = angle;
    _hsv[1] = cv::Mat::ones(angle.size(), CV_32F);
    _hsv[2] = magn_norm;
    cv::merge(_hsv, 3, hsv);
    hsv.convertTo(hsv8, CV_8U, 255.0);

    // Display the results
    cv::cvtColor(hsv8, bgr, cv::COLOR_HSV2BGR);
    if (isDebug)
    {
        std::string save_path = debugOutput + "/of_frame.jpg";
        cv::imwrite(save_path, bgr);
    }

    // Create a map for remapping
    cv::Mat mapX(flow.size(), CV_32FC1);
    cv::Mat mapY(flow.size(), CV_32FC1);

    for (int y = 0; y < flow.rows; y++)
    {
        for (int x = 0; x < flow.cols; x++)
        {
            cv::Point2f flowAtPoint = flow.at<cv::Point2f>(y, x);
            mapX.at<float>(y, x) = static_cast<float>(x + flowAtPoint.x);
            mapY.at<float>(y, x) = static_cast<float>(y + flowAtPoint.y);
        }
    }

    // Warp img1 to img2 using the flow map
    cv::Mat warpedImg;
    cv::remap(images[1], warpedImg, mapX, mapY, cv::INTER_LINEAR);

    std::string save_path = debugOutput + "/warped.jpg";
    cv::imwrite(save_path, warpedImg);

    exit(1);

    // alignedImages.push_back(cropped_aligned_s);
    // alignedImages.push_back(cropped_t);

    return alignedImages;
}
