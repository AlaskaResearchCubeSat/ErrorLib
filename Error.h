#ifndef __ERROR_H
#define __ERROR_H
#include <stdio.h>

//System Error sources, Any source greater then ERR_SRC_SUBSYSTEM is available for subsystem use
enum{ERR_SRC_ARCBUS=0,ERR_SRC_SUBSYSTEM=50};
  
//Address range for ERROR data on the SD card
enum{ERR_ADDR_START=0,ERR_ADDR_END=64};

//Error severity classes
enum{ERR_LEV_DEBUG=0,ERR_LEV_INFO=30,ERR_LEV_WARNING=60,ERR_LEV_ERROR=90,ERR_LEV_CRITICAL=120};

//setup for error reporting
void error_init(void);

//Start error recording
void error_recording_start(void);

//user function to decode errors
char *err_decode(char buf[150],unsigned short source,int err, unsigned short argument);

//set error level that gets recorded
unsigned char set_error_level(unsigned char lev);

//get error level
unsigned char get_error_level(void);

//report an error
void report_error(unsigned char level,unsigned short source,int err, unsigned short argument);

//Print all errors in log
void error_log_replay(unsigned short num,unsigned char level);

//clear all errors from the SD card (if used)
int clear_saved_errors(void);

#endif
  