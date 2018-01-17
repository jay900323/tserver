#ifndef _SETNONBLOCKING_H_
#define _SETNONBLOCKING_H_

#ifdef _WIN32
#include <WinSock2.h>
#endif

#ifdef _WIN32
int setnonblocking(SOCKET s);
int setblocking(SOCKET s);

#elif __linux__
int setnonblocking(int fd);
int setblocking(int fd);
#endif

#endif

