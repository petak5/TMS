// TMSRegister.cpp: implementation of the TMSRegister class.
//
//////////////////////////////////////////////////////////////////////
#include "TMSRegister.h"

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

#include "TMSAlignHomographyMethod.h"
#include "TMSAlignOpticalFlowMethod.h"
#include "TMSAlignMTBMethod.h"

#define DEBUG true

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

TMSRegister::TMSRegister()
{
}

TMSRegister::~TMSRegister()
{
}

const int RESIZE_RATIO = 2;

int TMSRegister::main(int argc, char *argv[])
{
	std::string inputFileName1;
	std::string inputFileName2;
	std::string outputFolder = "./aligned/";

	// Parse args
	if (argc == 3)
	{
		inputFileName1 = argv[1];
		inputFileName2 = argv[2];
	}
	else if (argc == 5)
	{
		if (strcmp(argv[1], "-o") == 0)
		{
			outputFolder = argv[2];

			inputFileName1 = argv[3];
			inputFileName2 = argv[4];
		}
		else if (strcmp(argv[3], "-o") == 0)
		{
			outputFolder = argv[4];

			inputFileName1 = argv[1];
			inputFileName2 = argv[2];
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

	std::string debugOutput = outputFolder + "/debug/";
	std::vector<cv::Mat> images;

	int numImages = 2;

	// List of image filenames
	static const char *filenames[] = {inputFileName1.c_str(), inputFileName2.c_str()};
	for (int i = 0; i < numImages; i++)
	{
		std::cout << "Reading image: " << filenames[i] << std::endl;
		cv::Mat im = cv::imread(filenames[i]);
		images.push_back(im);
	}

	if (images[0].empty() || images[1].empty())
	{
		std::cout << "Error loading images." << std::endl;
		return 1;
	}

	std::cout << "Done loading images" << std::endl;

	// Feature points with homography and Optical Flow
	TMSAlignHomographyMethod alignMethod;
	// TMSAlignOpticalFlowMethod alignMethod;
	std::vector<cv::Mat> alignedImages = alignMethod.align(images, RESIZE_RATIO, debugOutput, DEBUG);

	// MTB
	// TMSAlignMTBMethod alignMethod;
	// std::vector<cv::Mat> alignedImages = alignMethod.align(images, debugOutput, DEBUG);

	// Save cropped images
	cv::imwrite(outputFolder + "/cropped_s.jpg", alignedImages[0]);
	cv::imwrite(outputFolder + "/cropped_t.jpg", alignedImages[1]);

	std::cout << "Done all" << std::endl;

	return 0;
}

void TMSRegister::Help()
{
	wprintf(L"Usage : \n");
	wprintf(L"    ./TMSRegister [-o outputFolder] image1 image2\n\n");
}
