// TMSHDRCreate.cpp: implementation of the TMSHDRCreate class.
//
//////////////////////////////////////////////////////////////////////
#include "TMSHDRCreate.h"

#ifdef LINUX
#include "../TMOLinux/TMOLinux.h"
#endif
#ifndef LINUX
#include "../TMOW32/TMOW32.h"
#endif

#ifndef LINUX
#define OPENEXR_DLL
#else
#define _stricmp strcasecmp
#endif

#include <stdlib.h>
#include <limits.h>
#include <wchar.h>
#include <iostream>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <string.h>
#include <opencv2/opencv.hpp>
#include "SrgbTransform.hpp"

static cv::Mat srgbToLinear(const cv::Mat& img)
{
    cv::Mat result = img.clone();
    float* data = result.ptr<float>();
    int total = result.total() * result.channels();
    for (int i = 0; i < total; i++)
        data[i] = SrgbTransform::srgbToLinear(data[i]);
    return result;
}

TMSHDRCreate::TMSHDRCreate()
{
}

TMSHDRCreate::~TMSHDRCreate()
{
}

int TMSHDRCreate::main(int argc, char *argv[])
{
    std::string outputFileName = "output";
    bool useHDRPlus = false;
    bool useDISMerge = false;
    float disNoise = 0.05f;
    std::vector<std::string> imageFiles;
    std::vector<float> times;

    for (int i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "-o") == 0)
        {
            if (i + 1 >= argc) { Help(); return 1; }
            outputFileName = argv[++i];
        }
        else if (strcmp(argv[i], "--hdrplus") == 0)
        {
            useHDRPlus = true;
        }
        else if (strcmp(argv[i], "--exposure-fusion") == 0)
        {
            useDISMerge = true;
        }
        else if (strcmp(argv[i], "--dis-noise") == 0)
        {
            if (i + 1 >= argc) { Help(); return 1; }
            disNoise = std::stof(argv[++i]);
        }
        else
        {
            imageFiles.push_back(argv[i]);
            // Exposure time is optional
            // Look at the next arg and use it only if it is a number. If not provided, use default value of 1.0
            if (i + 1 < argc)
            {
                try
                {
                    float t = std::stof(argv[i + 1]);
                    times.push_back(t);
                    ++i;
                }
                catch (...) { times.push_back(1.0f); }
            }
            else { times.push_back(1.0f); }
        }
    }

    if (imageFiles.size() < 2)
    {
        Help();
        return 1;
    }
    if (!useHDRPlus && !useDISMerge && imageFiles.size() != 2)
    {
        std::cerr << "Debevec method supports creating HDR from only 2 images." << std::endl;
        return 1;
    }

    // HDR+ and DIS flow merge methods use full bit depth normalized to linear float32 [0,1]
    // For now the basic Debevec HDR merge stays as is with raw 8-bit encoded values for camera response calibration
    bool needLinearFloat = useHDRPlus || useDISMerge;

    std::vector<cv::Mat> images;
    for (const auto& f : imageFiles)
    {
        std::cout << "Reading image: " << f << std::endl;
        cv::Mat img;
        if (needLinearFloat)
        {
            img = cv::imread(f, cv::IMREAD_UNCHANGED);
            if (img.empty())
            {
                std::cerr << "Failed to read: " << f << std::endl;
                return 1;
            }
            double scale = (img.depth() == CV_16U) ? 1.0 / 65535.0 : 1.0 / 255.0;
            img.convertTo(img, CV_32F, scale);
            img = srgbToLinear(img);
        }
        else
        {
            img = cv::imread(f);
            if (img.empty())
            {
                std::cerr << "Failed to read: " << f << std::endl;
                return 1;
            }
        }
        images.push_back(img);
    }

    cv::Mat hdr;
    if (useHDRPlus)
    {
        TMSHDRMergeHDRPlus hdrPlus;
        hdr = hdrPlus.process(images, times);
    }
    else if (useDISMerge)
    {
        TMSHDRMergeDIS dis(disNoise);
        hdr = dis.process(images, times);
    }
    else
    {
        cv::Mat responseDebevec;
        cv::Ptr<cv::CalibrateDebevec> calibrateDebevec = cv::createCalibrateDebevec();
        calibrateDebevec->process(images, responseDebevec, times);

        cv::Ptr<cv::MergeDebevec> mergeDebevec = cv::createMergeDebevec();
        mergeDebevec->process(images, hdr, times, responseDebevec);
    }

    std::string savePath = outputFileName + ".tiff";
    std::cout << "Saving HDR image to: " << savePath << std::endl;
    cv::imwrite(savePath, hdr);

    return 0;
}

void TMSHDRCreate::Help()
{
    std::cout << "Usage:\n"
              << "    ./TMSHDRCreate [-o output] image1 time1 image2 time2\n"
              << "    ./TMSHDRCreate [-o output] --hdrplus image1 [time1] image2 [time2] ...\n"
              << "    ./TMSHDRCreate [-o output] --exposure-fusion image1 [time1] image2 [time2] ...\n\n"
              << "Options:\n"
              << "    -o output               Output filename (without extension, default: output)\n"
              << "    --hdrplus               HDR+ (Hasinoff 2016): hierarchical tile alignment +\n"
              << "                            per-tile frequency-domain Wiener merge. N>=2 images.\n"
              << "    --exposure-fusion       DIS optical flow alignment + spatial Wiener merge. N>=2.\n"
              << "                            Exposure times are optional for both multi-image modes:\n"
              << "                              with times:    linear irradiance output (needs tone mapping)\n"
              << "                              without times: display-ready exposure fusion\n"
              << "    --dis-noise <value>     Noise variance for DIS merge Wiener weight (default: 0.05)\n\n";
}
