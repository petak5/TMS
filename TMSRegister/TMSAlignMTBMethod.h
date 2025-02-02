// TMSAlignMTBMethod.h: interface for the TMSAlignMTBMethod class.
//
//////////////////////////////////////////////////////////////////////

#include <opencv2/opencv.hpp>

class TMSAlignMTBMethod
{
public:
    virtual std::vector<cv::Mat> align(std::vector<cv::Mat> images, std::string debugOutput, bool isDebug);
    TMSAlignMTBMethod();
    virtual ~TMSAlignMTBMethod();
};
