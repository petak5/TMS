// TMSHDRRegister.h: interface for the TMSHDRRegister class.
//
//////////////////////////////////////////////////////////////////////
#pragma once
#include "../TMSHDRCreate/TMSHDRProgress.h"
#include <string>
#include <vector>

enum class RegistrationMethod { HOMOGRAPHY, MTB, OPTICAL_FLOW };

class TMSHDRRegister
{
public:
    virtual int main(int argc, char *argv[]);
    // Align images and save results to outputFolder
    // Returns empty string on success or error message on failure
    std::string alignToFolder(const std::vector<std::string>& inputPaths,
                              RegistrationMethod method,
                              const std::string& outputFolder,
                              std::vector<std::string>& alignedPaths,
                              ProgressFn progress = nullptr);
    TMSHDRRegister();
    virtual ~TMSHDRRegister();

protected:
    void Help();
};
