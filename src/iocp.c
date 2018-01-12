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

	zlog_info(z_cate, "�����߳������� tid=%lu!", pthread_self());
	while(TRUE)
	{
		if(WaitForSingleObject(exit_event, 0)==WAIT_OBJECT_0)
		{
			PostQueuedCompletionStatus(complateport, 0, (DWORD)NULL, NULL); //Ͷ�ݽ�����ɰ�
			Sleep(0); //�ó�CPUȨ����ҵ���߳��л����˳�		
			closesocket(listenfd);//���Բ�Ҫ
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
	zlog_info(z_cate, "IO�߳������� tid=%lu!", pthread_self());

	for(;;) {
		// �ڹ���������ɶ˿ڵ������׽����ϵȴ�I/O���
		BOOL bOK = GetQueuedCompletionStatus(hCompletion, 
			&dwTrans, (LPDWORD)&c, (LPOVERLAPPED *)&ol, 1000*10);  //WSA_INFINITE
		

		if(bOK) {//bOK==true 
			if(c==NULL && ol == NULL) {//�յ��˳��źŵ���ɰ�
				Sleep(10);
				break;
			}
			else if(c==1 && ol == NULL){
				xsend();
				continue;
			}
			else if(dwTrans == 0 &&	 // �׽��ֱ��Է��ر�
				(c->operation_type & OP_READ || 
				c->operation_type & OP_WRITE))	{
				close_connect(c);
				continue;
			}		
		}
		else{
			if(c==NULL || c == 1){
				continue;
			}
			else { //pPack!=NULL, �ڴ��׽������д�����
				close_connect(c);
				continue;
			}
		}
		if(c->operation_type & OP_READ){
			packet = buffer_queue_last(c->recv_queue);
			packet->remain_size -= dwTrans;
			c->recv_queue->size += dwTrans;
			iRet = packet_recv(c);
			if(iRet == RECV_COMPLATE){
				zlog_debug(z_cate, "���յ�һ�����������ݰ�c->recv_queue %d",c->recv_queue->size);
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
		else if(c->operation_type & OP_WRITE){
			if(dwTrans == 0){
				close_connect(c);
			}
			else{
				packet = buffer_queue_head(c->send_queue);
				packet->send_size += dwTrans;
				c->recv_queue->size -= dwTrans;
				iRet = packet_send(c);
				if(iRet == SEND_COMPLATE){
					buffer_queue_detroy(c->send_queue);
					c->send_queue = NULL;
					c->operation_type ^= OP_WRITE;
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
				zlog_debug(z_cate, "���ͻ�����δ�������");
				buffer_queue_write_ex(c->send_queue, node->buf_queue);
				node = remove_result(node);
				continue;
			}
		}

		buffer_queue_detroy(c->send_queue);
		c->send_queue = node->buf_queue;
		zlog_debug(z_cate, "�������� len=%d", c->send_queue->size);
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
	int nlen = 0;
	if (c->recv_queue->size < sizeof(int)){
		return sizeof(int)-c->recv_queue->size;
	}else{
		int *len = (int *)c->recv_queue->p_head->buffer;
		char buff[10];
		sprintf(buff,"%d",*len);
		nlen = atoi(buff);
		zlog_error(z_cate, "length_to_read nlen=%d", nlen);
		if(*len >= MAX_DATALEN){
			//data length is too large
			zlog_error(z_cate, "data length is too large %d", *len);
			atomic_set(&c->aborted, 1);
			return -1;
		}else if(nlen <= 0){
			zlog_error(z_cate, "data qlength is error *len=%d nlen=%d", *len, nlen);
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
	int total_recvlen = 0;
	int needlen = 0;
	WSABUF buf;
	DWORD dwFlags = 0;
	DWORD dwBytes;
	struct buffer_packet_t *packet = NULL;
	
	c->operation_type |= OP_READ;
	needlen = length_to_read(c);
	if(needlen == -1){
		return RECV_FAILED;
	}
	if(needlen == 0){
		return RECV_COMPLATE;
	}

	packet = buffer_queue_last(c->recv_queue);
	begin = DATA_BUFSIZE - packet->remain_size;
	readlen = needlen-total_recvlen<packet->remain_size?needlen-total_recvlen:packet->remain_size;
	buf.buf = packet->buffer+begin;
	buf.len = readlen;
	
	if(WSARecv(c->fd, &buf, 1, &dwBytes, &dwFlags, &c->ol, NULL) != NO_ERROR)
	{
		if(WSAGetLastError() != WSA_IO_PENDING)
		{
			zlog_debug(z_cate, "��������ʧ��. �������:%d", WSAGetLastError());
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

	c->operation_type |= OP_WRITE;
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
		if(WSASend(c->fd, &wsabuf, 1, &dwBytes, dwFlags, &c->ol, NULL) != NO_ERROR) {
			if(WSAGetLastError() != WSA_IO_PENDING)
				zlog_debug(z_cate, "��������ʧ��. �������:%d", WSAGetLastError());
				return SEND_FAILED;
		}	
		return SEND_AGAIN;
	}
}

int s_connect(conn_rec *c)
{
	zlog_debug(z_cate, "�ر��׽���");
	CancelIo((HANDLE)(c->fd));
	closesocket(c->fd);
	c->fd = INVALID_SOCKET;
	if(!deref(c)){
		pthread_mutex_lock(&conn_list_mutex);
		zlog_debug(z_cate, "�Ƴ�����");
		remove_connect(c);
		pthread_mutex_unlock(&conn_list_mutex);
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
