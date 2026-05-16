// TMSHDRAlignMTB.h: interface for the TMSHDRAlignMTB class.
//
//////////////////////////////////////////////////////////////////////

#include <opencv2/opencv.hpp>

class TMSHDRAlignMTB
{
public:
    virtual std::vector<cv::Mat> align(std::vector<cv::Mat> images, std::string debugOutput, bool isDebug);
    TMSHDRAlignMTB();
    virtual ~TMSHDRAlignMTB();
};
