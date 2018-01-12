
#include "config.h"
#include "connect.h"
#include "atomic.h"

#ifdef _WIN32
HANDLE complateport;
SOCKET listenfd;
HANDLE exit_event;
#elif __linux__
int epfd;
int wakeupfd;
int listenfd;
#endif


int server_stop;
apr_pool_t *server_rec;
conn_rec_list conn_list;
pthread_mutex_t conn_list_mutex;


#ifdef _WIN32
FILE *z_cate;
#elif __linux__
zlog_category_t *z_cate;
#endif

int c_fd = 0;
char *db_host = "172.16.39.95";
char *db_user = "root";
char *db_password = "";
char *db_dbname = "cmd_system";
char *db_schema = NULL;
char *db_socket = NULL;
int db_port = 3306;

