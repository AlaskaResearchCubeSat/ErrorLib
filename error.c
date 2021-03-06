#include <stdio.h>
#include <string.h>
#include <ctl.h>
#include "Error.h"
#include <ARCbus.h>
#include <commandLib.h>

#ifdef SD_CARD_OUTPUT
  #include <crc.h>
  #include <SDlib.h>
#endif

#define NUM_HANDLERS        (4)

typedef struct{
  char min,max;
  ERR_DECODE decode;
  unsigned short flags;
}ERR_DCODER;

static int err_next_decode=0;

static ERR_DCODER decode_tbl[NUM_HANDLERS];

int err_register_handler(char min, char max,ERR_DECODE decode,unsigned short flags){
  int i;
  //check for available decode slot
  if(err_next_decode>=NUM_HANDLERS){
    return ERR_TABLE_FULL;
  }
  //check that min is greater than max
  if(min>max){
    return ERR_INVALID_RANGE;
  }
  //check for overlapping ranges
  for(i=0;i<err_next_decode;i++){
    //check for overlapping errors
    if((decode_tbl[i].min<=max && decode_tbl[i].min>=min) ||
       (decode_tbl[i].max<=max && decode_tbl[i].max>=min) ){
      return ERR_OVERLAP;
    }
  }
  //add handler to list
  decode_tbl[err_next_decode].decode=decode;
  decode_tbl[err_next_decode].min=min;
  decode_tbl[err_next_decode].max=max;
  decode_tbl[err_next_decode].flags=flags;
  //increment index
  err_next_decode++;
  //success
  return RET_SUCCESS;
}

#define SAVED_ERROR_MAGIC   0xA5
//signature values for SD card storage
//TODO: decide on good values to use (below values are quite arbitrary)
#define ERROR_BLOCK_SIGNATURE1    0xA55A
#define ERROR_BLOCK_SIGNATURE2    0xCB31

void print_error(unsigned char level,unsigned short source,int err, unsigned short argument,ticker time);

//returned by _record_error to tell if a block has been filled
enum {BLOCK_NOT_FULL=0,BLOCK_FULL};


#ifdef SD_CARD_OUTPUT
  //number of errors in a block
  #define NUM_ERRORS      (504/sizeof(ERROR_DAT))
  //A block of errors
  typedef struct{
    //magic numbers to identify data from randomness
    unsigned short sig1,sig2;
    //error block number used to figure out which block is the most recent
    unsigned short number;
    //actual saved error data
    ERROR_DAT saved_errors[NUM_ERRORS];
    //CRC to make sure that data is not corrupted
    unsigned short chk;
  }ERROR_BLOCK;
  //place to store the error data
  static ERROR_BLOCK errors;
  //SD card address to store data to
  //static SD_blolck_addr current_block;
  static long current_block;
  //used to derermine if library is ready to store data to the SD card
  static running;
#else
  //number of errors in a block
  #define NUM_ERRORS      (64)
  //A block of errors
  typedef struct{
    //actual saved error data
    ERROR_DAT saved_errors[NUM_ERRORS];
  }ERROR_BLOCK;
  //place to store the error data
  static ERROR_BLOCK errors;
#endif

//pointer to the current block of errors
static ERROR_BLOCK *err_dest;
//next index to store errors to
int next_idx;
//mutex for error storage
static CTL_MUTEX_t saved_err_mutex;

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
    running=0;
  #endif
}
  
#ifdef SD_CARD_OUTPUT
  static int write_error_block(SD_block_addr addr,ERROR_BLOCK *data){
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
    SD_block_addr addr,found_addr;
    ERROR_BLOCK *blk;
    unsigned char *buf;
    unsigned short number;
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
      print_error(err_dest->saved_errors[idx].level,err_dest->saved_errors[idx].source,err_dest->saved_errors[idx].err,err_dest->saved_errors[idx].argument,err_dest->saved_errors[idx].time);
    }while(idx!=start);
  #endif
  #ifdef SD_CARD_OUTPUT
    resp=mmcInit_card();
    if(resp==MMC_SUCCESS){
      //lock card so that we can search uninterrupted
      resp=mmcLock(CTL_TIMEOUT_DELAY,2048);
      //check if card was locked
      if(resp==MMC_SUCCESS){
        //get buffer 
        buf=BUS_get_buffer(CTL_TIMEOUT_DELAY,100);
        //check if buffer acquired
        if(buf){
          //look for previous errors on SD card
          //TODO : add some way to find which error block is most recent
          for(addr=ERR_ADDR_START,found_addr=0,found=0,number=0;addr<ERR_ADDR_END;addr++){
            //read block
            resp=mmcReadBlock(addr,buf);
            //check for error
            if(resp==MMC_SUCCESS){
              //check for valid error block
              blk=(ERROR_BLOCK*)buf;
              //check signature values
              if(blk->sig1==ERROR_BLOCK_SIGNATURE1 && blk->sig2==ERROR_BLOCK_SIGNATURE2){
                //TODO: check CRC?
                //check block number is greater then found block
                if(blk->number>=number){
                  found_addr=addr;
                  found=1;
                  number=blk->number;
                }
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
          //set number
          err_dest->number=number+1;
        }else{
          //set address to first address
          current_block=ERR_ADDR_START;
          //set number to zero
          err_dest->number=0;
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
short _record_error(unsigned char level,unsigned short source,int err, unsigned short argument,ticker time){
  //set structures value
  err_dest->saved_errors[next_idx].level=level;
  err_dest->saved_errors[next_idx].source=source;
  err_dest->saved_errors[next_idx].err=err;
  err_dest->saved_errors[next_idx].argument=argument;
  err_dest->saved_errors[next_idx].time=time;
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
void record_error(unsigned char level,unsigned short source,int err, unsigned short argument,ticker time){
  short full;
  //lock saved errors mutex
  if(ctl_mutex_lock(&saved_err_mutex,CTL_TIMEOUT_NONE,0)){
    full=_record_error(level,source,err,argument,time);
    #ifdef SD_CARD_OUTPUT
      //check if error code has been initialized
      if(running){
        //write block to SD card
        write_error_block(current_block,err_dest);
        if(full==BLOCK_FULL){
          //increment address
          current_block++;
          //check for wraparound
          if(current_block>ERR_ADDR_END){
            current_block=ERR_ADDR_START;
          }
          //clear errors
          memset(&err_dest->saved_errors,0,sizeof(err_dest->saved_errors));
          //increment number
          err_dest->number++;
        }
      }
    #endif
    //done, unlock saved errors mutex
    ctl_mutex_unlock(&saved_err_mutex);
  }
}

//check error level and return appropriate string
const char* ERR_lev_str(unsigned char level){
  if(level<ERR_LEV_INFO){
    return "Debug";
  }else if(level<ERR_LEV_WARNING){
    return "Info";
  }else if(level<ERR_LEV_ERROR){
    return "Warning";
  }else if(level<ERR_LEV_CRITICAL){
    return "Error";
  }else{
    return "Critical Error";
  }
}

const char *err_do_decode(char buf[150],unsigned short source,int err, unsigned short argument,unsigned short flags){
  int i;
  //check for matching handler
  for(i=0;i<err_next_decode;i++){
    //check if range and flags are a match
    if(((!flags) || (decode_tbl[i].flags&flags)) && (decode_tbl[i].min<=source && decode_tbl[i].max>=source)){
      //call error handler
      return decode_tbl[i].decode(buf,source,err,argument);
    }
  }
  //source unknown, return string with error numbers
  sprintf(buf,"Unknown Source : source = %u, error = %u, argument = %u",source,err,argument);
  //return buffer
  return buf;
}

//print an error
void print_error(unsigned char level,unsigned short source,int err, unsigned short argument,ticker time){
  char buf[150];
  const char *lev_str;
  //check error level and use appropriate string
  lev_str=ERR_lev_str(level);
  //print message
  printf("%10lu:%-14s (%3i) : %s\r\n",time,lev_str,level,err_do_decode(buf,source,err,argument,0));
}

//report error function : record an error if it's level is greater then the log level
void report_error(unsigned char level,unsigned short source,int err, unsigned short argument){
  ticker time;
  //check log level
  if(level>=log_level){
    time=get_ticker_time();
    //if error level is above threshold then print and record the error
    record_error(level,source,err,argument,time);
    #ifdef PRINTF_OUTPUT
      print_error(level,source,err,argument,time);
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
    //clear errors saved in RAM
    next_idx=0;
    memset(err_dest,0,sizeof(ERROR_BLOCK));
    #ifdef SD_CARD_OUTPUT
      //reset current block
      current_block=0;
      //set error signatures
      errors.sig1=ERROR_BLOCK_SIGNATURE1;
      errors.sig2=ERROR_BLOCK_SIGNATURE2;
      errors.number=0;
    #endif
  ctl_mutex_unlock(&saved_err_mutex);
  return ret;
}
  
void error_log_mem_replay(unsigned char *dest,unsigned short size,unsigned char level,unsigned char *buf){
  //place to write number of errors to
  unsigned short *num=(unsigned short*)dest;
  int i,skip;
  #ifdef SD_CARD_OUTPUT
    SD_block_addr start=current_block,addr=start;
    ERROR_BLOCK *blk;
    unsigned long number=errors.number;
    int resp,last;
  #endif
  ERROR_DAT *_dest=(ERROR_DAT*)(dest+2);
  //set num to zero
  *num=0;
  //subtract the size of num from size
  size-=sizeof(*num);
  #ifdef SD_CARD_OUTPUT
    resp=mmcLock(CTL_TIMEOUT_DELAY,10);
    //check if card was locked
    if(resp==MMC_SUCCESS){
        for(;;){
          //read block
          resp=mmcReadBlock(addr,buf);
          //check for error
          if(resp==MMC_SUCCESS){
            //check for valid error block
            blk=(ERROR_BLOCK*)buf;
            //check signature values
            if(blk->sig1==ERROR_BLOCK_SIGNATURE1 && blk->sig2==ERROR_BLOCK_SIGNATURE2){
              //check CRC
              if(blk->chk==crc16((unsigned char*)blk,sizeof(ERROR_BLOCK)-sizeof(blk->chk))){
                if(number!=blk->number){
                  //update number
                  number=blk->number;
                }
                //loop through the block printing the most recent errors first
                for(i=NUM_ERRORS-1,skip=0;i>=0;i--){
                  //check if error is valid
                  if(blk->saved_errors[i].valid==SAVED_ERROR_MAGIC){
                    //check if error slots have been skipped
                    if(skip>0){
                      //TODO: add gap error?
                      //reset skip count
                      skip=0;
                    }
                    //check error level
                    if(blk->saved_errors[i].level>=level){
                        //copy error
                        memcpy(_dest++,&blk->saved_errors[i],sizeof(ERROR_DAT));
                        //increment count
                        (*num)++;
                        size-=sizeof(ERROR_DAT);
                        //check if there is room for more errors
                        if(size<sizeof(ERROR_DAT)){
                            //done!
                            break;
                        }
                    }
                  }else{
                    //no valid error, skip
                    skip++;
                  }
                }
                //check if dest is full
                if(size<sizeof(ERROR_DAT)){
                    break;
                }
              }else{
                  //block CRC is not valid TODO: do something
              }
            }else{
                //check if this block is expected to be the last
                if(last){
                  //exit loop
                  break;
                }
                //TODO: block header is not valid
            }
          }else{
             //error reading from SD card
             //TODO: return error code?
             //exit loop to prevent further errors
             break; 
          }
          //check if this should be the last block
          if(number==0){
             //set flag so code can exit silently next time
             last=1;
          }
          //next block should have a lower number decrement
          number--;
          //check if address is at the beginning
          if(addr==ERR_ADDR_START){
            //set address to the end
            addr=ERR_ADDR_END;
          }else{
            //decrement address
            addr--;
          }
          //check if there are more errors to display
          if(addr==start){
            //replay complete, exit
            break;
          }
        }
      //unlock card
      mmcUnlock();
    }else{
      //TODO: give error for SD card fail?
  #endif
      if(next_idx!=0)
            //print errors from buffer printing the most recent errors first
            for(i=next_idx-1,skip=0;i>=0;i--){
              //check if error is valid
              if(errors.saved_errors[i].valid==SAVED_ERROR_MAGIC){
                //check error level
                if(errors.saved_errors[i].level>=level){
                    //copy error
                    memcpy(_dest++,&errors.saved_errors[i],sizeof(ERROR_DAT));
                    //increment count
                    (*num)++;
                    size-=sizeof(ERROR_DAT);
                    //check if there is room for more errors
                    if(size<sizeof(ERROR_DAT)){
                        //done!
                        break;
                    }
                }
              }
            }

  #ifdef SD_CARD_OUTPUT
      }
  #endif
}      

//print errors in the log starting with the most recent ones
//print only errors with a level greater than level up to a maximum of num errors 
void error_log_replay(unsigned short num,unsigned char level){
  unsigned short ecount=0;
  #ifdef SD_CARD_OUTPUT
    SD_block_addr start=current_block,addr=start;
    ERROR_BLOCK *blk;
    unsigned long number=errors.number;
    unsigned char *buf;
    int i,skip,resp,last;
    resp=mmcLock(CTL_TIMEOUT_DELAY,10);
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
            if(blk->sig1==ERROR_BLOCK_SIGNATURE1 && blk->sig2==ERROR_BLOCK_SIGNATURE2){
              //check CRC
              if(blk->chk==crc16((unsigned char*)blk,sizeof(ERROR_BLOCK)-sizeof(blk->chk))){
                if(number!=blk->number){
                  //print message
                  printf("Missing block(s) expected #%u got #%u\r\n",number,blk->number);
                  //update number
                  number=blk->number;
                }/*else{
                  printf("Block #%u\r\n",blk->number);
                }*/
                //loop through the block printing the most recent errors first
                for(i=NUM_ERRORS-1,skip=0;i>=0;i--){
                  //check if error is valid
                  if(blk->saved_errors[i].valid==SAVED_ERROR_MAGIC){
                    //check if error slots have been skipped
                    if(skip>0){
                      //print the number of skipped error slots
                      //printf("Skipping %i empty error slots\r\n",skip);
                      printf("\r\n");
                      //reset skip count
                      skip=0;
                    }
                    //check error level
                    if(blk->saved_errors[i].level>=level){
                        //print error from error slot
                        print_error(blk->saved_errors[i].level,blk->saved_errors[i].source,blk->saved_errors[i].err,blk->saved_errors[i].argument,blk->saved_errors[i].time);
                        //check if we are counting
                        if(num!=0){
                            //increment count
                            ecount++;
                            //check if enough errors have been printed
                            if(ecount>=num){
                                //done!
                                break;
                            }
                        }
                    }
                  }else{
                    //no valid error, skip
                    skip++;
                  }
                }
                //check if enough errors have been printed
                if(num!=0 && ecount>=num){
                    //done!
                    break;
                }
              }else{
                  //block CRC is not valid, print error
                  printf("Error : invalid block CRC\r\n");
              }
            }else{
                //check if this block is expected to be the last
                if(last){
                  //exit loop
                  break;
                }
                //block header is not valid
                printf("Error : invalid block header\r\n");
            }
          }else{
             //error reading from SD card
             printf("Error : failed to read from SD card : %s\r\n",SD_error_str(resp));
             //exit loop to prevent further errors
             break; 
          }
          //check if this should be the last block
          if(number==0){
             //set flag so code can exit silently next time
             last=1;
          }
          //next block should have a lower number decrement
          number--;
          //check if address is at the beginning
          if(addr==ERR_ADDR_START){
            //set address to the end
            addr=ERR_ADDR_END;
          }else{
            //decrement address
            addr--;
          }
          //check if there are more errors to display
          if(addr==start){
            //replay complete, exit
            break;
          }
        }
        //free buffer
        BUS_free_buffer();
      }else{
        printf("Error : failed to get buffer\r\n");
      }
      //unlock card
      mmcUnlock();
    }else{
      printf("Error : Failed to lock SD card : %s\r\nPrinting Errors from RAM\r\n\r\n",SD_error_str(resp));
      if(next_idx==0){
         printf("No errors to display\r\n");
      }else{
            //print errors from buffer printing the most recent errors first
            for(i=next_idx-1,skip=0;i>=0;i--){
              //check if error is valid
              if(errors.saved_errors[i].valid==SAVED_ERROR_MAGIC){
                //check error level
                if(errors.saved_errors[i].level>=level){
                    //print error from error slot
                    print_error(errors.saved_errors[i].level,errors.saved_errors[i].source,errors.saved_errors[i].err,errors.saved_errors[i].argument,errors.saved_errors[i].time);
                    //check if we are counting
                    if(num!=0){
                        //increment count
                        ecount++;
                        //check if enough errors have been printed
                        if(ecount>=num){
                            //done!
                            break;
                        }
                    }
                }
              }
            }
      }
    }
  #else
    //print errors stored in error buffer stored in RAM
    int idx,start;
    //start at with the next index to print which gets decremented to start at the last error printed
    start=next_idx;
    //set index to start
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
      //check error level
      if(err_dest->saved_errors[idx].level>=level){
          //print error
          print_error(err_dest->saved_errors[idx].level,err_dest->saved_errors[idx].source,err_dest->saved_errors[idx].err,err_dest->saved_errors[idx].argument,err_dest->saved_errors[idx].time);
          //check if we are counting
          if(num!=0){
              //increment count
              ecount++;
              //check if enough errors have been printed
              if(ecount>=num){
                  //done!
                  break;
              }
          }
      }
    }while(idx!=start);
  #endif  
}

void print_spi_err(const unsigned char *dat,unsigned short len){
    const char *name;
    unsigned short num;
    int i;
    char buf[150];
    const ERROR_DAT *data;
    //check if it is a SPI error data block
    if(dat[0]!=SPI_ERROR_DAT){
        //print error and return
        printf("Error : data is not SPI error block\r\n");
        return;
    }
    //get sender address name
    name=I2C_addr_revlookup(dat[1],busAddrSym);
    if(name!=NULL){
        printf("Printing errors from %s (0x%02X)\r\n",name,dat[1]);
    }else{
        printf("Printing errors from address 0x%02X\r\n",i);
    }
    num=*(unsigned short*)(dat+2);
    for(i=0,data=(const ERROR_DAT*)(dat+4);i<num;i++){
        if(data[i].valid!=SAVED_ERROR_MAGIC){
            printf("Invalid error\r\n");
            continue;
        }
        //print message
        printf("%10lu:%-14s (%3i) : %s\r\n",data[i].time,ERR_lev_str(data[i].level),data[i].level,err_do_decode(buf,data[i].source,data[i].err,data[i].argument,ERR_FLAGS_LIB));
    }
}

