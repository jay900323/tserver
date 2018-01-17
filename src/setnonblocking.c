#include "setnonblocking.h"

#ifdef _WIN32
#include <WinSock2.h>

int setnonblocking(SOCKET s)
{
	unsigned long ul=1;  
	int ret;  
	s=socket(AF_INET,SOCK_STREAM,0);  
	ret=ioctlsocket(s, FIONBIO, (unsigned long *)&ul);
	if(ret==SOCKET_ERROR)
	{  
		return -1;
	} 
}

int setblocking(SOCKET s){
	unsigned long ul=0;  
	int ret;  
	s=socket(AF_INET,SOCK_STREAM,0);  
	ret=ioctlsocket(s, FIONBIO, (unsigned long *)&ul);
	if(ret==SOCKET_ERROR)
	{  
		return -1;
	} 
	return 0;
}

#elif __linux__
#include <fcntl.h>
int setnonblocking(int fd){
	int old_option = fcntl(fd, F_GETFL);
	int new_option = old_option | O_NONBLOCK;
	fcntl(fd, F_SETFL, new_option);
	return new_option;
}

int setblocking(int fd){
	int old_option = fcntl(fd, F_GETFL);
	int new_option = old_option & O_NONBLOCK;
	fcntl(fd, F_SETFL, new_option);
	return new_option;
}

#endif