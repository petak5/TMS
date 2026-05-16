// TMSHDRRegister.h: interface for the TMSHDRRegister class.
//
//////////////////////////////////////////////////////////////////////
#include "../tmolib/TMO.h"
class TMSHDRRegister
{
public:
    virtual int main(int argc, char *argv[]);
    TMSHDRRegister();
    virtual ~TMSHDRRegister();

protected:
    void Help();
};
