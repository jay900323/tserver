﻿#include "db_process.h"
#include "util.h"

void *db_read_thread(void *p)
{
	long int sec1, sec2, nextcheck;
	int	cmd_num = 0, sleeptime;
	DB_CON 	con = CreateDBConnect();

	sec1 = t_time();
	sec2 = sec1;
	zlog_info(z_cate, "指令线程已启动 tid=%lu!", pthread_self());

	sleeptime = COMMANDTHREAD_TIMEOUT - sec1 % COMMANDTHREAD_TIMEOUT;
	DBconnect(con, T_DB_CONNECT_NORMAL);

	for (;;)
	{
		t_sleep_loop(sleeptime);

		sec1 = t_time();
		cmd_num = fetch_command(con);
		sec2 = t_time();

		nextcheck = sec1 - sec1 % COMMANDTHREAD_TIMEOUT + COMMANDTHREAD_TIMEOUT;
		if (0 > (sleeptime = nextcheck - sec2))
			sleeptime = 0;
	}
	DBclose(con);
	return NULL;

}

bson_t *create_bson(DB_ROW	 row)
{
	int op_type = atoi(row[1]);
	char *op_command = row[2];
	struct command_rec_t *r = NULL;
	bson_t *bs = NULL;
	int fsize = 0;

	switch(op_type){
		case cmdline:
			r = command_rec_new(COMMAND_TYPE_CMD);
			r->data.exc_cmd.cmdline = apr_pstrdup(r->pool,row[2]);
			break;
		case file_upload:
			r = command_rec_new(COMMAND_TYPE_FILEULD);
			fsize = t_readfile(r->pool, row[2], &r->data.file_upload.content);
			if(fsize){
				r->data.file_upload.filesize = fsize;
				if(t_getfilename(r->pool, row[2], &r->data.file_upload.filename) <0) {
					command_rec_free(&r);
					return NULL;
				}
				zlog_info(z_cate, "文件上传命令 文件路径: %s 文件名:%s 文件长度 %d", row[2], r->data.file_upload.filename, fsize);
			}else{
				command_rec_free(&r);
				return NULL;
			}
			break;
		case file_download:
			r = command_rec_new(COMMAND_TYPE_FILEDLD);
			r->data.file_download.filepath = row[2];
			break;
	}
	r->src = apr_pstrdup(r->pool, row[3]);
	r->dest = apr_pstrdup(r->pool, row[4]);
	r->uuid = apr_pstrdup(r->pool,row[0]);
	bs = encode_command_rep_to_bson(r);
	command_rec_free(&r);
	return bs;
}

int fetch_command(DB_CON 	con)
{
	DB_RESULT	result;
	DB_ROW		row;
	bson_t *bs = NULL;
	conn_rec *c = NULL;

	result = DBselect(con, "select op_id, op_type, op_command, src_id, dest_id from opcommand");
	while (NULL != (row = DBfetch(result)))
	{
		zlog_info(z_cate, "op_id:%s, op_type:%s, op_command:%s src_id:%s, dest_id:%s", row[0], row[1], row[2], row[3], row[4]);

		bs = create_bson(row);
		if(!bs) continue;

		pthread_mutex_lock(&conn_list_mutex);
	    c = conn_list.conn_head;
	    while(NULL != c){
	    	pthread_mutex_lock(&c->context->context_mutex);
	    	if(c->context->id && !strcmp(c->context->id, row[4])){
				zlog_info(z_cate,"c->context->id %s, row[0] %s", c->context->id, row[4]);
				pthread_mutex_unlock(&c->context->context_mutex);
				if(bs){
					const uint8_t *data = bson_get_data (bs);
					int towrite = bs->len;
					struct buffer_queue_t *result_buf = buffer_queue_init(c->pool);
					buffer_queue_write(result_buf, data, towrite);
					if(result_buf){
						zlog_info(z_cate, "发送一条指令");
						push_result(result_buf, c);
						wakeup();
					}
				}
				break;
	    	}
	    	c = c->next;
	    }
	    if(bs){
	    	bson_destroy(bs);
	    }
	    pthread_mutex_unlock(&conn_list_mutex);
	}
	DBfree_result(result);
	return 0;
}

db_exec_list t_db_exec_list;
pthread_cond_t t_db_exec_cond;

int init_db_exec_list()
{
	t_db_exec_list.head = NULL;
	t_db_exec_list.max_size = 0;
	t_db_exec_list.size = 0;
	pthread_mutex_init(&t_db_exec_list.mutex, NULL);
	pthread_cond_init(&t_db_exec_cond, NULL);
	return 0;
}

void add_db_exec_rec(db_exec_rec *r)
{
	pthread_mutex_lock(&t_db_exec_list.mutex);
	if(!t_db_exec_list.head){
		t_db_exec_list.head = r;
		r->next = NULL;
	}else{
		r->next = t_db_exec_list.head;
		t_db_exec_list.head = r;
	}
	pthread_mutex_unlock(&t_db_exec_list.mutex);
	pthread_cond_signal(&t_db_exec_cond);
}

db_exec_rec *get_db_exec_rec()
{
	db_exec_rec *r = NULL;
	if(t_db_exec_list.head){
		r = t_db_exec_list.head;
		t_db_exec_list.head = r->next;
	}
	return r;
}

void *db_write_thread(void *p)
{
	db_exec_rec *r = NULL;
	DB_CON 	con = CreateDBConnect();
	DB_PARAMBIND bind;

	zlog_info(z_cate, "数据库线程启动 tid=%lu!", pthread_self());
	DBconnect(con, T_DB_CONNECT_NORMAL);

	for(;;){
		pthread_mutex_lock(&t_db_exec_list.mutex);
		while(NULL == (r = get_db_exec_rec())){
			pthread_cond_wait(&t_db_exec_cond, &t_db_exec_list.mutex);
			zlog_info(z_cate, "pthread_cond_wait!");
		}
		pthread_mutex_unlock(&t_db_exec_list.mutex);
		//DBexecute(con, "replace into opresult (op_id, op_result, op_type, op_status)"
		//	" values ('%s', '%s', '%d', '%d');", r->op_id, r->op_result, r->op_tye, r->op_status);
		bind = DB_Parambind_init(con, "replace into opresult (op_id, op_result, op_type, op_status)"
			" values (?, ?, ?, ?)");
		DB_setParambind(bind, 0, r->op_id, strlen(r->op_id), PARAM_TYPE_STRING);
		DB_setParambind(bind, 1, r->op_result, strlen(r->op_result), PARAM_TYPE_STRING);
		DB_setParambind(bind, 2, &r->op_tye, sizeof(r->op_tye), PARAM_TYPE_INT);
		DB_setParambind(bind, 3, &r->op_status, sizeof(r->op_status), PARAM_TYPE_INT);
		DB_Parambind_execute(bind);
		Parambind_free(bind);
		apr_pool_destroy(r->pool);

	}
	DBclose(con);
}
