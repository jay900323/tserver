#ifndef _TDB_H
#define _TDB_H
#include "db.h"

void	DBinit(void);
DB_CON CreateDBConnect();
void	DBclose(DB_CON con);
int DBconnect(DB_CON con, int flag);
void	DBbegin(DB_CON con);
void	DBcommit(DB_CON con);
void	DBrollback(DB_CON con);
void	DBend(DB_CON con, int ret);
int	DBexecute(DB_CON con, const char *fmt, ...);
DB_ROW	DBfetch(DB_RESULT result);
DB_RESULT DBselect_once(DB_CON con, const char *fmt, ...);
DB_RESULT DBselect(DB_CON con, const char *fmt, ...);

#endif
