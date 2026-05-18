#include "TMSHDRMergeDIS.h"
#include "TMSHDRMergeUtil.h"
#include <iostream>
#include <algorithm>

cv::Mat TMSHDRMergeDIS::process(std::vector<cv::Mat>& images, std::vector<float>& times,
                                 ProgressFn progress)
{
    int n = (int)images.size();
    int toAlign = n - 1;

    if (progress) progress(0, "DIS - optical flow alignment");

    int refIdx = fusionSelectReference(images);
    std::cout << "DIS merge - reference image " << refIdx << std::endl;

    cv::Mat refGray;
    cv::cvtColor(images[refIdx], refGray, cv::COLOR_BGR2GRAY);

    std::vector<cv::Mat> aligned(images.size());
    aligned[refIdx] = images[refIdx].clone();

    int alignedCount = 0;
    for (int i = 0; i < n; i++)
    {
        if (i == refIdx) continue;
        if (progress) progress(5 + alignedCount * 45 / std::max(toAlign, 1),
            "DIS - aligning image " + std::to_string(alignedCount + 1) + "/" + std::to_string(toAlign));
        std::cout << "DIS merge - aligning image " << i << std::endl;
        aligned[i] = alignFrame(refGray, images[i]);
        alignedCount++;
    }

    if (progress) progress(50, "DIS - spatial merge");
    cv::Mat result = mergeFrames(aligned, times, refIdx);
    if (progress) progress(95, "DIS - merge complete");
    return result;
}

// Align src to refGray using DIS optical flow
// Return aligned src frame
cv::Mat TMSHDRMergeDIS::alignFrame(const cv::Mat& refGray, const cv::Mat& src)
{
    // Images are float32 [0,1], but equalizeHist and DIS flow need uint8
    cv::Mat srcGray32;
    cv::cvtColor(src, srcGray32, cv::COLOR_BGR2GRAY);

    cv::Mat refGray8, srcGray8;
    refGray.convertTo(refGray8, CV_8U, 255.0);
    srcGray32.convertTo(srcGray8, CV_8U, 255.0);

    cv::Mat refEq, srcEq;
    cv::equalizeHist(refGray8, refEq);
    cv::equalizeHist(srcGray8, srcEq);

    cv::Mat flow;
    cv::Ptr<cv::DISOpticalFlow> dis = cv::DISOpticalFlow::create(cv::DISOpticalFlow::PRESET_MEDIUM);
    dis->calc(refEq, srcEq, flow);

    // Split x and y displacement channels
    std::vector<cv::Mat> flowCh;
    cv::split(flow, flowCh);

    // Build displacement map for remap function from the flow
    // Use pointers for better performance
    int rows = refGray.rows, cols = refGray.cols;
    cv::Mat mapX(rows, cols, CV_32F);
    cv::Mat mapY(rows, cols, CV_32F);

    for (int y = 0; y < rows; y++)
    {
        const float* fxRow = flowCh[0].ptr<float>(y);
        const float* fyRow = flowCh[1].ptr<float>(y);
        float* mxRow = mapX.ptr<float>(y);
        float* myRow = mapY.ptr<float>(y);
        for (int x = 0; x < cols; x++)
        {
            mxRow[x] = (float)x + fxRow[x];
            myRow[x] = (float)y + fyRow[x];
        }
    }

    cv::Mat warped;
    cv::remap(src, warped, mapX, mapY, cv::INTER_LINEAR, cv::BORDER_REPLICATE);
    return warped;
}

// Merge aligned frames into a 32-bit linear irradiance image.
cv::Mat TMSHDRMergeDIS::mergeFrames(const std::vector<cv::Mat>& aligned, const std::vector<float>& times, int refIdx)
{
    int rows = aligned[refIdx].rows;
    int cols = aligned[refIdx].cols;
    int imagesCount = (int)aligned.size();

    std::cout << "DISMerge - merging " << imagesCount << " images at " << cols << "x" << rows << std::endl;

    cv::Mat refNorm;
    aligned[refIdx].convertTo(refNorm, CV_32FC3, 1.0 / (double)times[refIdx]);

    cv::Mat refGrayNorm;
    cv::cvtColor(refNorm, refGrayNorm, cv::COLOR_BGR2GRAY);
    refNorm.release();

    // 1 if pixel is not clipped or too dark (between 5/255 and 250/255), 0 otherwise.
    cv::Mat refValid;
    cv::Mat refGrayF;
    cv::cvtColor(aligned[refIdx], refGrayF, cv::COLOR_BGR2GRAY);
    cv::inRange(refGrayF, 5.0f / 255.0f, 250.0f / 255.0f, refValid);
    refValid.convertTo(refValid, CV_32F, 1.0f / 255.0f);
    cv::Mat refInvalid;
    cv::subtract(cv::Scalar(1.0f), refValid, refInvalid);

    cv::Mat result = cv::Mat::zeros(rows, cols, CV_32FC3);
    cv::Mat weightSum  = cv::Mat::zeros(rows, cols, CV_32F);

    for (int i = 0; i < imagesCount; i++)
    {
        cv::Mat normI;
        aligned[i].convertTo(normI, CV_32FC3, 1.0 / (double)times[i]);

        // Exposure weight
        // Use ramp from 1.0 at brightness 0.9 down to 0 at 1.0
        // Shadows keep full weight to avoid posterisation from near-zero wtSum
        cv::Mat grayI;
        cv::cvtColor(aligned[i], grayI, cv::COLOR_BGR2GRAY);
        cv::Mat satFraction;
        cv::subtract(grayI, cv::Scalar(0.9f), satFraction);
        grayI.release();
        // Clamp negative values to 0
        cv::threshold(satFraction, satFraction, 0.0, 0.0, cv::THRESH_TOZERO);
        satFraction *= 10.0f;
        cv::Mat expWeight;
        cv::subtract(cv::Scalar(1.0f), satFraction, expWeight);
        satFraction.release();
        cv::threshold(expWeight, expWeight, 0.0, 0.0, cv::THRESH_TOZERO);
        // Small floor so that saturated pixels in every image of the sequence have some normalised average values rather than 0 which would output black pixel (`weightSum == 0` means `result/eps = 0`)
        expWeight += 1e-4f;

        cv::Mat weight;
        if (i == refIdx)
        {
            weight = expWeight;
        }
        else
        {
            // Use Wiener filter to suppress pixels that differ from reference in brightness
            cv::Mat normGrayI;
            cv::cvtColor(normI, normGrayI, cv::COLOR_BGR2GRAY);

            cv::Mat diffNorm;
            cv::subtract(normGrayI, refGrayNorm, diffNorm);
            normGrayI.release();
            cv::Mat diffSq = diffNorm.mul(diffNorm);
            diffNorm.release();

            // Use pixel surroundings for smoother boundaries
            cv::Mat localEnergy;
            int patchSize = 2 * TILE_SIZE + 1;
            cv::boxFilter(diffSq, localEnergy, -1, cv::Size(patchSize, patchSize));
            diffSq.release();

            cv::Mat motionWeight;
            cv::divide((double)noiseVariance, localEnergy + noiseVariance, motionWeight);
            localEnergy.release();

            // Skip penalty when the reference pixel is clipped or dark
            cv::Mat motionWeightAdj = motionWeight.mul(refValid) + refInvalid;
            motionWeight.release();
            weight = expWeight.mul(motionWeightAdj);
        }

        cv::Mat w3;
        cv::merge(std::vector<cv::Mat>{weight, weight, weight}, w3);
        result += normI.mul(w3);
        weightSum += weight;
    }

    // Small value to avoid division by 0
    cv::Mat eps = cv::Mat::ones(rows, cols, CV_32F) * 1e-10f;
    cv::Mat weightSum3;
    cv::merge(std::vector<cv::Mat>{weightSum + eps, weightSum + eps, weightSum + eps}, weightSum3);

    return result / weightSum3;
}
