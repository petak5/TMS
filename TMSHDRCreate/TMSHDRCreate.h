// TMSHDRCreate.h: interface for the TMSHDRCreate class.
//
//////////////////////////////////////////////////////////////////////
#include "../tmolib/TMO.h"
#include "TMSHDRMergeDIS.h"
#include "TMSHDRMergeHDRPlus.h"
#include "TMSHDRMergeSAFNet.h"

class TMSHDRCreate
{
public:
    virtual int main(int argc, char *argv[]);
    TMSHDRCreate();
    virtual ~TMSHDRCreate();

protected:
    void Help();
};
