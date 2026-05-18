#include "TMSHDRMergeSAFNet.h"
#include <iostream>
#include <stdexcept>

static constexpr int ALIGN = 32;
static constexpr int TILE = 2048;
static constexpr int OVERLAP = 128;

TMSHDRMergeSAFNet::TMSHDRMergeSAFNet(const std::string& modelPath, int tileSize, int overlap)
    : env(ORT_LOGGING_LEVEL_WARNING, "SAFNet"),
      session(env, modelPath.c_str(), Ort::SessionOptions{}),
      tileSize(tileSize), overlap(overlap)
{
    std::cout << "SAFNet - loaded model from: " << modelPath << std::endl;
}

std::vector<float> TMSHDRMergeSAFNet::buildTensor(const cv::Mat& ldr, float exposureRatio) const
{
    int H = ldr.rows, W = ldr.cols;

    cv::Mat lin;
    cv::pow(ldr, 2.2, lin);
    lin /= exposureRatio;

    std::vector<cv::Mat> ldrChs(3), linChs(3);
    cv::split(ldr, ldrChs);
    cv::split(lin, linChs);

    std::vector<float> data(6 * H * W);
    int planeSize = H * W;
    for (int c = 0; c < 3; c++)
    {
        std::memcpy(data.data() + c * planeSize, linChs[2 - c].ptr<float>(), planeSize * sizeof(float));
        std::memcpy(data.data() + (c + 3) * planeSize, ldrChs[2 - c].ptr<float>(), planeSize * sizeof(float));
    }
    return data;
}

cv::Mat TMSHDRMergeSAFNet::runTile(const std::vector<cv::Mat>& tiles, const std::vector<float>& expos)
{
    int tH = tiles[0].rows, tW = tiles[0].cols;

    // Pad tile to multiple of ALIGN
    int pH = ((tH + ALIGN - 1) / ALIGN) * ALIGN;
    int pW = ((tW + ALIGN - 1) / ALIGN) * ALIGN;

    std::vector<cv::Mat> padded(3);
    for (int i = 0; i < 3; i++)
        cv::copyMakeBorder(tiles[i], padded[i], 0, pH - tH, 0, pW - tW, cv::BORDER_REPLICATE);

    std::vector<float> t0 = buildTensor(padded[0], expos[0]);
    std::vector<float> t1 = buildTensor(padded[1], expos[1]);
    std::vector<float> t2 = buildTensor(padded[2], expos[2]);

    std::array<int64_t, 4> shape = {1, 6, pH, pW};
    auto memInfo = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);

    std::array<Ort::Value, 3> inputs = {
        Ort::Value::CreateTensor<float>(memInfo, t0.data(), t0.size(), shape.data(), shape.size()),
        Ort::Value::CreateTensor<float>(memInfo, t1.data(), t1.size(), shape.data(), shape.size()),
        Ort::Value::CreateTensor<float>(memInfo, t2.data(), t2.size(), shape.data(), shape.size()),
    };

    const char* inputNames[]  = {"img0", "img1", "img2"};
    const char* outputNames[] = {"hdr_medium", "hdr_refined"};

    auto outputs = session.Run(Ort::RunOptions{}, inputNames, inputs.data(), 3, outputNames, 2);

    float* outPtr = outputs[1].GetTensorMutableData<float>();
    int planeSize = pH * pW;

    std::vector<cv::Mat> chs(3);
    for (int c = 0; c < 3; c++)
        chs[c] = cv::Mat(pH, pW, CV_32F, outPtr + (2 - c) * planeSize).clone();

    cv::Mat out;
    cv::merge(chs, out);
    return out(cv::Range(0, tH), cv::Range(0, tW)).clone();
}

cv::Mat TMSHDRMergeSAFNet::process(const std::vector<cv::Mat>& images, const std::vector<float>& times,
                                    ProgressFn progress)
{
    if (images.size() != 3 || times.size() != 3)
        throw std::runtime_error("SAFNet requires exactly 3 images and 3 exposure times");

    int H = images[0].rows, W = images[0].cols;

    float minTime = *std::min_element(times.begin(), times.end());
    std::vector<float> expos(times.size());
    for (size_t i = 0; i < times.size(); ++i)
        expos[i] = times[i] / minTime;

    // If the image fits in a single tile (plus alignment padding), skip tiling
    if (H <= tileSize && W <= tileSize)
    {
        std::cout << "SAFNet - merging " << W << "x" << H << std::endl;
        if (progress) progress(0, "SAFNet - running inference");
        cv::Mat result = runTile(images, expos);
        if (progress) progress(95, "SAFNet - done");
        return result;
    }

    std::cout << "SAFNet - merging " << W << "x" << H << " in tiles of " << tileSize << "px with " << overlap << "px overlap" << std::endl;

    cv::Mat result = cv::Mat::zeros(H, W, CV_32FC3);
    cv::Mat wtSum  = cv::Mat::zeros(H, W, CV_32F);

    int step = tileSize - overlap;
    int nTilesY = (H <= tileSize) ? 1 : ((H - tileSize + step - 1) / step + 1);
    int nTilesX = (W <= tileSize) ? 1 : ((W - tileSize + step - 1) / step + 1);
    int totalTiles = nTilesY * nTilesX;
    int doneCount = 0;

    // Linearly taper tile edges
    auto make1DWeight = [&](int size, bool taperStart, bool taperEnd) {
        cv::Mat w(size, 1, CV_32F, cv::Scalar(1.0f));
        for (int i = 0; i < overlap && i < size; i++) {
            float v = float(i) / float(overlap);
            if (taperStart) w.at<float>(i) = std::min(w.at<float>(i), v);
            if (taperEnd) w.at<float>(size - 1 - i) = std::min(w.at<float>(size - 1 - i), v);
        }
        return w;
    };

    for (int ty = 0; ty < nTilesY; ty++)
    {
        for (int tx = 0; tx < nTilesX; tx++)
        {
            int y0 = ty * step, x0 = tx * step;
            int y1 = std::min(y0 + tileSize, H);
            int x1 = std::min(x0 + tileSize, W);
            int tH = y1 - y0, tW = x1 - x0;

            if (progress) progress(doneCount * 90 / totalTiles,
                "SAFNet - tile " + std::to_string(doneCount + 1) + "/" + std::to_string(totalTiles));

            std::vector<cv::Mat> tiles(3);
            for (int i = 0; i < 3; i++)
                tiles[i] = images[i](cv::Range(y0, y1), cv::Range(x0, x1));

            cv::Mat tileOut = runTile(tiles, expos);
            doneCount++;

            std::cout << " Tile (" << ty + 1 << "/" << nTilesY << ", " << tx + 1 << "/" << nTilesX << ") done" << std::endl;

            cv::Mat wy   = make1DWeight(tH, y0 > 0, y1 < H);
            cv::Mat wx   = make1DWeight(tW, x0 > 0, x1 < W).reshape(1, 1);
            cv::Mat wt2D = wy * wx;

            cv::Mat wt3D;
            cv::merge(std::vector<cv::Mat>{wt2D, wt2D, wt2D}, wt3D);

            result(cv::Range(y0, y1), cv::Range(x0, x1)) += tileOut.mul(wt3D);
            wtSum(cv::Range(y0, y1), cv::Range(x0, x1))  += wt2D;
        }
    }

    if (progress) progress(95, "SAFNet - compositing tiles");

    // Normalise by accumulated per-pixel weights
    cv::Mat wtSum3;
    cv::merge(std::vector<cv::Mat>{wtSum, wtSum, wtSum}, wtSum3);
    return result / wtSum3;
}
