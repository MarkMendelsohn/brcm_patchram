
#ifdef ANDROID
#include <termios.h>
#else
#include <sys/termios.h>
#include <sys/ioctl.h>
#include <limits.h>
#endif

#define _B(n) { n, B ## n }
int
validate_baudrate(int requested_rate)
{
	struct rates {
		int rate;
		int termios_value;
	} baud_rate[] = {
		_B(115200), 
		_B(230400),
		_B(460800),
		_B(500000),
		_B(576000),
		_B(921600),
		_B(1000000),
		_B(1152000),
		_B(1500000),
		_B(2000000),
		_B(2500000),
		_B(3000000),
#ifndef __CYGWIN__
		_B(3500000),
		_B(4000000),
#endif
	};

	for (unsigned i = 0; i < (sizeof (baud_rate)/sizeof (struct rates)); i++)
		if (baud_rate[i].rate == requested_rate)
			return baud_rate[i].termios_value;

	return -1;
}
