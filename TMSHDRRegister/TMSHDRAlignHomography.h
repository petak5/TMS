// TMSHDRAlignHomography.h: interface for the TMSHDRAlignHomography class.
//
//////////////////////////////////////////////////////////////////////

#include <opencv2/opencv.hpp>

class TMSHDRAlignHomography
{
public:
    virtual std::vector<cv::Mat> align(std::vector<cv::Mat> images, int resizeRatio);
    TMSHDRAlignHomography();
    virtual ~TMSHDRAlignHomography();


};
