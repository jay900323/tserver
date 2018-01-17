#include <stdio.h>
#include <time.h>
#ifdef _WIN32
#include <Windows.h>
#endif
#include "util.h"
#include "apr_strings.h"


#define UUID_FORMAT "0x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x"


#define UUID_FORMAT_STR(uuid) ((unsigned char *)uuid)[0], ((unsigned char *)uuid)[1], ((unsigned char *)uuid)[2], ((unsigned char *)uuid)[3], \
                                  ((unsigned char *)uuid)[4], ((unsigned char *)uuid)[5], ((unsigned char *)uuid)[6], ((unsigned char *)uuid)[7], \
                                  ((unsigned char *)uuid)[8], ((unsigned char *)uuid)[9], ((unsigned char *)uuid)[10], ((unsigned char *)uuid)[11], \
                                  ((unsigned char *)uuid)[12], ((unsigned char *)uuid)[13], ((unsigned char *)uuid)[14], ((unsigned char *)uuid)[15]


/*
int uuid_is_valid_string (const char *str)
{
  uuid_t id;
  if (uuid_parse (str, id) == 0)
    return 1;
  else
    return 0;
}
*/
/*
char * uuid_create_hex_string (){
	uuid_t id;
	char* hex = malloc(37);
	while ((uuid_generate_time_safe (id)) != 0)
	    usleep (10);

	uuid_unparse_lower (id, hex);
	return hex;
}
*/
time_t t_time()
{
	return time(NULL);
}

void t_sleep_loop(int second)
{
#ifdef _WIN32
	Sleep(second*1000);
#elif __linux__
	sleep(second);
#endif
}

int t_getfilename(apr_pool_t *pool, char *filename, char **buf)
{
	char *pos = filename;
	while(*pos){
		pos++;
	}
	pos--;
	while(*pos && pos != filename){
		if(*pos == '/' || *pos == '\\'){
			pos++;
			if(*pos){
				*buf = apr_pstrdup(pool, pos);
				return 0;
			}else{
				return -1;
			}
		}
		pos--;
	}
	return -1;
}

int t_readfile(apr_pool_t *pool, char *filename, char **buf)
{
	FILE *fp = 0;
	int file_len = 0;
	int ret = 1;
	char *buffer = NULL;

	if((fp=fopen(filename, "rb"))== NULL){
		return 0;
	}
	fseek(fp, 0L, SEEK_END);
	file_len = ftell(fp);
	fseek(fp, 0L, SEEK_SET);
	buffer = *buf = (char*)apr_palloc(pool, file_len);

	while(ret){
		ret = fread(buffer, 1024, 1, fp);
		if(ret == -1){
			fclose(fp);
			return 0;
		}
		buffer +=ret;
	}
	fclose(fp);
	return file_len;
}
