// TMSAlignOpticalFlowMethod.h: interface for the TMSAlignOpticalFlowMethod class.
//
//////////////////////////////////////////////////////////////////////

#include <opencv2/opencv.hpp>

class TMSAlignOpticalFlowMethod
{
public:
    virtual std::vector<cv::Mat> align(std::vector<cv::Mat> images, int resizeRatio, std::string debugOutputs, bool isDebug);
    TMSAlignOpticalFlowMethod();
    virtual ~TMSAlignOpticalFlowMethod();

protected:
    cv::Mat align_image(const cv::Mat &image, const cv::Mat &homography);
    std::pair<cv::Mat, cv::Mat> align_and_resize_images(const cv::Mat &image_s, const cv::Mat &image_t, const cv::Mat &homography);
    cv::Mat crop_image(const cv::Mat &image, const cv::Mat &homography);
};
