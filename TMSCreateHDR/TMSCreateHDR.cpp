// TMSCreateHDR.cpp: implementation of the TMSCreateHDR class.
//
//////////////////////////////////////////////////////////////////////
#include "TMSCreateHDR.h"

#ifdef LINUX
#include "../TMOLinux/TMOLinux.h"
#endif
#ifndef LINUX
#include "../TMOW32/TMOW32.h"
#endif

#ifndef LINUX
#define OPENEXR_DLL
#else
#define _stricmp strcasecmp
#endif

#include <stdlib.h>
#include <limits.h>
#include <wchar.h>
#include <iostream>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <string.h>
#include <opencv2/opencv.hpp>

TMSCreateHDR::TMSCreateHDR()
{
}

TMSCreateHDR::~TMSCreateHDR()
{
}

int TMSCreateHDR::main(int argc, char *argv[])
{
	std::string inputFileName1;
	std::string inputFileName2;
	float inputFileTime1;
	float inputFileTime2;
	std::string outputFileName = "output";

	// Parse args
	if (argc == 5)
	{
		inputFileName1 = argv[1];
		inputFileName2 = argv[3];
		inputFileTime1 = std::stof(argv[2]);
		inputFileTime2 = std::stof(argv[4]);
	}
	else if (argc == 7)
	{
		if (strcmp(argv[1], "-o") == 0)
		{
			outputFileName = argv[2];

			inputFileName1 = argv[3];
			inputFileName2 = argv[5];
			inputFileTime1 = std::stof(argv[4]);
			inputFileTime2 = std::stof(argv[6]);
		}
		else if (strcmp(argv[5], "-o") == 0)
		{
			outputFileName = argv[6];

			inputFileName1 = argv[1];
			inputFileName2 = argv[3];
			inputFileTime1 = std::stof(argv[2]);
			inputFileTime2 = std::stof(argv[4]);
		}
		else
		{
			Help();
			return 1;
		}
	}
	else
	{
		Help();
		return 1;
	}

	std::vector<cv::Mat> images;
	std::vector<float> times;

	int numImages = 2;

	// List of exposure times
	static const float timesArray[] = {inputFileTime1, inputFileTime2};
	times.assign(timesArray, timesArray + numImages);

	// List of image filenames
	static const char *filenames[] = {inputFileName1.c_str(), inputFileName2.c_str()};
	for (int i = 0; i < numImages; i++)
	{
		std::cout << "Reading image: " << filenames[i] << std::endl;
		cv::Mat im = cv::imread(filenames[i]);
		images.push_back(im);
	}

	cv::Mat responseDebevec;
	cv::Ptr<cv::CalibrateDebevec> calibrateDebevec = cv::createCalibrateDebevec();
	calibrateDebevec->process(images, responseDebevec, times);

	cv::Mat hdrDebevec;
	cv::Ptr<cv::MergeDebevec> mergeDebevec = cv::createMergeDebevec();
	mergeDebevec->process(images, hdrDebevec, times, responseDebevec);

	std::string savePath = outputFileName + ".tiff";
	std::cout << "Saving HDR image to: " << savePath << std::endl;
	cv::imwrite(savePath, hdrDebevec);

	return 0;
}

void TMSCreateHDR::Help()
{
	wprintf(L"Usage : \n");
	wprintf(L"    ./TMSCreateHDR [-o output] image1 exposureTime1 image2 exposureTime2\n\n");
}
