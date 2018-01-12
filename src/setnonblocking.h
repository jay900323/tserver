#ifndef _SETNONBLOCKING_H_
#define _SETNONBLOCKING_H_

#ifdef _WIN32
int setnonblocking(int fd);
int setblocking(int fd);

#elif __linux__
int setnonblocking(SOCKET s);
int setblocking(SOCKET s);
#endif

#endif

