// TMSHDRAlignOpticalFlow.h: interface for the TMSHDRAlignOpticalFlow class.
//
//////////////////////////////////////////////////////////////////////

#include <opencv2/opencv.hpp>

class TMSHDRAlignOpticalFlow
{
public:
    virtual std::vector<cv::Mat> align(std::vector<cv::Mat> images, int resizeRatio);
    TMSHDRAlignOpticalFlow();
    virtual ~TMSHDRAlignOpticalFlow();


};
