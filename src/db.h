#ifndef _DB_H
#define _DB_H

#include "config.h"

#define T_DB_DOWN 2   //���ݿ�down
#define T_DB_FAIL 1   //���ݿ����
#define T_DB_OK 0
//���ݿ�����ѡ��
#define T_DB_CONNECT_ONCE   0   //������һ��
#define T_DB_CONNECT_NORMAL 1   //��������
#define T_DB_CONNECT_EXIT   2   //���Ӻ�ֱ���˳�

#define T_DB_WAIT_DOWN  3    //�����ȴ�ʱ��(��)

#define SUCCEED 0
#define FAILED 1

//�洢fetch��ĵ��м�¼
typedef char	**DB_ROW;
//�洢�����
typedef struct db_result_t	*DB_RESULT;
typedef struct db_conn_t	*DB_CON;

int	t_db_init(const char *dbname);
void t_db_close(struct db_conn_t *con);
int	t_db_connect(struct db_conn_t *con,char *host, char *user, char *password, char *dbname, char *dbschema, char *dbsocket, int port);
int	t_db_begin(struct db_conn_t *con);
int	t_db_commit(struct db_conn_t *con);
int	t_db_rollback(struct db_conn_t *con);
DB_RESULT	t_db_vselect(struct db_conn_t *con, const char *fmt, va_list args);
int	t_db_vexecute(struct db_conn_t *con, const char *fmt, va_list args);
DB_ROW	t_db_fetch(DB_RESULT result);
void DBfree_result(DB_RESULT result);
int	zbx_db_txn_level(void);
int	zbx_db_txn_error(void);
struct db_conn_t *create_db_connect();
void release_db_connect(struct db_conn_t *con);

#endif

