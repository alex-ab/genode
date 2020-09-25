#include <base/log.h>

extern "C" void adainit(void);

void library_init()
{
	Genode::log("Use libsparkcrypto");
	adainit();
}
