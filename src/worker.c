#include "worker.h"
#include <pthread.h>
#include "atomic.h"
#include "command.h"
#include "parser_bson_data.h"
#include "handler_command.h"


struct job_queue packet_queue;
pthread_mutex_t packet_queue_mutex;
pthread_cond_t packet_queue_cond;
struct job_queue result_queue;
pthread_mutex_t result_queue_mutex;
apr_pool_t *queue_pool;

void init_work_thread()
{
	packet_queue.p_head = packet_queue.p_last = NULL;
	packet_queue.max_size = 0;
	packet_queue.size = 0;
	result_queue.p_head = result_queue.p_last = NULL;
	result_queue.max_size = 0;
	result_queue.size = 0;

	 if(apr_pool_create(&queue_pool, server_rec) != APR_SUCCESS){
		 zlog_error(z_cate, "�����ڴ�س�ʼ��ʧ��!");
		 return;
	 }

	pthread_mutex_init (&result_queue_mutex, NULL);
	pthread_mutex_init (&packet_queue_mutex, NULL);
	pthread_cond_init(&packet_queue_cond, NULL);
}

struct job_node_t *create_job_node(struct buffer_queue_t* buf_queue, conn_rec *c)
{
	apr_pool_t *pool;
	struct job_node_t *j = NULL;
	if(apr_pool_create(&pool, queue_pool) != APR_SUCCESS){
		return NULL;
	}
	addref(c);
	j = (struct job_node_t *)apr_pcalloc(pool, sizeof(struct job_node_t));
	j->buf_queue = buf_queue;
	j->con = c;
	j->pool = pool;
	j->before = NULL;
	j->next = NULL;
	return j;
}

void push_packet(struct buffer_queue_t *buf_queue, conn_rec *c)
{
	pthread_mutex_lock(&packet_queue_mutex);
	if(packet_queue.p_last == NULL){
		packet_queue.p_last = packet_queue.p_head = create_job_node(buf_queue, c);
	}else{
		struct job_node_t *j = create_job_node(buf_queue, c);
		packet_queue.p_last->next = j;
		j->before = packet_queue.p_last;
		j->next = NULL;
		packet_queue.p_last = j;
	}
	pthread_cond_signal(&packet_queue_cond);
	pthread_mutex_unlock(&packet_queue_mutex);
}

struct buffer_queue_t *remove_packet(struct job_node_t *j)
{
	struct job_node_t *ret = j->next;
	if(j->before){
		j->before->next = j->next;
	}
	else{
		packet_queue.p_head = j->next;
	}
	if(j->next){
		j->next->before = j->before;
	}else{
		packet_queue.p_last = j->before;
	}

	deref(j->con);
	apr_pool_destroy(j->pool);
	return ret;
}

struct job_node_t *pop_front_packet()
{
	struct job_node_t *ret = NULL;
	ret = packet_queue.p_head;
	if(packet_queue.p_head){
		packet_queue.p_head = packet_queue.p_head->next;
		if(packet_queue.p_head == NULL){
			packet_queue.p_last = packet_queue.p_head;
		}
	}
	return ret;
}

void job_node_destroy(struct job_node_t *node)
{
	if(node){
		buffer_queue_detroy(node->buf_queue);
		deref(node->con);
		apr_pool_destroy(node->pool);
	}
}

void push_result(struct buffer_queue_t *buf_queue, conn_rec *c)
{
	pthread_mutex_lock(&result_queue_mutex);
	if(result_queue.p_last == NULL){
		result_queue.p_last = result_queue.p_head = create_job_node(buf_queue, c);
	}else{
		struct job_node_t *j = create_job_node(buf_queue, c);
		result_queue.p_last->next = j;
		j->before = result_queue.p_last;
		j->next = NULL;
		result_queue.p_last = j;
	}
	pthread_mutex_unlock(&result_queue_mutex);
}

struct buffer_queue_t *remove_result(struct job_node_t *j)
{
	struct job_node_t *ret = j->next;
	if(j->before){
		j->before->next = j->next;
	}
	else{
		result_queue.p_head = j->next;
	}
	if(j->next){
		j->next->before = j->before;
	}else{
		result_queue.p_last = j->before;
	}

	deref(j->con);
	apr_pool_destroy(j->pool);
	return ret;
}

/*����һ�����ݰ�����������*/
struct buffer_queue_t * do_packet(struct job_node_t *job)
{
	struct command_rec_t *req = NULL;
	bson_t * bson_result = NULL;
	struct command_rec_t *rep = NULL;
	int size = job->buf_queue->size;
	char *buf = apr_pcalloc(job->pool, size+1);
	struct context_rec *ctx = job->con->context;

	buffer_queue_read(job->buf_queue, buf, size);

	req= parse_bson(buf, size);
	if(req){
		rep = handler_command(req, ctx);
		if(rep){
			bson_result = encode_command_rep_to_bson(rep);
			if(bson_result){
				const uint8_t *data = bson_get_data (bson_result);
				int towrite = bson_result->len;
				struct buffer_queue_t *result_buf = buffer_queue_init(job->con->pool);
				buffer_queue_write(result_buf, data, towrite);
				bson_destroy(bson_result);
				command_rec_free(&req);
				command_rec_free(&rep);
				return result_buf;
			}
			command_rec_free(&rep);
		}
		command_rec_free(&req);
	}
	return NULL;
}

void *work_thread(void *p)
{
	struct job_node_t *first = NULL;
	struct buffer_queue_t *result_buf = NULL;

	zlog_info(z_cate, "�����߳������� tid=%lu!", pthread_self());
	while(!server_stop){
		pthread_mutex_lock(&packet_queue_mutex);
		while(NULL == (first = pop_front_packet())){
			pthread_cond_wait(&packet_queue_cond, &packet_queue_mutex);
			zlog_info(z_cate, "pthread_cond_wait!");
			if(server_stop){
				zlog_info(z_cate, "server_stop!");
				return 0;
			}
		}
		pthread_mutex_unlock(&packet_queue_mutex);
		if(atomic_read(&first->con->aborted) > 0){
			zlog_info(z_cate, "�����Ѿ��Ͽ����÷���������.");
			job_node_destroy(first);
			continue;
		}
		zlog_debug(z_cate, "���յ�һ�����ݰ�.");

		result_buf = do_packet(first);
		if(result_buf){
			push_result(result_buf, first->con);
			wakeup();
		}
		job_node_destroy(first);
	}
}
