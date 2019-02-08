

#ifndef __TFTP_UDP__H
#define __TFTP_UDP__H

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <dirent.h>
#include <errno.h>


/*hafs opt code*/
#define RRQ_CODE        (short)1
#define WRQ_CODE        (short)2
#define DATA_CODE       (short)3
#define ACK_CODE        (short)4
#define CONF_CODE       (short)5
#define SIZE_CODE       (short)6
#define ERROR_CODE      (short)7
#define MD5_CODE        (short)8
#define TEST_CODE       (short)9

#define MAX_FILE_NAME_LEN   256
#define OPT_LEN             256
#define MAX_DATA_SIZE       4096

/*0 Not defined, see error message (if any).
 1 File not found.
 2 Access violation.
 3 Disk full or allocation exceeded.
 4 Illegal TFTP operation.
 5 Unknown transfer ID.
 6 File already exists.
 7 No such user.*/

/*error code*/
#define FILE_NOT_FOUND     1
#define ACCESS_VIOLATION   2
#define DISK_FULL          3
#define ILLEGAL_OPER       4
#define UNKNOW_TRANS_ID    5
#define FILE_ALREADY_EXIST 6
#define NO_SUCH_USER       7



/*packet size*/
#define MIN_PACK_SIZE       (sizeof(unsigned int) + sizeof(unsigned int))
#define PACKET_HEAD_SIZE    (sizeof(unsigned int) + sizeof(unsigned int))

/*timeout*/
#define TFTP_TIMEOUT     1

/*resend time*/
#define MAX_RESEND_TIME     5    

/*size check*/
#define TFTP_SIZEOK      1
#define TFTP_SIZEBAD     0


/*md5 len*/
#define TFTP_MD5_LEN     32

enum _opt
{
	GET_FILE,
	PUT_FILE,
	INVAILED
};

/*request packet struct*/
typedef struct opt_pack
{
    unsigned int cmd;
    char opt[MAX_FILE_NAME_LEN];
}TFTP_OPT_PACK;

/*DATA packet struct*/
typedef struct packet
{
    unsigned int cmd;
    union
    {
        unsigned int code;
        unsigned int block;
    };
    char data[MAX_DATA_SIZE];
}TFTP_PACKET;

/*ACK or CONF packet*/
typedef struct tag_ack
{
    unsigned int cmd;
    unsigned int block;
}TFTP_ACK;

/*udp info*/
typedef struct tag_tftp_udp_info
{
    struct sockaddr_in client;
    struct sockaddr_in server;
    int sockfd;
}TFTP_UDP_INFO;



int tftp_server_send_ack(TFTP_UDP_INFO *sock_info,unsigned short  cmd,unsigned int block);
int tftp_client_send_ack(TFTP_UDP_INFO *sock_info,unsigned short cmd,unsigned int block);
int tftp_timeout_check(int fd,int wait_seconds);

unsigned int tftps_handle_rrq(TFTP_UDP_INFO *sock_info,TFTP_OPT_PACK *request_packet);

unsigned int tftps_handle_wrq(TFTP_UDP_INFO *sock_info,TFTP_OPT_PACK *request_packet);

int tftps_handle_md5(TFTP_UDP_INFO *sock_info,TFTP_OPT_PACK *request_packet);

int tftps_size_check(TFTP_UDP_INFO *udp_info,unsigned int len);


int tftpc_get_file(char *filename);

int tftpc_send_file(char *filename);
int tftpc_size_check(TFTP_UDP_INFO *udp_info,unsigned int len);
int tftpc_send_file_md_str(char *filename,char *md5);


int tftpc_error_handle(unsigned int error_code);

unsigned int tftp_select_read_fd(int fd,int timeout_sec);

void tftpc_select_opt(char *opt,char *filename);

unsigned int tftps_get_direct_path(char *srcpath,char *destpath);

#endif



