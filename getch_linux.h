#include <termios.h>
#include <unistd.h>	// for STDIN_FILENO and usleep()
#include <sys/time.h>	// for struct timeval in _kbhit()

static void changemode(bool);
static int _kbhit(void);
static int _getch(void);

static struct termios oldterm;
static bool termmode;

static void changemode(bool dir)
{
	static struct termios newterm;
	
	if (termmode == dir)
		return;
	
	if (dir)
	{
		newterm = oldterm;
		newterm.c_lflag &= ~(ICANON | ECHO);
		tcsetattr(STDIN_FILENO, TCSANOW, &newterm);
	}
	else
	{
		tcsetattr(STDIN_FILENO, TCSANOW, &oldterm);
	}
	termmode = dir;
	
	return;
}

static int _kbhit(void)
{
	struct timeval tv;
	fd_set rdfs;
	int kbret;
	bool needchg;
	
	needchg = (! termmode);
	if (needchg)
		changemode(true);
	tv.tv_sec = 0;
	tv.tv_usec = 0;
	
	FD_ZERO(&rdfs);
	FD_SET(STDIN_FILENO, &rdfs);
	
	select(STDIN_FILENO + 1, &rdfs, NULL, NULL, &tv);
	kbret = FD_ISSET(STDIN_FILENO, &rdfs);
	if (needchg)
		changemode(false);
	
	return kbret;
}

static int _getch(void)
{
	int ch;
	bool needchg;
	
	needchg = (! termmode);
	if (needchg)
		changemode(true);
	ch = getchar();
	if (needchg)
		changemode(false);
	
	return ch;
}
