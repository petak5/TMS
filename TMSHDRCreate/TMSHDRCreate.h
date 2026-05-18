// TMSHDRCreate.h: interface for the TMSHDRCreate class.
//
//////////////////////////////////////////////////////////////////////
#pragma once
#include "TMSHDRProgress.h"
#include <string>
#include <vector>

struct HDRCreateParams {
    enum Method { DEBEVEC, HDRPLUS, DIS, SAFNET } method = DEBEVEC;
    std::vector<std::string> imagePaths;
    std::vector<float> exposureTimes;
    std::string outputPath = "output.tiff";
    float hdrplusC = 2.0f;
    float disNoise = 0.05f;
    std::string safnetModel = "SAFNet.onnx";
    int safnetTile = 2048;
    int safnetOverlap = 128;
};

class TMSHDRCreate
{
public:
    virtual int main(int argc, char *argv[]);
    // Returns empty string on success, error message on failure
    std::string run(const HDRCreateParams& params,
                    ProgressFn progress = nullptr);
    TMSHDRCreate();
    virtual ~TMSHDRCreate();

protected:
    void Help();
};
