#include <stdio.h>
#include <string.h>
#include <ctl.h>
#include "Error.h"
#include <crc.h>
#ifdef SD_CARD_OUTPUT
  #include <SDlib.h>
#endif

#define SAVED_ERROR_MAGIC   0xA5
//signature values for SD card storage
//TODO: decide on good values to use (below values are quite arbitrary)
#define ERROR_BLOCK_SIGNATURE1    0xA55A
#define ERROR_BLOCK_SIGNATURE2    0xCB31
#define ERROR_BLOCK_SIGNATURE3    0xE93A

void print_error(unsigned char level,unsigned short source,int err, unsigned short argument);

//returned by _record_error to tell if a block has been filled
enum {BLOCK_NOT_FULL=0,BLOCK_FULL};

//error descriptor structure
typedef struct{
  unsigned char valid,level;
  unsigned short source;
  int err;
  unsigned short argument;
}ERROR_DAT;

#ifdef SD_CARD_OUTPUT
  //number of errors in a block
  #define NUM_ERRORS      (504/sizeof(ERROR_DAT))
  //A block of errors
  typedef struct{
    unsigned short sig1,sig2,sig3;
    ERROR_DAT saved_errors[NUM_ERRORS];
    unsigned short chk;
  }ERROR_BLOCK;
  static ERROR_BLOCK errors;
  SD_blolck_addr current_block;
#else
  //number of errors in a block
  #define NUM_ERRORS      (64)
  //A block of errors
  typedef struct{
    ERROR_DAT saved_errors[NUM_ERRORS];
  }ERROR_BLOCK;
  static ERROR_BLOCK errors;
#endif
ERROR_BLOCK *err_dest;
int next_idx;
CTL_MUTEX_t saved_err_mutex;

//arcbus function to decode errors
char *err_decode_arcbus(char buf[150],unsigned short source,int err, unsigned short argument);

//log level
static char log_level=0;


//initialize error reporting system
void error_init(void){
  next_idx=0;
  memset(&errors,0,sizeof(ERROR_BLOCK));
  err_dest=&errors;
  ctl_mutex_init(&saved_err_mutex);
  #ifdef SD_CARD_OUTPUT
    current_block=-1;
    errors.sig1=ERROR_BLOCK_SIGNATURE1;
    errors.sig2=ERROR_BLOCK_SIGNATURE2;
    errors.sig3=ERROR_BLOCK_SIGNATURE3;
  #endif
}

//start recording of errors
void error_recording_start(void){
  #ifdef SD_CARD_OUTPUT
    int resp;
  #endif
  #ifdef PRINTF_OUTPUT 
    //print errors that may have occurred during startup
    int idx,start=next_idx;
    idx=start;
    do{
      //decrement idx
      idx--;
      //wrap around
      if(idx<0){
        idx=(sizeof(saved_errors)/sizeof(ERROR_DAT))-1;
      }
      //check if error is valid
      if(saved_errors[idx].valid!=SAVED_ERROR_MAGIC){
        //error not valid, exit
        break;
      }
      print_error(saved_errors[idx].level,saved_errors[idx].source,saved_errors[idx].err,saved_errors[idx].argument);
    }while(idx!=start);
  #endif
  #ifdef SD_CARD_OUTPUT
    resp=mmcInit_card();
    if(resp==MMC_SUCCESS){
      //TODO: look for prevous errors on SD card
    }else{
      //TODO: handle error
    }
  #endif
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

//record an error without locking, used for init code
short _record_error(unsigned char level,unsigned short source,int err, unsigned short argument){
  //set structures value
  err_dest->saved_errors[next_idx].level=level;
  err_dest->saved_errors[next_idx].source=source;
  err_dest->saved_errors[next_idx].err=err;
  err_dest->saved_errors[next_idx].argument=argument;
  err_dest->saved_errors[next_idx].valid=SAVED_ERROR_MAGIC;
  //increment index
  next_idx++;
  //wrap around
  if(next_idx>=(NUM_ERRORS)){
    next_idx=0;
    return BLOCK_FULL;
  }
  return BLOCK_NOT_FULL;
}

//put error data into array but don't do anything
void record_error(unsigned char level,unsigned short source,int err, unsigned short argument){
  short full;
  //lock saved errors mutex
  ctl_mutex_lock(&saved_err_mutex,CTL_TIMEOUT_NONE,0);
  full=_record_error(level,source,err,argument);
  #ifdef SD_CARD_OUTPUT
    //compute CRC
    err_dest->chk=crc16((unsigned char*)err_dest,sizeof(ERROR_BLOCK)-sizeof(err_dest->chk));
    //write block
    mmcWriteBlock(current_block,(unsigned char*)err_dest);
    if(full==BLOCK_FULL){
      //increment address
      current_block++;
      //TODO: check for wraparound
      //clear errors
      memset(&err_dest->saved_errors,0,sizeof(err_dest->saved_errors));
    }
  #endif
  //done, unlock saved errors mutex
  ctl_mutex_unlock(&saved_err_mutex);
}

//print an error
void print_error(unsigned char level,unsigned short source,int err, unsigned short argument){
  char buf[150];
  const char *lev_str;
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

//report error function : record an error if it's level is greater then the log level
void report_error(unsigned char level,unsigned short source,int err, unsigned short argument){
  //check log level
  if(level>=log_level){
    //if error level is above threshold then print and record the error
    record_error(level,source,err,argument);
    #ifdef PRINTF_OUTPUT
      print_error(level,source,err,argument);
    #endif
  }
}

//print all errors in the log starting with the most recent ones
void error_log_replay(void){
  int idx,start=next_idx;
  idx=start;
  do{
    //decrement idx
    idx--;
    //wrap around
    if(idx<0){
      idx=NUM_ERRORS-1;
    }
    //check if error is valid
    if(err_dest->saved_errors[idx].valid!=SAVED_ERROR_MAGIC){
      //error not valid, exit
      break;
    }
    print_error(err_dest->saved_errors[idx].level,err_dest->saved_errors[idx].source,err_dest->saved_errors[idx].err,err_dest->saved_errors[idx].argument);
  }while(idx!=start);
}
