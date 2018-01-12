#ifndef _ROUTE_TABLE_H
#define _ROUTE_TABLE_H
#include "apr_pools.h"
#include "pthread.h"

typedef struct route_rec_t{
	apr_pool_t *pool;
	char *hd_node; /*����ڵ�*/
	char *next_node; /*����ýڵ��·����ѡȡ����һ���ڵ�*/
	struct route_rec_t *next;
}route_rec;

typedef struct route_table_t
{
	pthread_mutex_t mutex;
	route_rec *head;
	int max_size;
	int size;
}route_table;

int init_route_table(route_table *t);
void add_route_node(route_rec *r);
#endif
