#ifndef _DB_PROCESS_H
#define _DB_PROCESS_H

#include "tdb.h"
#include "command.h"
#include "parser_bson_data.h"
#include "connect.h"
#include "pthread.h"

typedef struct db_exec_rec_t{
	apr_pool_t *pool;
	char *op_id;			/*����id*/
	char *op_result;    	/*����ִ�н��*/
	int op_tye;			/*�������� ��ϵͳ���� �ļ�������*/
	int op_status;			/*����ִ�е�״̬ 0 �ɹ� 1 ʧ�� 2δ���*/
	struct db_exec_rec_t *next;
}db_exec_rec;

typedef struct db_exec_list_t
{
	pthread_mutex_t mutex;
	db_exec_rec *head;
	int max_size;
	int size;
}db_exec_list;

void *db_read_thread(void *p);
int fetch_command(DB_CON 	con);
void add_db_exec_rec(db_exec_rec *r);
int init_db_exec_list();

#endif
