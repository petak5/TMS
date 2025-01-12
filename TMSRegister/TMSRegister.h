// TMSRegister.h: interface for the TMSRegister class.
//
//////////////////////////////////////////////////////////////////////
#include "../tmolib/TMO.h"
class TMSRegister
{
public:
	virtual int main(int argc, char *argv[]);
	TMSRegister();
	virtual ~TMSRegister();

protected:
	void Help();
};
