#include <stdio.h>
#include <string.h>
#include <ctl.h>
#include "Error.h"
#ifdef SD_CARD_OUTPUT
  #include <crc.h>
  #include <ARCbus.h>
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
  static SD_blolck_addr current_block;
  static running;
#else
  //number of errors in a block
  #define NUM_ERRORS      (64)
  //A block of errors
  typedef struct{
    ERROR_DAT saved_errors[NUM_ERRORS];
  }ERROR_BLOCK;
  static ERROR_BLOCK errors;
#endif

//globals
static ERROR_BLOCK *err_dest;
int next_idx;
static CTL_MUTEX_t saved_err_mutex;

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
    running=0;
  #endif
}
  
#ifdef SD_CARD_OUTPUT
  static int write_error_block(SD_blolck_addr addr,ERROR_BLOCK *data){
    //compute CRC
    data->chk=crc16((unsigned char*)err_dest,sizeof(ERROR_BLOCK)-sizeof(data->chk));
    //write block
    return mmcWriteBlock(current_block,(unsigned char*)data);
  }
#endif

//start recording of errors
void error_recording_start(void){
  #ifdef SD_CARD_OUTPUT
    int resp,found;
    SD_blolck_addr addr,found_addr;
    ERROR_BLOCK *blk;
    unsigned char *buf;
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
        idx=(NUM_ERRORS)-1;
      }
      //check if error is valid
      if(err_dest->saved_errors[idx].valid!=SAVED_ERROR_MAGIC){
        //error not valid, exit
        break;
      }
      print_error(err_dest->saved_errors[idx].level,err_dest->saved_errors[idx].source,err_dest->saved_errors[idx].err,err_dest->saved_errors[idx].argument);
    }while(idx!=start);
  #endif
  #ifdef SD_CARD_OUTPUT
    resp=mmcInit_card();
    if(resp==MMC_SUCCESS){
      //lock card so that we can search uninterrupted
      resp=mmcLock();
      //check if card was locked
      if(resp==MMC_SUCCESS){
        //get buffer 
        buf=BUS_get_buffer(CTL_TIMEOUT_DELAY,100);
        //check if buffer acquired
        if(buf){
          //look for previous errors on SD card
          //TODO : add some way to find which error block is most recent
          for(addr=ERR_ADDR_START,found_addr=0,found=0;addr<ERR_ADDR_END;addr++){
            //read block
            resp=mmcReadBlock(addr,buf);
            //check for error
            if(resp==MMC_SUCCESS){
              //check for valid error block
              blk=(ERROR_BLOCK*)buf;
              //check signature values
              if(blk->sig1==ERROR_BLOCK_SIGNATURE1 && blk->sig2==ERROR_BLOCK_SIGNATURE2 && blk->sig3==ERROR_BLOCK_SIGNATURE3){
                //TODO: check CRC?
                found_addr=addr;
                found=1;
              }
            }else{
                //read failed
                //TODO: handle error 
            }
          }
        }
        //TODO: check for errors
        //check if an address was found
        if(found){
          //set error address
          current_block=found_addr+1;
          //check for wraparound
          if(current_block>ERR_ADDR_END){
            current_block=ERR_ADDR_START;
          }
        }else{
          //set address to first address
          current_block=ERR_ADDR_START;
        }
        //done using buffer
        BUS_free_buffer();
        //write current block
        write_error_block(current_block,err_dest);
        //ready to store errors
        running=1;
        //done using card, unlock
        mmcUnlock();
      }else{
        //could not lock SD card
        //TODO: handle error
      }
    }else{
      //could not init card
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
    //check if error code has been initialized
    if(running){
      //write block to SD card
      write_error_block(current_block,err_dest);
      if(full==BLOCK_FULL){
        //increment address
        current_block++;
        //TODO: check for wraparound
        //clear errors
        memset(&err_dest->saved_errors,0,sizeof(err_dest->saved_errors));
      }
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
  printf("%-14s (%3i) : %s\r\n",lev_str,level,(source<ERR_SRC_SUBSYSTEM?err_decode_arcbus:err_decode)(buf,source,err,argument));
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

//clear all errors saved on the SD card
int clear_saved_errors(void){
  int ret;
  //lock saved errors mutex
  ctl_mutex_lock(&saved_err_mutex,CTL_TIMEOUT_NONE,0);
  #ifdef SD_CARD_OUTPUT
     ret=mmcErase(ERR_ADDR_START,ERR_ADDR_END);
  #else
    ret=0;
  #endif
  if(!ret){
    //clear errors saved in RAM
    next_idx=0;
    memset(err_dest,0,sizeof(ERROR_BLOCK));
    #ifdef SD_CARD_OUTPUT
      //reset current block
      current_block=0;
      //set error signatures
      errors.sig1=ERROR_BLOCK_SIGNATURE1;
      errors.sig2=ERROR_BLOCK_SIGNATURE2;
      errors.sig3=ERROR_BLOCK_SIGNATURE3;
    #endif
  }
  ctl_mutex_unlock(&saved_err_mutex);
  return ret;
}

//print all errors in the log starting with the most recent ones
void error_log_replay(void){
  #ifdef SD_CARD_OUTPUT
    SD_blolck_addr addr=current_block;
    ERROR_BLOCK *blk;
    unsigned char *buf;
    int i,skip,resp;
    resp=mmcLock();
    //check if card was locked
    if(resp==MMC_SUCCESS){
      //get buffer 
      buf=BUS_get_buffer(CTL_TIMEOUT_DELAY,100);
      //check if buffer acquired
      if(buf){
        for(;;){
          //read block
          resp=mmcReadBlock(addr,buf);
          //check for error
          if(resp==MMC_SUCCESS){
            //check for valid error block
            blk=(ERROR_BLOCK*)buf;
            //check signature values
            if(blk->sig1==ERROR_BLOCK_SIGNATURE1 && blk->sig2==ERROR_BLOCK_SIGNATURE2 && blk->sig3==ERROR_BLOCK_SIGNATURE3){
              //check CRC
              if(blk->chk==crc16((unsigned char*)blk,sizeof(ERROR_BLOCK)-sizeof(blk->chk))){
                for(i=NUM_ERRORS-1,skip=0;i>=0;i--){
                  if(blk->saved_errors[i].valid==SAVED_ERROR_MAGIC){
                    if(skip>0){
                      printf("Skipping %i empty error slots\r\n",skip);
                      skip=0;
                    }
                    print_error(blk->saved_errors[i].level,blk->saved_errors[i].source,blk->saved_errors[i].err,blk->saved_errors[i].argument);
                  }else{
                    skip++;
                  }
                }
              }else{
                  printf("Error : invalid block CRC\r\n");
              }
            }else{
                printf("Error : invalid block header\r\n");
            }
          }else{
             printf("Error : failed to read from SD card : %s\r\n",SD_error_str(resp));
             return; 
          }
          if(addr==0){
            break;
          }
          //decrement address
          addr--;
        }
        //free buffer
        BUS_free_buffer();
      }
      //unlock card
      mmcUnlock();
    }
  #else
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
  #endif  
}
