#ifndef __ERROR_H
#define __ERROR_H
#include <stdio.h>
#include <ARCbus.h>
#include <MSP430.h>
  
//Address range for ERROR data on the SD card
enum{ERR_ADDR_START=0,ERR_ADDR_END=64};

//error codes for 
enum{ERR_TABLE_FULL=1,ERR_INVALID_RANGE,ERR_OVERLAP};

//flags for error decode functions
enum{ERR_FLAGS_LIB=BIT0,ERR_FLAGS_SUBSYSTEM=BIT1};

//Error severity classes
enum{ERR_LEV_DEBUG=0,ERR_LEV_INFO=30,ERR_LEV_WARNING=60,ERR_LEV_ERROR=90,ERR_LEV_CRITICAL=120};

//error descriptor structure
typedef struct{
  //"magic" number used to make sure it really is an error structure
  unsigned char valid;
  //error level
  unsigned char level;
  //error source
  unsigned short source;
  //error code
  int err;
  //more information about error (content depends on error code)
  unsigned short argument;
  //Ticker time that the error happened
  ticker time;
}ERROR_DAT;

//setup for error reporting
void error_init(void);

//Start error recording
void error_recording_start(void);

//user function to decode errors
typedef const char* (*ERR_DECODE)(char buf[150],unsigned short source,int err, unsigned short argument);

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

//read errors into a buffer
void error_log_mem_replay(unsigned char *dest,unsigned short size,unsigned char level,unsigned char *buf);

//print errors received over SPI
void print_spi_err(const unsigned char *dat,unsigned short len);

//register error handler with error library
int err_register_handler(char min, char max,ERR_DECODE decode,unsigned short flags);

#endif
  