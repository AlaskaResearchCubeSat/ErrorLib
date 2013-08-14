#ifndef __ERROR_H
#define __ERROR_H
#include <stdio.h>

//System Error sources, Any source greater then ERR_SRC_SUBSYSTEM is available for subsystem use
enum{ERR_SRC_ARCBUS=0,ERR_SRC_SUBSYSTEM=50};

//Error severity classes
enum{ERR_LEV_DEBUG=0,ERR_LEV_INFO=30,ERR_LEV_WARNING=60,ERR_LEV_ERROR=90,ERR_LEV_CRITICAL=120};

//setup for error reporting
void error_init(void);

//user function to decode errors
char *err_decode(char buf[150],unsigned short source,int err, unsigned short argument);

//set error level that gets recorded
unsigned char set_error_level(unsigned char lev);

//get error level
unsigned char get_error_level(void);

//report an error
void report_error(unsigned char level,unsigned short source,int err, unsigned short argument);

#endif
  