#include "db.h"
#include "config.h"
#include <assert.h>

#define HAVE_MYSQL 1


#if defined(HAVE_IBM_DB2)
#	include <sqlcli1.h>
#elif defined(HAVE_MYSQL)
#	include "mysql.h"
#	include "errmsg.h"
#	include "mysqld_error.h"
#elif defined(HAVE_ORACLE)
#	include "oci.h"
#elif defined(HAVE_POSTGRESQL)
#	include <libpq-fe.h>
#elif defined(HAVE_SQLITE3)
#	include <sqlite3.h>
#endif

struct db_result_t
{
	apr_pool_t *pool;
#if defined(HAVE_IBM_DB2)
	SQLHANDLE	hstmt;
	SQLSMALLINT	nalloc;
	SQLSMALLINT	ncolumn;
	DB_ROW		values;
	DB_ROW		values_cli;
	SQLINTEGER	*values_len;
#elif defined(HAVE_MYSQL)
	MYSQL_RES	*result;
#elif defined(HAVE_ORACLE)
	OCIStmt		*stmthp;	/* the statement handle for select operations */
	int 		ncolumn;
	DB_ROW		values;
	ub4		*values_alloc;
	OCILobLocator	**clobs;
#elif defined(HAVE_POSTGRESQL)
	PGresult	*pg_result;
	int		row_num;
	int		fld_num;
	int		cursor;
	DB_ROW		values;
#elif defined(HAVE_SQLITE3)
	int		curow;
	char		**data;
	int		nrow;
	int		ncolumn;
	DB_ROW		values;
#endif
};

struct db_conn_t{
	apr_pool_t *pool;
#if defined(HAVE_MYSQL)
	MYSQL *conn;
#endif
};

//�����ύ�ȼ�
static int	txn_level = 0;
//
static int	txn_error = 0;
static apr_pool_t *db_pools = NULL;


#if defined(HAVE_MYSQL)
static int	is_recoverable_mysql_error(struct db_conn_t *con)
{
	switch (mysql_errno(con->conn))
	{
		case CR_CONN_HOST_ERROR:
		case CR_SERVER_GONE_ERROR:
		case CR_CONNECTION_ERROR:
		case CR_SERVER_LOST:
		case CR_UNKNOWN_HOST:
		case CR_COMMANDS_OUT_OF_SYNC:
		case ER_SERVER_SHUTDOWN:
		case ER_ACCESS_DENIED_ERROR:		/* wrong user or password */
		case ER_ILLEGAL_GRANT_FOR_TABLE:	/* user without any privileges */
		case ER_TABLEACCESS_DENIED_ERROR:	/* user without some privilege */
		case ER_UNKNOWN_ERROR:
			return SUCCEED;
	}

	return FAILED;
}
#endif

static int	t_db_execute(struct db_conn_t *con, const char *fmt, ...)
{
	va_list	args;
	int	ret;

	va_start(args, fmt);
	ret = t_db_vexecute(con, fmt, args);
	va_end(args);

	return ret;
}

int	t_db_init(const char *dbname)
{
	if(apr_pool_create(&db_pools, server_rec) != APR_SUCCESS){
		return FAILED;
	}
	return SUCCEED;
}

struct db_conn_t *create_db_connect()
{
	apr_pool_t *pool;
	struct db_conn_t *con = NULL;
	if(apr_pool_create(&pool, db_pools) != APR_SUCCESS){
		return NULL;
	}
	con = (struct db_conn_t *)apr_pcalloc(pool, sizeof(struct db_conn_t));
	con->pool = pool;
	con->conn = NULL;
	return con;
}

void release_db_connect(struct db_conn_t *con)
{
	if(con && con->pool){
		apr_pool_destroy(con->pool);
	}
}

int	t_db_connect(struct db_conn_t *con, char *host, char *user, char *password, char *dbname, char *dbschema, char *dbsocket, int port)
{
	int		ret = T_DB_OK, last_txn_error, last_txn_level;
#if defined(HAVE_MYSQL)
	my_bool		mysql_reconnect = 1;
#endif

	/* Allow executing statements during a connection initialization. Make sure to mark transaction as failed. */
	if (0 != txn_level)
		txn_error = 1;

	last_txn_error = txn_error;
	last_txn_level = txn_level;

	txn_error = 0;
	txn_level = 0;

#if defined(HAVE_MYSQL)

	if (NULL == (con->conn = mysql_init(NULL)))
	{
		zlog_fatal(z_cate, "cannot allocate or initialize MYSQL database connection object");
		exit(EXIT_FAILURE);
	}

	if (NULL == mysql_real_connect(con->conn, host, user, password, dbname, port, dbsocket, CLIENT_MULTI_STATEMENTS))
	{
		zlog_error(z_cate,"����mysqlʧ��. ���ݿ���: %s ��������:%s", dbname, mysql_error(con->conn));
		ret = T_DB_FAIL;
	}

	/* The RECONNECT option setting is placed here, AFTER the connection	*/
	/* is made, due to a bug in MySQL versions prior to 5.1.6 where it	*/
	/* reset the options value to the default, regardless of what it was	*/
	/* set to prior to the connection. MySQL allows changing connection	*/
	/* options on an open connection, so setting it here is safe.		*/

	if (0 != mysql_options(con->conn, MYSQL_OPT_RECONNECT, &mysql_reconnect))
		zlog_warn(z_cate, "Cannot set MySQL reconnect option.");

	/* in contrast to "set names utf8" results of this call will survive auto-reconnects */
	if (0 != mysql_set_character_set(con->conn, "utf8"))
		zlog_warn(z_cate, "cannot set MySQL character set to \"utf8\"");

	if (T_DB_OK == ret && 0 != mysql_select_db(con->conn, dbname))
	{
		zlog_error(z_cate,"����mysqlʧ��. ���ݿ���: %s ��������:%s", dbname, mysql_error(con->conn));
		ret = T_DB_FAIL;
	}

	if (T_DB_FAIL == ret && SUCCEED == is_recoverable_mysql_error(con))
		ret = T_DB_DOWN;

#endif
	if (T_DB_OK != ret)
		t_db_close(con);

	txn_error = last_txn_error;
	txn_level = last_txn_level;

	return ret;
}

void t_db_close(struct db_conn_t *con)
{
#if defined(HAVE_MYSQL)
	if (NULL != con->conn)
	{
		mysql_close(con->conn);
		con->conn = NULL;
	}
#endif
}

int	t_db_begin(struct db_conn_t *con)
{
	int	rc = T_DB_OK;

	if (txn_level > 0)
	{
		zlog_error(z_cate, "ERROR: nested transaction detected.");
		assert(0);
	}

	txn_level++;


#if defined(HAVE_MYSQL) || defined(HAVE_POSTGRESQL)
	rc = t_db_execute(con, "%s", "begin;");
#endif

	if (T_DB_DOWN == rc)
		txn_level--;

	return rc;
}

int	t_db_commit(struct db_conn_t *con)
{
	int	rc = T_DB_OK;

	if (0 == txn_level)
	{
		zlog_error(z_cate, "ERROR: commit without transaction.");
		assert(0);
	}

	//�������� ��������ع�
	if (1 == txn_error)
	{
		zlog_debug(z_cate, "commit called on failed transaction, doing a rollback instead");
		return t_db_rollback(con);
	}

#if defined(HAVE_MYSQL) || defined(HAVE_POSTGRESQL)
	rc = t_db_execute(con, "%s", "commit;");
#endif

	if (T_DB_DOWN != rc)	/* ZBX_DB_FAIL or ZBX_DB_OK or number of changes */
		txn_level--;

	return rc;
}

DB_RESULT	t_db_vselect(struct db_conn_t *con, const char *fmt, va_list args)
{
	apr_pool_t *tmp = NULL;
	apr_pool_t *pool = NULL;
	char		*sql = NULL;
	DB_RESULT	result = NULL;
	double		sec = 0;

	if(apr_pool_create(&tmp, db_pools) != APR_SUCCESS){
		return NULL;
	}
	sql = apr_pvsprintf(tmp, fmt, args);

	if (1 == txn_error)
	{
		zlog_debug(z_cate, "ignoring query [txnlev:%d] [%s] within failed transaction", txn_level, sql);
		goto clean;
	}

	zlog_debug(z_cate, "query [txnlev:%d] [%s]", txn_level, sql);

#if defined(HAVE_MYSQL)

	if(apr_pool_create(&pool, db_pools) != APR_SUCCESS){
		return NULL;
	}
	result = (struct db_result_t *)apr_pcalloc(pool, sizeof(struct db_result_t));
	result->pool = pool;
	result->result = NULL;

	if (NULL == con->conn)
	{
		DBfree_result(result);
		result = NULL;
	}
	else
	{
		if (0 != mysql_query(con->conn, sql))
		{
			zlog_error(z_cate, "���ݿ��ѯ����.��������:%s. sql���:%s", mysql_error(con->conn), sql);
			DBfree_result(result);
			result = (SUCCEED == is_recoverable_mysql_error(con) ? (DB_RESULT)T_DB_DOWN : NULL);
		}
		else
			result->result = mysql_store_result(con->conn);
	}
#endif

	if (NULL == result && 0 < txn_level)
	{
		zlog_debug(z_cate, "query [%s] failed, setting transaction as failed", sql);
		txn_error = 1;
	}
clean:
	apr_pool_destroy(tmp);

	return result;
}

void DBfree_result(DB_RESULT result)
{

#if defined(HAVE_MYSQL)
	if (NULL == result)
		return;

	mysql_free_result(result->result);
	apr_pool_destroy(result->pool);
#endif
}

int	t_db_vexecute(struct db_conn_t *con, const char *fmt, va_list args)
{
	char	*sql = NULL;
	int	ret = T_DB_OK;
	apr_pool_t *tmp = NULL;

#if defined(HAVE_MYSQL)
	int		status;
#endif

	if(apr_pool_create(&tmp, db_pools) != APR_SUCCESS){
		return T_DB_FAIL;
	}
	sql = apr_pvsprintf(tmp, fmt, args);

	if (0 == txn_level)
		zlog_debug(z_cate, "query without transaction detected");

	if (1 == txn_error)
	{
		zlog_debug(z_cate, "ignoring query [txnlev:%d] [%s] within failed transaction", txn_level, sql);
		ret = T_DB_FAIL;
		goto clean;
	}

	zlog_debug(z_cate, "query [txnlev:%d] [%s]", txn_level, sql);

#if defined(HAVE_MYSQL)
	if (NULL == con->conn)
	{
		ret = T_DB_FAIL;
	}
	else
	{
		if (0 != (status = mysql_query(con->conn, sql)))
		{
			zlog_debug(z_cate, "ִ�����ݲ������� ��������:%s, sql���:%s", mysql_error(con->conn), sql);
			ret = (SUCCEED == is_recoverable_mysql_error(con) ? T_DB_DOWN : T_DB_FAIL);
		}
		else
		{
			do
			{
				if (0 != mysql_field_count(con->conn))
				{
					zlog_debug(z_cate, "cannot retrieve result set");
					break;
				}

				ret += (int)mysql_affected_rows(con->conn);

				/* more results? 0 = yes (keep looping), -1 = no, >0 = error */
				if (0 < (status = mysql_next_result(con->conn)))
				{
					zlog_error(z_cate, "��ȡ��¼������.��������%s sql���:%s", mysql_error(con->conn), sql);
					ret = (SUCCEED == is_recoverable_mysql_error(con) ? T_DB_DOWN : T_DB_FAIL);
				}
			}
			while (0 == status);
		}
	}
#endif

	if (T_DB_FAIL == ret && 0 < txn_level)
	{
		zlog_debug(z_cate, "query [%s] failed, setting transaction as failed", sql);
		txn_error = 1;
	}
clean:
	apr_pool_destroy(tmp);

	return ret;
}

DB_ROW	t_db_fetch(DB_RESULT result)
{
	if (NULL == result)
		return NULL;

#if defined(HAVE_MYSQL)
	if (NULL == result->result)
		return NULL;

	return (DB_ROW)mysql_fetch_row(result->result);
#endif
}

int	t_db_rollback(struct db_conn_t *con)
{
	int	rc = T_DB_OK, last_txn_error;

	if (0 == txn_level)
	{
		zlog_fatal(z_cate, "ERROR: rollback without transaction. Please report it to Server Team.");
		assert(0);
	}

	last_txn_error = txn_error;

	/* allow rollback of failed transaction */
	txn_error = 0;

#if defined(HAVE_MYSQL) || defined(HAVE_POSTGRESQL)
	rc = t_db_execute(con, "%s", "rollback;");
#endif
	if (T_DB_DOWN != rc)	/* ZBX_DB_FAIL or ZBX_DB_OK or number of changes */
		txn_level--;
	else
		txn_error = last_txn_error;	/* in case of DB down we will repeat this operation */

	return rc;
}

int	zbx_db_txn_level(void)
{
	return txn_level;
}

int	zbx_db_txn_error(void)
{
	return txn_error;
}




