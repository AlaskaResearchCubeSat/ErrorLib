
#include <stdio.h>
#include "Error.h"


//arcbus function to decode errors
char *err_decode_arcbus(char buf[150],unsigned short source,int err, unsigned short argument);

//log level
static char log_level=ERR_LEV_WARNING;


//initialize error reporting system
void error_init(void){

}

//set log level that triggers errors to be recorded
unsigned char set_error_level(unsigned char lev){
  unsigned char tmp=log_level;
  log_level=lev;
  return tmp;
}

//get the log level
unsigned char get_error_level(void){
  return log_level;
}

//report error function : record an error if it's level is greater then the log level
void report_error(unsigned char level,unsigned short source,int err, unsigned short argument){
  char buf[150];
  const char *lev_str;
  //check log level
  if(level>=log_level){
    //check error level and use appropriate string
    if(level<ERR_LEV_INFO){
      lev_str="Debug";
    }else if(level<ERR_LEV_WARNING){
      lev_str="Info";
    }else if(level<ERR_LEV_ERROR){
      lev_str="Warning";
    }else if(level<ERR_LEV_CRITICAL){
      lev_str="Error";
    }else{
      lev_str="Critical Error";
    }
    //print message
    printf("%s (%i) : %s\r\n",lev_str,level,(source<ERR_SRC_SUBSYSTEM?err_decode_arcbus:err_decode)(buf,source,err,argument));
  }
}

