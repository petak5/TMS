// TMSCreateHDR.h: interface for the TMSCreateHDR class.
//
//////////////////////////////////////////////////////////////////////
#include "../tmolib/TMO.h"
class TMSCreateHDR
{
public:
	virtual int main(int argc, char *argv[]);
	TMSCreateHDR();
	virtual ~TMSCreateHDR();

protected:
	void Help();
};
