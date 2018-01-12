#ifndef _BUFFER_QUEUE_H_
#define _BUFFER_QUEUE_H_

#include "config.h"

struct buffer_packet_t
{
    char buffer[DATA_BUFSIZE];
    int remain_size;
    //�ѷ��ͳ��� ���ڷ��ͻ�����
    int send_size;
    struct buffer_packet_t  *next;
};

struct buffer_queue_t
{
	apr_pool_t *pool;
    struct buffer_packet_t   *p_head,*p_last;
    /*���������󳤶�(�ֽ���)*/
    unsigned short   max_size;
    /*������г���(�ֽ���)*/
    unsigned short   size;
};

struct buffer_queue_t * buffer_queue_init(apr_pool_t *con_pool);
void buffer_queue_detroy(struct buffer_queue_t *p_queue);
/*�������ܣ������������׷������*/
int buffer_queue_write(struct buffer_queue_t *p_queue,unsigned char *p_dest,int rd_len);
int buffer_queue_write_ex(struct buffer_queue_t *dest_queue,struct buffer_queue_t *src_dest);
/*�������ܣ��ӻ�������ж�ȡ���е�����*/
int buffer_queue_read(struct buffer_queue_t *rb,unsigned char *pdest, int rd_len);
/*�������ܣ��Ӷ���ȡ�����һ���п����������Ŀ飬����������򴴽�һ��*/
struct buffer_packet_t *buffer_queue_last(struct buffer_queue_t *queue);
/*�������ܣ�ɾ�������еĵ�һ����*/
struct buffer_packet_t *buffer_queue_pop(struct buffer_queue_t *queue);
/*�������ܣ�ȡ�������еĵ�һ��Ԫ��*/
struct buffer_packet_t *buffer_queue_head(struct buffer_queue_t *queue);
#endif //_BUFFER_QUEUE_H_