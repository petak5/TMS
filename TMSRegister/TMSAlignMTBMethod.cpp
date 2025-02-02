// TMSAlignMTBMethod.cpp: implementation of the TMSAlignMTBMethod class.
//
//////////////////////////////////////////////////////////////////////

#include "TMSAlignMTBMethod.h"
#include <algorithm>
#include <opencv2/optflow.hpp>

TMSAlignMTBMethod::TMSAlignMTBMethod()
{
}

TMSAlignMTBMethod::~TMSAlignMTBMethod()
{
}

std::vector<cv::Mat> TMSAlignMTBMethod::align(std::vector<cv::Mat> images, std::string debugOutput, bool isDebug)
{
    int numImages = images.size();
    std::vector<cv::Mat> alignedImages;
    std::vector<cv::Mat> bwImages;
    std::vector<cv::Mat> downsampledBwImages;

    // OpenCV implementation of MTB calcluates shift of values 0-63 pixels (in negative and positive directions), for larger movements (or high resolution images) need to do rough alignment first on lower resolution
    int resizeRatio = 5;

    for (int i = 0; i < numImages; i++)
    {
        // cv::Mat bwImage;
        cv::Mat downsampledBwImage;

        cv::resize(images[i], downsampledBwImage, cv::Size(images[i].cols / resizeRatio, images[i].rows / resizeRatio));

        // cv::cvtColor(images[i], bwImage, cv::COLOR_BGR2GRAY);
        cv::cvtColor(downsampledBwImage, downsampledBwImage, cv::COLOR_BGR2GRAY);

        // bwImages.push_back(bwImage);
        downsampledBwImages.push_back(downsampledBwImage);
    }

    cv::Ptr<cv::AlignMTB> alignMTB = cv::createAlignMTB();
    cv::Point shift = alignMTB->calculateShift(downsampledBwImages[0], downsampledBwImages[1]);

    if (isDebug)
    {
        std::cout << "Shift between images at " << resizeRatio << "x downsampled resolution: " << shift.x << ", " << shift.y << std::endl;
    }

    // Coordinate (0, 0) is top left

    int w = images[0].cols;
    int h = images[0].rows;

    // Use the shift on full size images
    shift.x = shift.x * resizeRatio;
    shift.y = shift.y * resizeRatio;

    int left1 = std::max(0, shift.x);
    int right1 = std::min(0, shift.x);
    int top1 = std::max(0, shift.y);
    int bottom1 = std::min(0, shift.y);

    int left2 = -right1;
    int right2 = -left1;
    int top2 = -bottom1;
    int bottom2 = -top1;

    if (isDebug)
    {
        std::cout << "Cropping to: " << left1 << " - " << w - 1 + right1 << std::endl;
        std::cout << "Cropping to: " << left2 << " - " << w - 1 + right2 << std::endl;
        std::cout << "Cropping to: " << top1 << " - " << w - 1 + bottom1 << std::endl;
        std::cout << "Cropping to: " << top2 << " - " << w - 1 + bottom2 << std::endl;
    }

    cv::Mat croppedImage1 = images[0](cv::Range(top1, h - 1 + bottom1), cv::Range(left1, w - 1 + right1));
    cv::Mat croppedImage2 = images[1](cv::Range(top2, h - 1 + bottom2), cv::Range(left2, w - 1 + right2));

    if (isDebug)
    {
        std::cout << "Cropped" << std::endl;

        std::string savePath1 = debugOutput + "/cropped1.jpg";
        cv::imwrite(savePath1, croppedImage1);
        std::string savePath2 = debugOutput + "/cropped2.jpg";
        cv::imwrite(savePath2, croppedImage2);
    }

    std::vector<cv::Mat> updatedImages;
    updatedImages.push_back(croppedImage1);
    updatedImages.push_back(croppedImage2);

    for (int i = 0; i < numImages; i++)
    {
        cv::Mat bwImage;

        cv::cvtColor(images[i], bwImage, cv::COLOR_BGR2GRAY);

        bwImages.push_back(bwImage);
    }

    // Finally use MTB align, updated images are already roughly aligned
    alignMTB->process(updatedImages, alignedImages);

    shift = alignMTB->calculateShift(bwImages[0], bwImages[1]);

    if (isDebug)
    {
        std::cout << "Shift between fine-tuned images: " << shift.x << ", " << shift.y << std::endl;
    }

    return alignedImages;
}
