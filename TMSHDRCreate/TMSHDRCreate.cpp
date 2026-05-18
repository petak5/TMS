// TMSHDRCreate.cpp: implementation of the TMSHDRCreate class.
//
//////////////////////////////////////////////////////////////////////
#include "TMSHDRCreate.h"
#include "TMSHDRMergeDIS.h"
#include "TMSHDRMergeHDRPlus.h"
#include "TMSHDRMergeSAFNet.h"

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

std::string TMSHDRCreate::run(const HDRCreateParams& params, ProgressFn progress)
{
    if (progress) progress(0, "Loading images");

    if (params.imagePaths.size() < 2)
        return "At least 2 images required";
    if (params.method == HDRCreateParams::SAFNET && params.imagePaths.size() != 3)
        return "SAFNet requires exactly 3 images";

    bool needLinearFloat = (params.method == HDRCreateParams::HDRPLUS ||
                            params.method == HDRCreateParams::DIS);
    bool needSrgbFloat   = (params.method == HDRCreateParams::SAFNET);

    std::vector<float> times = params.exposureTimes;
    while (times.size() < params.imagePaths.size())
        times.push_back(1.0f);

    std::vector<cv::Mat> images;
    int n = (int)params.imagePaths.size();
    for (int i = 0; i < n; i++)
    {
        const std::string& f = params.imagePaths[i];
        std::cout << "Reading image: " << f << std::endl;
        cv::Mat img;
        if (needLinearFloat)
        {
            img = cv::imread(f, cv::IMREAD_UNCHANGED);
            if (img.empty()) return "Failed to read: " + f;
            double scale = (img.depth() == CV_16U) ? 1.0 / 65535.0 : 1.0 / 255.0;
            img.convertTo(img, CV_32F, scale);
            img = srgbToLinear(img);
        }
        else if (needSrgbFloat)
        {
            img = cv::imread(f, cv::IMREAD_UNCHANGED);
            if (img.empty()) return "Failed to read: " + f;
            double scale = (img.depth() == CV_16U) ? 1.0 / 65535.0 : 1.0 / 255.0;
            img.convertTo(img, CV_32F, scale);
        }
        else
        {
            img = cv::imread(f);
            if (img.empty()) return "Failed to read: " + f;
        }
        images.push_back(img);
        if (progress) progress(5 + (i + 1) * 40 / n,
            "Loading image " + std::to_string(i + 1) + "/" + std::to_string(n));
    }

    // Merge phase occupies 45-90%; merge methods receive a 0-100 sub-range
    auto mergeProg = [&](int pct, const std::string& stage) {
        if (progress) progress(45 + pct * 45 / 100, stage);
    };

    cv::Mat hdr;
    if (params.method == HDRCreateParams::SAFNET)
    {
        TMSHDRMergeSAFNet safnet(params.safnetModel, params.safnetTile, params.safnetOverlap);
        hdr = safnet.process(images, times, mergeProg);
    }
    else if (params.method == HDRCreateParams::HDRPLUS)
    {
        TMSHDRMergeHDRPlus hdrPlus(0.001f, 0.005f, 32, 16, 64, 4, params.hdrplusC);
        hdr = hdrPlus.process(images, times, mergeProg);
    }
    else if (params.method == HDRCreateParams::DIS)
    {
        TMSHDRMergeDIS dis(params.disNoise);
        hdr = dis.process(images, times, mergeProg);
    }
    else
    {
        bool allEqual = true;
        for (int i = 1; i < (int)times.size(); i++)
            if (std::abs(times[i] - times[0]) > 1e-6f) { allEqual = false; break; }
        if (allEqual)
            return "Debevec requires different exposure times for each image "
                   "(all are currently " + std::to_string(times[0]) + " s). "
                   "Set the Exposure Time column to the actual shutter speeds used.";

        mergeProg(0, "Debevec - calibrating camera response");
        cv::Mat responseDebevec;
        cv::Ptr<cv::CalibrateDebevec> calibrateDebevec = cv::createCalibrateDebevec();
        calibrateDebevec->process(images, responseDebevec, times);
        mergeProg(50, "Debevec - merging exposures");
        cv::Ptr<cv::MergeDebevec> mergeDebevec = cv::createMergeDebevec();
        mergeDebevec->process(images, hdr, times, responseDebevec);

        // Debevec produces linear radiance values >> 1.0.
        // Apply global Reinhard TMO (L_out = L_in / (1 + L_in)) so the result
        // is in [0,1) and displays correctly in TMS without a separate tone mapper.
        cv::patchNaNs(hdr, 0.0f);
        hdr = cv::max(hdr, cv::Scalar::all(0.0f));
        hdr = hdr / (hdr + cv::Scalar::all(1.0f));
    }

    if (progress) progress(90, "Saving output");

    static const std::vector<std::string> validExts = {".tiff", ".tif", ".exr", ".hdr"};
    std::string savePath = params.outputPath;
    auto dotPos = savePath.rfind('.');
    std::string ext = (dotPos != std::string::npos) ? savePath.substr(dotPos) : "";
    for (char& c : ext) c = (char)std::tolower((unsigned char)c);
    bool extValid = !ext.empty() &&
                    std::find(validExts.begin(), validExts.end(), ext) != validExts.end();
    if (!extValid)
    {
        if (!ext.empty())
            std::cerr << "Warning: unsupported extension '" << ext << "' — appending .tiff" << std::endl;
        savePath += ".tiff";
    }
    std::cout << "Saving HDR image to: " << savePath << std::endl;
    if (!cv::imwrite(savePath, hdr))
        return "Failed to save output to: " + savePath;

    if (progress) progress(100, "Done");
    return "";
}

int TMSHDRCreate::main(int argc, char *argv[])
{
    HDRCreateParams params;
    bool useHDRPlus = false;
    bool useDISMerge = false;
    bool useSAFNet = false;

    for (int i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "-o") == 0)
        {
            if (i + 1 >= argc) { Help(); return 1; }
            params.outputPath = argv[++i];
        }
        else if (strcmp(argv[i], "--hdrplus") == 0)
        {
            useHDRPlus = true;
        }
        else if (strcmp(argv[i], "--exposure-fusion") == 0)
        {
            useDISMerge = true;
        }
        else if (strcmp(argv[i], "--safnet") == 0)
        {
            useSAFNet = true;
        }
        else if (strcmp(argv[i], "--safnet-model") == 0)
        {
            if (i + 1 >= argc) { Help(); return 1; }
            params.safnetModel = argv[++i];
        }
        else if (strcmp(argv[i], "--safnet-tile") == 0)
        {
            if (i + 1 >= argc) { Help(); return 1; }
            params.safnetTile = std::stoi(argv[++i]);
        }
        else if (strcmp(argv[i], "--safnet-overlap") == 0)
        {
            if (i + 1 >= argc) { Help(); return 1; }
            params.safnetOverlap = std::stoi(argv[++i]);
        }
        else if (strcmp(argv[i], "--dis-noise") == 0)
        {
            if (i + 1 >= argc) { Help(); return 1; }
            params.disNoise = std::stof(argv[++i]);
        }
        else if (strcmp(argv[i], "--hdrplus-c") == 0)
        {
            if (i + 1 >= argc) { Help(); return 1; }
            params.hdrplusC = std::stof(argv[++i]);
        }
        else
        {
            params.imagePaths.push_back(argv[i]);
            if (i + 1 < argc)
            {
                try
                {
                    float t = std::stof(argv[i + 1]);
                    params.exposureTimes.push_back(t);
                    ++i;
                }
                catch (...) { params.exposureTimes.push_back(1.0f); }
            }
            else { params.exposureTimes.push_back(1.0f); }
        }
    }

    if (params.imagePaths.size() < 2) { Help(); return 1; }

    if (useSAFNet)       params.method = HDRCreateParams::SAFNET;
    else if (useHDRPlus) params.method = HDRCreateParams::HDRPLUS;
    else if (useDISMerge) params.method = HDRCreateParams::DIS;

    std::string err = run(params);
    if (!err.empty())
    {
        std::cerr << err << std::endl;
        return 1;
    }
    return 0;
}

void TMSHDRCreate::Help()
{
    std::cout << "Usage:\n"
              << "    ./TMSHDRCreate [-o output] image1 time1 image2 time2 [imageN timeN ...]\n"
              << "    ./TMSHDRCreate [-o output] --hdrplus image1 [time1] image2 [time2] ...\n"
              << "    ./TMSHDRCreate [-o output] --exposure-fusion image1 [time1] image2 [time2] ...\n\n"
              << "Options:\n"
              << "    -o output.tiff          Output filename with extension (.tiff/.tif/.exr/.hdr, default: output.tiff)\n"
              << "    --hdrplus               HDR+ (Hasinoff 2016): hierarchical tile alignment +\n"
              << "                            per-tile frequency-domain Wiener merge. N>=2 images.\n"
              << "    --exposure-fusion       DIS optical flow alignment + spatial Wiener merge. N>=2.\n"
              << "                            Exposure times are optional for both multi-image modes:\n"
              << "                              with times:    linear irradiance output (needs tone mapping)\n"
              << "                              without times: display-ready exposure fusion\n"
              << "    --hdrplus-c <value>     HDR+ Wiener aggressiveness (default: 2.0). Higher = more\n"
              << "                            denoising.\n"
              << "    --dis-noise <value>     Noise variance for DIS merge Wiener weight (default: 0.05)\n"
              << "    --safnet                SAFNet deep alignment + fusion.\n"
              << "                            Requires exactly 3 images (under/mid/over exposure).\n"
              << "    --safnet-model <path>   Path to SAFNet ONNX model (default: SAFNet.onnx)\n"
              << "    --safnet-tile <px>      Tile size for large images (default: 2048)\n"
              << "    --safnet-overlap <px>   Overlap between tiles in pixels (default: 128)\n\n";
}
