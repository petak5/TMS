// TMSHDRAlignOpticalFlow.cpp: implementation of the TMSHDRAlignOpticalFlow class.
//
//////////////////////////////////////////////////////////////////////

#include "TMSHDRAlignOpticalFlow.h"
#include <opencv2/optflow.hpp>

TMSHDRAlignOpticalFlow::TMSHDRAlignOpticalFlow()
{
}

TMSHDRAlignOpticalFlow::~TMSHDRAlignOpticalFlow()
{
}

std::vector<cv::Mat> TMSHDRAlignOpticalFlow::align(std::vector<cv::Mat> images, int resizeRatio)
{
    int numImages = images.size();
    std::vector<cv::Mat> bwImages;

    for (int i = 0; i < numImages; i++)
    {
        cv::Mat bwImage;
        cv::cvtColor(images[i], bwImage, cv::COLOR_BGR2GRAY);
        cv::equalizeHist(bwImage, bwImage);

        if (resizeRatio > 1)
        {
            std::cout << "Adjusting image for OpticalFlow" << std::endl;
            cv::resize(bwImage, bwImage, cv::Size(bwImage.cols / resizeRatio, bwImage.rows / resizeRatio));
            std::cout << "Resized size: " << bwImage.size() << std::endl;
        }

        bwImages.push_back(bwImage);
    }

    cv::Ptr<cv::DISOpticalFlow> dis = cv::DISOpticalFlow::create(cv::DISOpticalFlow::PRESET_MEDIUM);

    std::vector<cv::Mat> alignedImages;
    alignedImages.push_back(images[0]);

    for (int i = 1; i < numImages; i++)
    {
        cv::Mat flow;
        dis->calc(bwImages[0], bwImages[i], flow);

        // Scale flow back up to full image resolution
        if (resizeRatio > 1)
        {
            cv::resize(flow, flow, images[0].size());
            flow *= static_cast<float>(resizeRatio);
        }

        cv::Mat mapX(images[0].rows, images[0].cols, CV_32FC1);
        cv::Mat mapY(images[0].rows, images[0].cols, CV_32FC1);

        for (int y = 0; y < flow.rows; y++)
        {
            for (int x = 0; x < flow.cols; x++)
            {
                cv::Point2f f = flow.at<cv::Point2f>(y, x);
                mapX.at<float>(y, x) = static_cast<float>(x) + f.x;
                mapY.at<float>(y, x) = static_cast<float>(y) + f.y;
            }
        }

        cv::Mat warpedImg;
        cv::remap(images[i], warpedImg, mapX, mapY, cv::INTER_LINEAR, cv::BORDER_REPLICATE);

        alignedImages.push_back(warpedImg);
    }

    return alignedImages;
}
