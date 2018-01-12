#ifndef _WORKER_H
#define _WORKER_H

#include "connect.h"
#include "buffer_queue.h"
#include "apr_pools.h"

extern struct job_queue packet_queue;
extern struct job_queue result_queue;
extern pthread_mutex_t packet_queue_mutex;
extern pthread_mutex_t result_queue_mutex;
extern pthread_mutex_t thread_mutex;
extern pthread_cond_t thread_cond;
extern apr_pool_t *queue_pool;

struct job_node_t{
	apr_pool_t* pool;
	struct buffer_queue_t* buf_queue;
	struct job_node_t *before;
	struct job_node_t *next;
	conn_rec *con;

};

struct job_queue{
    struct job_node_t   *p_head,*p_last;
    unsigned short   max_size;
    unsigned short   size;
};
/*��������: �����յ����������ݰ��ŵ��������*/
void push_packet(struct buffer_queue_t *buf_queue, conn_rec *c);
/*��������: ������������Ƴ������ٸ��������ݰ�*/
struct buffer_queue_t *remove_packet(struct job_node_t *j);
/*��������: ��������е�����һ�����ݰ�*/
struct job_node_t *pop_front_packet();
void job_node_destroy(struct job_node_t *node);
void push_result(struct buffer_queue_t *buf_queue, conn_rec *c);
struct buffer_queue_t *remove_result(struct job_node_t *j);
struct job_node_t *create_job_node(struct buffer_queue_t* buf_queue, conn_rec *c);
void *work_thread(void *p);
#endif
