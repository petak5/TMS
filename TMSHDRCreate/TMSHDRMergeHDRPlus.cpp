#include "TMSHDRMergeHDRPlus.h"
#include "TMSHDRMergeUtil.h"
#include <iostream>
#include <algorithm>
#include <cmath>
#include <Eigen/Dense>

// Compute modulus squared on each element of M (CV_32FC2)
static cv::Mat complexAbsSq(const cv::Mat& M)
{
    std::vector<cv::Mat> ch;
    cv::split(M, ch);
    return ch[0].mul(ch[0]) + ch[1].mul(ch[1]);
}

// Multiply complex and real parts of C (CV_32FC2) by R (CV_32F)
static cv::Mat complexMulReal(const cv::Mat& C, const cv::Mat& R)
{
    std::vector<cv::Mat> ch;
    cv::split(C, ch);
    ch[0] = ch[0].mul(R);
    ch[1] = ch[1].mul(R);
    cv::Mat out;
    cv::merge(ch, out);
    return out;
}

// Root Mean Square of tile values
static float tileRMS(const cv::Mat& tile)
{
    cv::Mat sq;
    cv::multiply(tile, tile, sq);
    return std::sqrt((float)cv::mean(sq)[0]);
}

TMSHDRMergeHDRPlus::TMSHDRMergeHDRPlus(float ls,
                                       float lr,
                                       int align_tile,
                                       int merge_tile,
                                       int search_radius,
                                       int n_levels,
                                       float c_temporal)
    : lambda_s(ls),
      lambda_r(lr),
      ALIGN_TILE(align_tile),
      MERGE_TILE(merge_tile),
      SEARCH_RADIUS(search_radius),
      N_LEVELS(n_levels),
      c_temporal(c_temporal)
{}

cv::Mat TMSHDRMergeHDRPlus::process(std::vector<cv::Mat>& images, std::vector<float>& times)
{
    int refIdx = fusionSelectReference(images);
    std::cout << "HDR+ - reference image: " << refIdx << std::endl;

    // Images are float32 [0,1] linear but equalizeHist needs uint8.
    auto toEqualizedGray = [](const cv::Mat& bgr) -> cv::Mat {
        cv::Mat gray32, gray8, eq;
        cv::cvtColor(bgr, gray32, cv::COLOR_BGR2GRAY);
        gray32.convertTo(gray8, CV_8U, 255.0);
        cv::equalizeHist(gray8, eq);
        cv::Mat out;
        eq.convertTo(out, CV_32F, 1.0 / 255.0);
        return out;
    };

    cv::Mat refGray = toEqualizedGray(images[refIdx]);

    std::vector<cv::Mat> offsets(images.size());
    for (int i = 0; i < (int)images.size(); i++)
    {
        if (i == refIdx) continue;
        std::cout << "HDR+ - aligning image " << i << std::endl;
        offsets[i] = alignFrame(refGray, toEqualizedGray(images[i]));
    }

    return mergeFrames(images, offsets, times, refIdx);
}

cv::Mat TMSHDRMergeHDRPlus::alignFrame(const cv::Mat& refGray, const cv::Mat& srcGray)
{
    std::vector<cv::Mat> refPyr(N_LEVELS);
    std::vector<cv::Mat> srcPyr(N_LEVELS);
    refPyr[0] = refGray;
    srcPyr[0] = srcGray;
    for (int l = 1; l < N_LEVELS; l++)
    {
        cv::pyrDown(refPyr[l-1], refPyr[l]);
        cv::pyrDown(srcPyr[l-1], srcPyr[l]);
    }

    cv::Mat offsets;

    // Start from top of pyramid
    for (int l = N_LEVELS - 1; l >= 0; l--)
    {
        int sr = (l == N_LEVELS - 1) ? SEARCH_RADIUS : 2;
        offsets = calculateOffsets(refPyr[l], srcPyr[l], offsets, sr);

        // Use median-filter to remove outliers (featureless regions that matched randomly)
        if (offsets.rows >= 3 && offsets.cols >= 3)
        {
            std::vector<cv::Mat> ch;
            cv::split(offsets, ch);
            cv::medianBlur(ch[0], ch[0], 3);
            cv::medianBlur(ch[1], ch[1], 3);
            cv::merge(ch, offsets);
        }

        if (l > 0)
        {
            offsets = upsampleMultiHypothesis(offsets, refPyr[l-1], srcPyr[l-1]);
        }
    }

    return offsets;
}

cv::Mat TMSHDRMergeHDRPlus::calculateOffsets(const cv::Mat& ref, const cv::Mat& src, const cv::Mat& initOffsets, int searchRadius)
{
    int rows = ref.rows;
    int cols = ref.cols;
    int nTilesY = (int)std::ceil((float)rows / ALIGN_TILE);
    int nTilesX = (int)std::ceil((float)cols / ALIGN_TILE);

    // Compute max absolute initial offset to determine required padding
    float maxAbsOffset = 0;
    if (!initOffsets.empty())
    {
        for (int ty = 0; ty < initOffsets.rows; ty++)
            for (int tx = 0; tx < initOffsets.cols; tx++)
            {
                cv::Vec2f v = initOffsets.at<cv::Vec2f>(ty, tx);
                maxAbsOffset = std::max(maxAbsOffset, std::abs(v[0]));
                maxAbsOffset = std::max(maxAbsOffset, std::abs(v[1]));
            }
    }
    int pad = (int)maxAbsOffset + searchRadius + ALIGN_TILE + 1;

    // Add computed padding to source on all sides to ensure that every search region is within bounds
    cv::Mat srcPad;
    cv::copyMakeBorder(src, srcPad, pad, pad, pad, pad, cv::BORDER_REPLICATE);

    // Add `ALIGN_TILE` padding to reference at bottom/right for border tiles
    cv::Mat refPadded;
    cv::copyMakeBorder(ref, refPadded, 0, ALIGN_TILE, 0, ALIGN_TILE, cv::BORDER_REPLICATE);

    cv::Mat result(nTilesY, nTilesX, CV_32FC2);

    // Go over image tile by tile and find offset for each tile
    for (int ty = 0; ty < nTilesY; ty++)
    {
        for (int tx = 0; tx < nTilesX; tx++)
        {
            int y0 = ty * ALIGN_TILE;
            int x0 = tx * ALIGN_TILE;
            cv::Mat refTile = refPadded(cv::Range(y0, y0 + ALIGN_TILE), cv::Range(x0, x0 + ALIGN_TILE)).clone();

            int initOffsetY = 0;
            int initOffsetX = 0;
            if (!initOffsets.empty())
            {
                cv::Vec2f initOffset = initOffsets.at<cv::Vec2f>(ty, tx);
                initOffsetY = (int)std::round(initOffset[0]);
                initOffsetX = (int)std::round(initOffset[1]);
            }

            float bestOffsetY;
            float bestOffsetX;

            int regionY0 = y0 + initOffsetY - searchRadius + pad;
                int regionX0 = x0 + initOffsetX - searchRadius + pad;
                int regionH = 2 * searchRadius + ALIGN_TILE;
                int regionW = 2 * searchRadius + ALIGN_TILE;
                cv::Mat searchRegion = srcPad(cv::Range(regionY0, regionY0 + regionH),
                                              cv::Range(regionX0, regionX0 + regionW)).clone();

                cv::Mat matchResult;
                cv::matchTemplate(searchRegion, refTile, matchResult, cv::TM_SQDIFF);

                cv::Point minLoc;
                cv::minMaxLoc(matchResult, nullptr, nullptr, &minLoc, nullptr);

                float subY = 0.0f;
                float subX = 0.0f;
                if (minLoc.y > 0 && minLoc.y < matchResult.rows - 1 &&
                    minLoc.x > 0 && minLoc.x < matchResult.cols - 1)
                {
                    Eigen::Matrix3f F;
                    for (int dy = -1; dy <= 1; dy++)
                        for (int dx = -1; dx <= 1; dx++)
                            F(dy + 1, dx + 1) = matchResult.at<float>(minLoc.y + dy, minLoc.x + dx);

                    float sumAll    = F.sum();
                    float rowDiff   = F.row(2).sum() - F.row(0).sum();
                    float colDiff   = F.col(2).sum() - F.col(0).sum();
                    float outerRows = F.row(0).sum() + F.row(2).sum();
                    float outerCols = F.col(0).sum() + F.col(2).sum();
                    float cornerDiff = F(0,0) + F(2,2) - F(0,2) - F(2,0);

                    float betaVV = 0.5f * outerRows - (1.0f/3.0f) * sumAll;
                    float betaUU = 0.5f * outerCols - (1.0f/3.0f) * sumAll;
                    float betaUV = 0.25f * cornerDiff;
                    float b_v    = (1.0f/6.0f) * rowDiff;
                    float b_u    = (1.0f/6.0f) * colDiff;

                    Eigen::Matrix2f A;
                    A(0, 0) = 2.0f * betaVV;
                    A(0, 1) = betaUV;
                    A(1, 0) = betaUV;
                    A(1, 1) = 2.0f * betaUU;
                    Eigen::Vector2f b(b_v, b_u);

                    if (A.determinant() > 1e-6f)
                    {
                        Eigen::Vector2f mu = -A.inverse() * b;
                        subY = std::max(-1.0f, std::min(1.0f, mu[0]));
                        subX = std::max(-1.0f, std::min(1.0f, mu[1]));
                    }
                }

            bestOffsetY = (float)(initOffsetY - searchRadius + minLoc.y) + subY;
            bestOffsetX = (float)(initOffsetX - searchRadius + minLoc.x) + subX;

            result.at<cv::Vec2f>(ty, tx) = cv::Vec2f(bestOffsetY, bestOffsetX);
        }
    }

    return result;
}

// Upsample calculated offsets to a finer level (2x size). Compare the offset from tile directly above it and the tile closes in X and Y axis. Pick the best matching offset
cv::Mat TMSHDRMergeHDRPlus::upsampleMultiHypothesis(const cv::Mat& coarseOffsets,
                                                     const cv::Mat& refFine,
                                                     const cv::Mat& srcFine)
{
    int rowsFine = refFine.rows;
    int colsFine = refFine.cols;
    int nTilesY = (int)std::ceil((float)rowsFine / ALIGN_TILE);
    int nTilesX = (int)std::ceil((float)colsFine / ALIGN_TILE);

    float maxAbsOff = 0;
    for (int ty = 0; ty < coarseOffsets.rows; ty++)
        for (int tx = 0; tx < coarseOffsets.cols; tx++)
        {
            cv::Vec2f v = coarseOffsets.at<cv::Vec2f>(ty, tx);
            maxAbsOff = std::max(maxAbsOff, std::abs(v[0]));
            maxAbsOff = std::max(maxAbsOff, std::abs(v[1]));
        }
    int pad = (int)(2.0f * maxAbsOff) + ALIGN_TILE + 1;

    cv::Mat refPadded;
    cv::Mat srcPadded;
    cv::copyMakeBorder(refFine, refPadded, 0, ALIGN_TILE, 0, ALIGN_TILE, cv::BORDER_REPLICATE);
    cv::copyMakeBorder(srcFine, srcPadded, pad, pad, pad, pad, cv::BORDER_REPLICATE);

    cv::Mat result(nTilesY, nTilesX, CV_32FC2);

    for (int ty = 0; ty < nTilesY; ty++)
    {
        for (int tx = 0; tx < nTilesX; tx++)
        {
            int coarseY  = std::min(ty / 2, coarseOffsets.rows - 1);
            int coarseX  = std::min(tx / 2, coarseOffsets.cols - 1);
            int coarseYN = (ty % 2 == 0) ? std::max(coarseY - 1, 0) : std::min(coarseY + 1, coarseOffsets.rows - 1);
            int coarseXN = (tx % 2 == 0) ? std::max(coarseX - 1, 0) : std::min(coarseX + 1, coarseOffsets.cols - 1);

            cv::Vec2f candidates[3] = {
                coarseOffsets.at<cv::Vec2f>(coarseY,  coarseX)  * 2.0f,
                coarseOffsets.at<cv::Vec2f>(coarseYN, coarseX)  * 2.0f,
                coarseOffsets.at<cv::Vec2f>(coarseY,  coarseXN) * 2.0f,
            };

            int tileOriginY = ty * ALIGN_TILE;
            int tileOriginX = tx * ALIGN_TILE;
            cv::Mat referenceTile = refPadded(cv::Range(tileOriginY, tileOriginY + ALIGN_TILE),
                                              cv::Range(tileOriginX, tileOriginX + ALIGN_TILE)).clone();

            float bestMatchScore = std::numeric_limits<float>::max();
            cv::Vec2f bestOffset = candidates[0];

            for (const auto& candidate : candidates)
            {
                int offsetY = (int)std::round(candidate[0]);
                int offsetX = (int)std::round(candidate[1]);
                int sourceY = tileOriginY + offsetY + pad;
                int sourceX = tileOriginX + offsetX + pad;

                if (sourceY < 0 || sourceX < 0 ||
                    sourceY + ALIGN_TILE > srcPadded.rows ||
                    sourceX + ALIGN_TILE > srcPadded.cols)
                    continue;

                cv::Mat sourceTile = srcPadded(cv::Range(sourceY, sourceY + ALIGN_TILE),
                                               cv::Range(sourceX, sourceX + ALIGN_TILE));
                cv::Mat absDiff;
                cv::absdiff(referenceTile, sourceTile, absDiff);
                float matchScore = (float)cv::sum(absDiff)[0];

                if (matchScore < bestMatchScore)
                {
                    bestMatchScore = matchScore;
                    bestOffset = candidate;
                }
            }

            result.at<cv::Vec2f>(ty, tx) = bestOffset;
        }
    }

    return result;
}

cv::Mat TMSHDRMergeHDRPlus::mergeFrames(const std::vector<cv::Mat>& images,
                                        const std::vector<cv::Mat>& offsets,
                                        const std::vector<float>&   times,
                                        int                         refIdx)
{
    int imagesCount = (int)images.size();
    int rows_orig = images[refIdx].rows;
    int cols_orig = images[refIdx].cols;

    // Make sure rows and cols are divisible by MERGE_TILE and round up if necessary
    int rowsRemainder = rows_orig % MERGE_TILE;
    int colsRemainder = cols_orig % MERGE_TILE;
    int rows = rows_orig + (rowsRemainder ? MERGE_TILE - rowsRemainder : 0);
    int cols = cols_orig + (colsRemainder ? MERGE_TILE - colsRemainder : 0);

    std::cout << "HDR+ - merging " << imagesCount << " images at " << cols_orig << "x" << rows_orig << std::endl;

    // Normalise frames by exposure time
    std::vector<cv::Mat> normImgs(imagesCount);
    for (int i = 0; i < imagesCount; i++)
    {
        cv::Mat norm;
        images[i].convertTo(norm, CV_32FC3, 1.0 / (double)times[i]);
        cv::copyMakeBorder(norm, normImgs[i], 0, rows - rows_orig, 0, cols - cols_orig, cv::BORDER_REPLICATE);
    }

    // Compute max absolute offset for source image padding
    int maxOffset = 0;
    for (int i = 0; i < imagesCount; i++)
    {
        if (i == refIdx) continue;
        for (int ty = 0; ty < offsets[i].rows; ty++)
            for (int tx = 0; tx < offsets[i].cols; tx++)
            {
                cv::Vec2f offset = offsets[i].at<cv::Vec2f>(ty, tx);
                maxOffset = std::max(maxOffset, (int)std::ceil(std::abs(offset[0])));
                maxOffset = std::max(maxOffset, (int)std::ceil(std::abs(offset[1])));
            }
    }

    int halfMergeTile = MERGE_TILE / 2;
    int srcPadSize = maxOffset + halfMergeTile;

    // Pad reference image (bottom and right only)
    cv::Mat refPad;
    cv::copyMakeBorder(normImgs[refIdx], refPad, 0, halfMergeTile, 0, halfMergeTile, cv::BORDER_REPLICATE);

    // Pad source images on all sides
    std::vector<cv::Mat> srcPad(imagesCount);
    for (int i = 0; i < imagesCount; i++)
    {
        if (i == refIdx) continue;
        cv::copyMakeBorder(normImgs[i], srcPad[i], srcPadSize, srcPadSize, srcPadSize, srcPadSize, cv::BORDER_REPLICATE);
    }

    cv::Mat mergeWindow(MERGE_TILE, MERGE_TILE, CV_32F);
    for (int y = 0; y < MERGE_TILE; y++)
    {
        float wy = 0.5f - 0.5f * (float)std::cos(2.0 * CV_PI * (y + 0.5) / MERGE_TILE);
        for (int x = 0; x < MERGE_TILE; x++)
        {
            float wx = 0.5f - 0.5f * (float)std::cos(2.0 * CV_PI * (x + 0.5) / MERGE_TILE);
            mergeWindow.at<float>(y, x) = wy * wx;
        }
    }

    // Noise shaping for spatial denoising
    cv::Mat noiseShapeSq(MERGE_TILE, MERGE_TILE, CV_32F);
    {
        float halfN = (float)MERGE_TILE / 2.0f;
        for (int ky = 0; ky < MERGE_TILE; ky++)
        {
            float fy = (float)std::min(ky, MERGE_TILE - ky);
            for (int kx = 0; kx < MERGE_TILE; kx++)
            {
                float fx = (float)std::min(kx, MERGE_TILE - kx);
                float f = 1.0f + std::sqrt(fy * fy + fx * fx) / halfN;
                noiseShapeSq.at<float>(ky, kx) = f * f;
            }
        }
    }

    cv::Mat result = cv::Mat::zeros(rows, cols, CV_32FC3);
    cv::Mat weightSum = cv::Mat::zeros(rows, cols, CV_32F);

    int nAlignTilesY = (int)std::ceil((float)rows_orig / ALIGN_TILE);
    int nAlignTilesX = (int)std::ceil((float)cols_orig / ALIGN_TILE);

    for (int ty = 0; ty < rows; ty += halfMergeTile)
    {
        for (int tx = 0; tx < cols; tx += halfMergeTile)
        {
            // Nearest alignment tile (with offsets) for current merge tile
            int alignTileY = std::min(ty / ALIGN_TILE, nAlignTilesY - 1);
            int alignTileX = std::min(tx / ALIGN_TILE, nAlignTilesX - 1);

            cv::Mat refTile3 = refPad(cv::Range(ty, ty + MERGE_TILE), cv::Range(tx, tx + MERGE_TILE));
            std::vector<cv::Mat> refChs(3);
            cv::split(refTile3, refChs);

            std::vector<cv::Mat> mergedChs(3);

            // Wiener merge in frequency domain for each channel
            for (int c = 0; c < 3; c++)
            {
                cv::Mat refDft;
                cv::dft(refChs[c], refDft, cv::DFT_COMPLEX_OUTPUT);

                float rms = tileRMS(refChs[c]);
                float sigmaSq = lambda_s * rms + lambda_r * lambda_r;
                float sigmaSqDft = sigmaSq * (float)(MERGE_TILE * MERGE_TILE);
                float cSigSq = c_temporal * sigmaSqDft;

                // Temporal Wiener merge
                cv::Mat accum = refDft.clone();

                for (int i = 0; i < imagesCount; i++)
                {
                    if (i == refIdx) continue;

                    cv::Vec2f off = offsets[i].at<cv::Vec2f>(alignTileY, alignTileX);
                    int offsetY = (int)std::round(off[0]);
                    int offsetX = (int)std::round(off[1]);

                    // Extract source tile at offset from padded image
                    int srcY = ty + offsetY + srcPadSize;
                    int srcX = tx + offsetX + srcPadSize;
                    cv::Mat srcTile3 = srcPad[i](cv::Range(srcY, srcY + MERGE_TILE), cv::Range(srcX, srcX + MERGE_TILE));
                    std::vector<cv::Mat> srcChs(3);
                    cv::split(srcTile3, srcChs);

                    cv::Mat srcDft;
                    cv::dft(srcChs[c], srcDft, cv::DFT_COMPLEX_OUTPUT);

                    cv::Mat D = refDft - srcDft;
                    cv::Mat Dsq = complexAbsSq(D);

                    cv::Mat Az;
                    cv::divide(Dsq, Dsq + cSigSq, Az);

                    accum += srcDft + complexMulReal(D, Az);
                }

                cv::Mat temporalMerged;
                cv::idft(accum, temporalMerged, cv::DFT_REAL_OUTPUT | cv::DFT_SCALE);
                temporalMerged *= 1.0f / imagesCount;

                cv::Mat spatialDft;
                cv::dft(temporalMerged, spatialDft, cv::DFT_COMPLEX_OUTPUT);

                cv::Mat spatialPow = complexAbsSq(spatialDft);
                float cSigSqSp  = c_temporal * sigmaSqDft / (float)imagesCount;
                cv::Mat cSigSqSpMat = noiseShapeSq * cSigSqSp;

                cv::Mat spatialB;
                cv::divide(spatialPow, spatialPow + cSigSqSpMat, spatialB);

                cv::Mat denoised;
                cv::idft(complexMulReal(spatialDft, spatialB), denoised, cv::DFT_REAL_OUTPUT | cv::DFT_SCALE);
                mergedChs[c] = denoised;
            }

            cv::Mat merged3;
            cv::merge(mergedChs, merged3);

            int h = std::min(ty + MERGE_TILE, rows) - ty;
            int w = std::min(tx + MERGE_TILE, cols) - tx;

            cv::Mat windowRoi = mergeWindow(cv::Range(0, h), cv::Range(0, w));
            cv::Mat window3;
            cv::merge(std::vector<cv::Mat>{windowRoi, windowRoi, windowRoi}, window3);

            result(cv::Range(ty, ty + h), cv::Range(tx, tx + w)) += merged3(cv::Range(0, h), cv::Range(0, w)).mul(window3);
            weightSum(cv::Range(ty, ty + h), cv::Range(tx, tx + w)) += windowRoi;
        }
    }

    // Normalise by accumulated window weights
    cv::Mat wtSum3;
    cv::merge(std::vector<cv::Mat>{weightSum + 1e-10f, weightSum + 1e-10f, weightSum + 1e-10f}, wtSum3);
    cv::Mat finalResult = result / wtSum3;
    // Clamp noise below zero
    cv::max(finalResult, 0.0f, finalResult);

    // Remove padding and return final result
    return finalResult(cv::Range(0, rows_orig), cv::Range(0, cols_orig)).clone();
}
