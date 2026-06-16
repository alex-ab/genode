#include <glib.h>

#if GLIB_SIZEOF_VOID_P != 4
#error "Please make sure to install the 32bit version of glib2. On Debian/Ubuntu via 'sudo aptitude install libglib2.0-dev:i386'"
#endif
