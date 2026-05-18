#include "TMOGUIHDRCreateWorker.h"
#include "../TMSHDRCreate/TMSHDRCreate.h"
#include "../TMSHDRRegister/TMSHDRRegister.h"
#include <QTemporaryDir>
#include <QFileInfo>
#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

TMOGUIHDRCreateWorker::TMOGUIHDRCreateWorker(const TMOGUIHDRCreateParams& params, QObject* parent)
    : QThread(parent), m_params(params)
{
}

static void applyManualTransform(const AlignTransform& t,
                                 const QString& srcPath,
                                 const QString& dstPath)
{
    cv::Mat img = cv::imread(srcPath.toStdString(), cv::IMREAD_UNCHANGED);
    if (img.empty()) return;
    cv::Mat warped;
    if (t.useHomography)
    {
        cv::Mat H(3, 3, CV_64F);
        for (int i = 0; i < 3; i++)
            for (int j = 0; j < 3; j++)
                H.at<double>(i, j) = t.homography[i * 3 + j];
        cv::warpPerspective(img, warped, H, img.size(), cv::INTER_LINEAR, cv::BORDER_REPLICATE);
    }
    else
    {
        cv::Point2f center(img.cols / 2.0f, img.rows / 2.0f);
        cv::Mat M = cv::getRotationMatrix2D(center, -(double)t.angle, (double)t.scale);
        M.at<double>(0, 2) += t.tx;
        M.at<double>(1, 2) += t.ty;
        cv::warpAffine(img, warped, M, img.size(), cv::INTER_LINEAR, cv::BORDER_REPLICATE);
    }
    cv::imwrite(dstPath.toStdString(), warped);
}

void TMOGUIHDRCreateWorker::run()
{
    QStringList imagePaths = m_params.imagePaths;

    // Optional manual alignment step
    QTemporaryDir manualTempDir;
    {
        bool anyNonIdentity = false;
        for (const auto& t : m_params.manualTransforms)
            if (!t.isIdentity()) { anyNonIdentity = true; break; }

        if (anyNonIdentity)
        {
            emit progress(0, "Applying manual transforms");
            if (!manualTempDir.isValid())
            {
                emit failed("Could not create temporary directory for manual transforms");
                return;
            }
            QStringList aligned;
            for (int i = 0; i < imagePaths.size(); i++)
            {
                const AlignTransform& t = (i < m_params.manualTransforms.size())
                                          ? m_params.manualTransforms[i]
                                          : AlignTransform{};
                if (t.isIdentity())
                {
                    aligned.append(imagePaths[i]);
                }
                else
                {
                    QString dst = manualTempDir.path() + "/" + QFileInfo(imagePaths[i]).fileName();
                    applyManualTransform(t, imagePaths[i], dst);
                    aligned.append(dst);
                }
            }
            imagePaths = aligned;
        }
    }

    // registrationTempDir must outlive imagePaths references into it
    QTemporaryDir registrationTempDir;

    // Optional registration step
    if (m_params.doRegistration)
    {
        emit progress(0, "Registering images");

        RegistrationMethod method;
        switch (m_params.registrationMethod)
        {
        case TMOGUIHDRCreateParams::REG_HOMOGRAPHY:  method = RegistrationMethod::HOMOGRAPHY;   break;
        case TMOGUIHDRCreateParams::REG_OPTICAL_FLOW: method = RegistrationMethod::OPTICAL_FLOW; break;
        default:                                      method = RegistrationMethod::MTB;          break;
        }

        if (!registrationTempDir.isValid())
        {
            emit failed("Could not create temporary directory for registration");
            return;
        }

        std::vector<std::string> inputPaths;
        for (const QString& p : imagePaths)
            inputPaths.push_back(p.toStdString());

        std::vector<std::string> alignedPaths;
        TMSHDRRegister reg;
        std::string err = reg.alignToFolder(inputPaths, method,
                                            registrationTempDir.path().toStdString(),
                                            alignedPaths,
                                            [this](int pct, const std::string& stage) {
            emit progress(pct / 10, QString::fromStdString(stage));
        });
        if (!err.empty())
        {
            emit failed(QString::fromStdString(err));
            return;
        }

        imagePaths.clear();
        for (const std::string& p : alignedPaths)
            imagePaths.append(QString::fromStdString(p));

        emit progress(10, "Registration complete");
    }

    // HDR creation
    int progressBase = m_params.doRegistration ? 10 : 0;
    int progressRange = 100 - progressBase;

    HDRCreateParams params;
    switch (m_params.method)
    {
    case TMOGUIHDRCreateParams::HDRPLUS: params.method = HDRCreateParams::HDRPLUS; break;
    case TMOGUIHDRCreateParams::DIS:     params.method = HDRCreateParams::DIS;     break;
    case TMOGUIHDRCreateParams::SAFNET:  params.method = HDRCreateParams::SAFNET;  break;
    default:                             params.method = HDRCreateParams::DEBEVEC; break;
    }

    for (const QString& p : imagePaths)
        params.imagePaths.push_back(p.toStdString());

    for (double t : m_params.exposureTimes)
        params.exposureTimes.push_back((float)t);

    params.outputPath  = m_params.outputPath.toStdString();
    params.hdrplusC    = (float)m_params.hdrplusC;
    params.disNoise    = (float)m_params.disNoise;
    params.safnetModel = m_params.safnetModel.toStdString();
    params.safnetTile  = m_params.safnetTile;
    params.safnetOverlap = m_params.safnetOverlap;

    TMSHDRCreate creator;
    std::string err = creator.run(params, [this, progressBase, progressRange](int pct, const std::string& stage) {
        emit progress(progressBase + pct * progressRange / 100,
                      QString::fromStdString(stage));
    });

    if (!err.empty())
    {
        emit failed(QString::fromStdString(err));
        return;
    }

    emit done(m_params.outputPath);
}
