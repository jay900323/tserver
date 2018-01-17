#ifdef _WIN32
#include "iocp.h"
#include "worker.h"
#include "atomic.h"

void* listen_thread(void *param)
{
	SOCKADDR_IN saRemote;
	int nRemoteLen = sizeof(saRemote);
	SOCKET sNew;
	conn_rec *c = NULL;
	const char *remote_ip = NULL;
	int  remote_port = 0;

	zlog_info(z_cate, "监听线程已启动 tid=%lu!", pthread_self());
	while(TRUE)
	{
		if(WaitForSingleObject(exit_event, 0)==WAIT_OBJECT_0)
		{
			PostQueuedCompletionStatus(complateport, 0, (DWORD)NULL, NULL); /* 投递结束完成包 */
			Sleep(0); /*让出CPU权，让业务线程有机会退出	 */
			closesocket(listenfd); /* 可以不要 */
			Sleep(200); 
			break;
		}

		sNew = accept(listenfd, (struct sockaddr *)&saRemote, &nRemoteLen);
		if(sNew==INVALID_SOCKET)
		{
			Sleep(10);
			continue;
		}
		remote_ip = inet_ntoa(saRemote.sin_addr);
		remote_port = ntohs(saRemote.sin_port);
		zlog_info(z_cate, "accept connect ip=%s port=%d", remote_ip, remote_port);

		c = create_conn(sNew, remote_ip, remote_port);
		c->read_callback = push_complate_packet;
		c->close_callback = s_connect;
		add_connect(c);
		CreateIoCompletionPort((HANDLE)(c->fd), complateport, (DWORD)c, 0);
		addref(c);

		if(packet_recv(c) ==RECV_FAILED){
			close_connect(c);
		}
	}
	return 0;
}

void* service_thread(void *param)
{
	HANDLE hCompletion = complateport;
	DWORD dwTrans;
	conn_rec *c = NULL;
	LPOVERLAPPED ol = NULL;
	int iRet;
	struct buffer_packet_t *packet = NULL;
	BOOL bOK = FALSE;
	per_io_data *per_io = NULL;

	zlog_info(z_cate, "IO线程已启动 tid=%lu!", pthread_self());

	for(;;) {
		c = NULL;
		bOK = GetQueuedCompletionStatus(hCompletion, 
			&dwTrans, (LPDWORD)&c, (LPOVERLAPPED *)&ol, 1000*10);
		
		per_io = (per_io_data *)ol;
		if(bOK) {
			if(c==NULL && ol == NULL) {
				Sleep(10);
				break;
			}
			else if(c==1 && ol == NULL){
				xsend();
				continue;
			}
			else if(dwTrans == 0 && 
				(per_io->operation_type == OP_READ || per_io->operation_type == OP_WRITE))	{
				/* 套节字被对方关闭 */
				close_connect(c);
				continue;
			}		
		}
		else{
			if((c==NULL || c == 1) || ol == NULL){
				continue;
			}
			else { 
				/* 在此套节字上有错误发生 */
				close_connect(c);
				continue;
			}
		}
		if(per_io->operation_type == OP_READ){
			heart_handler(c);
			packet = buffer_queue_last(c->recv_queue);
			packet->remain_size -= dwTrans;
			c->recv_queue->size += dwTrans;
			iRet = packet_recv(c);
			if(iRet == RECV_COMPLATE){
				if(is_heart_beat(c) || c->read_callback == NULL){
					buffer_queue_detroy(c->recv_queue);
					c->recv_queue = buffer_queue_init(c->pool);
				}
				else{
					c->read_callback(c);
				}
				iRet = packet_recv(c);
			}
			if(iRet == RECV_FAILED){
				close_connect(c);
			}
		}
		else if(per_io->operation_type == OP_WRITE){
			if(dwTrans == 0){
				close_connect(c);
			}
			else{
				packet = buffer_queue_head(c->send_queue);
				packet->send_size += dwTrans;
				c->send_queue->size -= dwTrans;
				iRet = packet_send(c);
				if(iRet == SEND_COMPLATE){
					buffer_queue_detroy(c->send_queue);
					c->send_queue = NULL;
				}else if(iRet == SEND_FAILED){
					close_connect(c);
				}
			}
		}
		xsend();
	}
}

int xsend()
{
	int ret = 0;
	struct job_node_t   *node = NULL;
	pthread_mutex_lock(&result_queue_mutex);
	node = result_queue.p_head;
	while(node){
		conn_rec *c = node->con;
		if(c->send_queue){
			if(c->send_queue->size > 0){
				zlog_debug(z_cate, "发送缓冲区未发送完毕");
				buffer_queue_write_ex(c->send_queue, node->buf_queue);
				node = remove_result(node);
				continue;
			}
		}

		buffer_queue_detroy(c->send_queue);
		c->send_queue = node->buf_queue;
		ret = packet_send(c);
		if(ret == SEND_FAILED){
			zlog_debug(z_cate, "ret = SEND_FAILED");
			close_connect(c);
		}
		node = remove_result(node);
	}
	pthread_mutex_unlock(&result_queue_mutex);
}

int push_complate_packet(conn_rec *c)
{
	push_packet(c->recv_queue, c);
	c->recv_queue = buffer_queue_init(c->pool);
	return 0;
}

int length_to_read(conn_rec *c)
{
	if (c->recv_queue->size < sizeof(int)){
		return sizeof(int)-c->recv_queue->size;
	}else{
		int *len = (int *)c->recv_queue->p_head->buffer;
		if(*len >= MAX_DATALEN){
			zlog_error(z_cate, "数据包长度超过最大限定值 %d", *len);
			atomic_set(&c->aborted, 1);
			return -1;
		}else if(*len <= 0){
			zlog_error(z_cate, "数据长度错误 长度为=%d", *len);
			atomic_set(&c->aborted, 1);
			return -1;
		}
		return *len - c->recv_queue->size;
	}
}

int packet_recv(conn_rec *c)
{
	int aborted = 0, begin = 0, readlen = 0;
	int recvlen = 0;
	int needlen = 0;
	WSABUF buf;
	DWORD dwFlags = 0;
	DWORD dwBytes;
	struct buffer_packet_t *packet = NULL;
	
	needlen = length_to_read(c);
	if(needlen == -1){
		return RECV_FAILED;
	}
	if(needlen == 0){
		return RECV_COMPLATE;
	}

	packet = buffer_queue_last(c->recv_queue);
	begin = DATA_BUFSIZE - packet->remain_size;
	readlen = needlen<packet->remain_size?needlen:packet->remain_size;
	buf.buf = packet->buffer+begin;
	buf.len = readlen;
	if(WSARecv(c->fd, &buf, 1, &dwBytes, &dwFlags, &c->recv_per_io_data.ol, NULL) != NO_ERROR)
	{
		if(WSAGetLastError() != WSA_IO_PENDING)
		{
			zlog_debug(z_cate, "接收数据失败. 错误代码:%d", WSAGetLastError());
			return RECV_FAILED;
		}
	}	
	return RECV_AGAIN;
}

int packet_send(conn_rec *c)
{
	int aborted = 0;
	char *buf = NULL;
	int len = 0;
	int sendlen = 0;
	WSABUF wsabuf;
	DWORD dwBytes;
	DWORD dwFlags = 0;
	struct buffer_packet_t *packet = NULL;

	for(;;){
		packet = buffer_queue_head(c->send_queue);
		if(packet == NULL){
			return SEND_COMPLATE;
		}
		buf = packet->buffer + packet->send_size;
		len = DATA_BUFSIZE-(packet->send_size+ packet->remain_size);
		if(len == 0){
			buffer_queue_pop(c->send_queue);
			continue;
		}
		wsabuf.buf = buf;
		wsabuf.len = len;
		if(WSASend(c->fd, &wsabuf, 1, &dwBytes, dwFlags, &c->send_per_io_data.ol, NULL) != NO_ERROR) {
			if(WSAGetLastError() != WSA_IO_PENDING)
				zlog_debug(z_cate, "发送数据失败. 错误代码:%d", WSAGetLastError());
				return SEND_FAILED;
		}	
		return SEND_AGAIN;
	}
}

int s_connect(conn_rec *c)
{
	if(c->fd != INVALID_SOCKET){
		zlog_debug(z_cate, "关闭套接字");
		CancelIo((HANDLE)(c->fd));
		closesocket(c->fd);
		c->fd = INVALID_SOCKET;
		if(!deref(c)){
			pthread_mutex_lock(&conn_list_mutex);
			zlog_debug(z_cate, "移除连接");
			remove_connect(c);
			pthread_mutex_unlock(&conn_list_mutex);
		}
	}
}

int close_connect(conn_rec *c)
{
	if(c->close_callback){
		return c->close_callback(c);
	}
}

int is_heart_beat(conn_rec *c)
{
	int *len = (int *)(c->recv_queue->p_head->buffer);
	if(*len == 4){
		return 1;
	}
	return 0;
}

void wakeup()
{
	PostQueuedCompletionStatus(complateport, 0, (DWORD)1, NULL);
}
#endif
