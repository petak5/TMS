// TMSHDRRegister.cpp: implementation of the TMSHDRRegister class.
//
//////////////////////////////////////////////////////////////////////
#include "TMSHDRRegister.h"

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

#include "TMSHDRAlignHomography.h"
#include "TMSHDRAlignOpticalFlow.h"
#include "TMSHDRAlignMTB.h"

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

TMSHDRRegister::TMSHDRRegister()
{
}

TMSHDRRegister::~TMSHDRRegister()
{
}

const int RESIZE_RATIO = 2;

int TMSHDRRegister::main(int argc, char *argv[])
{
    std::string inputFileName1;
    std::string inputFileName2;
    std::string outputFolder = "./aligned/";

    // Parse args
    if (argc == 3)
    {
        inputFileName1 = argv[1];
        inputFileName2 = argv[2];
    }
    else if (argc == 5)
    {
        if (strcmp(argv[1], "-o") == 0)
        {
            outputFolder = argv[2];

            inputFileName1 = argv[3];
            inputFileName2 = argv[4];
        }
        else if (strcmp(argv[3], "-o") == 0)
        {
            outputFolder = argv[4];

            inputFileName1 = argv[1];
            inputFileName2 = argv[2];
        }
        else
        {
            Help();
            return 1;
        }
    }
    else
    {
        Help();
        return 1;
    }

    std::vector<cv::Mat> images;

    int numImages = 2;

    // List of image filenames
    static const char *filenames[] = {inputFileName1.c_str(), inputFileName2.c_str()};
    for (int i = 0; i < numImages; i++)
    {
        std::cout << "Reading image: " << filenames[i] << std::endl;
        cv::Mat im = cv::imread(filenames[i]);
        images.push_back(im);
    }

    if (images[0].empty() || images[1].empty())
    {
        std::cout << "Error loading images." << std::endl;
        return 1;
    }

    std::cout << "Done loading images" << std::endl;

    // Feature points with homography and Optical Flow
    TMSHDRAlignHomography alignMethod;
    // TMSHDRAlignOpticalFlow alignMethod;
    std::vector<cv::Mat> alignedImages = alignMethod.align(images, RESIZE_RATIO);

    // MTB
    // TMSHDRAlignMTB alignMethod;
    // std::vector<cv::Mat> alignedImages = alignMethod.align(images);

    // Save cropped images
    cv::imwrite(outputFolder + "/cropped_s.jpg", alignedImages[0]);
    cv::imwrite(outputFolder + "/cropped_t.jpg", alignedImages[1]);

    std::cout << "Done all" << std::endl;

    return 0;
}

std::string TMSHDRRegister::alignToFolder(
    const std::vector<std::string>& inputPaths,
    RegistrationMethod method,
    const std::string& outputFolder,
    std::vector<std::string>& alignedPaths,
    ProgressFn progress)
{
    if (inputPaths.size() < 2)
        return "At least 2 images required for registration";

    if (progress) progress(0, "Registering images");

    std::vector<cv::Mat> images;
    for (const auto& path : inputPaths)
    {
        cv::Mat img = cv::imread(path);
        if (img.empty()) return "Failed to load: " + path;
        images.push_back(img);
    }

    struct stat st = {};
    if (stat(outputFolder.c_str(), &st) != 0)
        mkdir(outputFolder.c_str(), 0755);

    const char* methodName = (method == RegistrationMethod::HOMOGRAPHY) ? "Homography"
                           : (method == RegistrationMethod::OPTICAL_FLOW) ? "Optical Flow"
                           : "MTB";
    if (progress) progress(10, std::string("Registering images (") + methodName + ")");

    std::vector<cv::Mat> aligned;
    if (method == RegistrationMethod::MTB)
    {
        if (images.size() == 2)
        {
            TMSHDRAlignMTB alignMTB;
            aligned = alignMTB.align(images);
        }
        else
        {
            cv::Ptr<cv::AlignMTB> alignMTB = cv::createAlignMTB();
            alignMTB->process(images, aligned);
        }
    }
    else if (method == RegistrationMethod::HOMOGRAPHY)
    {
        TMSHDRAlignHomography alignH;
        aligned = alignH.align(images, RESIZE_RATIO);
    }
    else
    {
        TMSHDRAlignOpticalFlow alignOF;
        aligned = alignOF.align(images, RESIZE_RATIO);
    }

    if (progress) progress(80, "Saving aligned images");

    alignedPaths.clear();
    for (size_t i = 0; i < aligned.size(); i++)
    {
        std::string outPath = outputFolder + "/aligned_" + std::to_string(i) + ".png";
        if (!cv::imwrite(outPath, aligned[i]))
            return "Failed to save aligned image: " + outPath;
        alignedPaths.push_back(outPath);
    }

    if (progress) progress(95, "Registration complete");
    return "";
}

void TMSHDRRegister::Help()
{
    std::cout << "Usage:\n"
              << "    ./TMSHDRRegister [-o outputFolder] image1 image2\n\n";
}
