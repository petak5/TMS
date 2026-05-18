#pragma once
#include <QThread>
#include <QString>
#include <QStringList>
#include <QList>
#include <QVector>
#include <array>
#include <cmath>

struct AlignTransform {
    // Simple transformation (when useHomography == false)
    float tx = 0;
    float ty = 0;
    float angle = 0;
    float scale = 1.0f;

    // Homography
    bool useHomography = false;
    std::array<double, 9> homography = {1,0,0, 0,1,0, 0,0,1};

    static bool nearZero(double v, double eps = 1e-6) { return std::abs(v) < eps; }

    // Skip applying transformation when homography or simple transformation is zero or close to it
    bool isIdentity() const {
        if (useHomography) {
            const auto& h = homography;
            return nearZero(h[0]-1) && nearZero(h[1]) && nearZero(h[2])
                && nearZero(h[3]) && nearZero(h[4]-1) && nearZero(h[5])
                && nearZero(h[6]) && nearZero(h[7]) && nearZero(h[8]-1);
        }
        return nearZero(tx) && nearZero(ty) && nearZero(angle) && nearZero(scale - 1.0f);
    }
};

struct TMOGUIHDRCreateParams {
    enum Method { DEBEVEC, HDRPLUS, DIS, SAFNET } method = DEBEVEC;
    QStringList imagePaths;
    QList<double> exposureTimes;
    QString outputPath = "output.tiff";
    double hdrplusC = 2.0;
    double disNoise = 0.05;
    QString safnetModel = "SAFNet.onnx";
    int safnetTile = 2048;
    int safnetOverlap = 128;
    bool doRegistration = false;
    enum RegMethod { REG_HOMOGRAPHY, REG_MTB, REG_OPTICAL_FLOW } registrationMethod = REG_MTB;
    QVector<AlignTransform> manualTransforms;
};

class TMOGUIHDRCreateWorker : public QThread
{
    Q_OBJECT
public:
    explicit TMOGUIHDRCreateWorker(const TMOGUIHDRCreateParams& params, QObject* parent = nullptr);

signals:
    void progress(int percent, QString stage);
    void done(QString outputPath);
    void failed(QString errorMessage);

protected:
    void run() override;

private:
    TMOGUIHDRCreateParams m_params;
};
