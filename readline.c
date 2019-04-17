/*****************************************************
* 实现Linux下的终端字符流读写控制主要函数接口
*****************************************************/
#include <termios.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>

#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <time.h>
#include <sys/ioctl.h>
#include <unistd.h>
