#ifndef _IOCP_H
#define _IOCP_H
#include "connect.h"
#include <WinSock2.h>
#pragma comment(lib, "Ws2_32.lib")

#ifdef _WIN32

#define OP_READ 1
#define OP_WRITE 2
void* listen_thread(void *param);
void* service_thread(void *param);
int xsend();
int push_complate_packet(conn_rec *c);
int length_to_read(conn_rec *c);
int packet_recv(conn_rec *c);
int packet_send(conn_rec *c);
int s_connect(conn_rec *c);
int close_connect(conn_rec *c);
int is_heart_beat(conn_rec *c);
void wakeup();
#endif

#endif