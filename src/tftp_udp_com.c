#include "tftp_udp.h"


int tftp_server_send_ack(TFTP_UDP_INFO *sock_info,unsigned short cmd,unsigned int block)
{
    TFTP_ACK ack;
    memset(&ack,0,sizeof(TFTP_ACK));
    ack.cmd = cmd;
    ack.block = block;
    int ret = sendto(sock_info->sockfd,&ack,sizeof(TFTP_ACK),0, \
            (struct sockaddr *)&sock_info->server,sizeof(struct sockaddr));
    if(ret != sizeof(TFTP_ACK))
    {
        printf("send_ack failed!with error %s\n",strerror(errno));
        return 0;
    }
    return 1;
}

int tftp_client_send_ack(TFTP_UDP_INFO *sock_info,unsigned short cmd,unsigned int block)
{
    TFTP_ACK ack;
    memset(&ack,0,sizeof(TFTP_ACK));
    ack.cmd = cmd;
    ack.block = block;
    int ret = sendto(sock_info->sockfd,&ack,sizeof(TFTP_ACK),0, \
        (struct sockaddr *)&sock_info->client,sizeof(struct sockaddr));
    if(ret != sizeof(TFTP_ACK))
    {
        printf("send_ack failed!with error %s\n",strerror(errno));
        return 0;
    }
    return 1;
}


int tftp_timeout_check(int fd,int wait_seconds)
{
    int ret = -1;

    if(wait_seconds > 0) 
    {
        fd_set read_fdset;     
        
        struct timeval timeout; 
        
        FD_ZERO(&read_fdset);
        FD_SET(fd,&read_fdset);
                
        timeout.tv_sec = wait_seconds;
        timeout.tv_usec = 0;
        do
        {
            ret = select(fd + 1,&read_fdset,NULL,NULL,&timeout);
            
        }while(ret < 0 && errno == EINTR);   
        
        if(ret == 0) 
        {
            ret = - 1;
            errno = ETIMEDOUT;
            return 0;
        }
        else if(ret > 0) 
        {
           return 1;
        }   
    }
    return 1;
}


unsigned int tftp_select_read_fd(int fd,int timeout_sec)
{
    fd_set read_fdset; 
    unsigned int ret = 0;
    FD_ZERO(&read_fdset);
    FD_SET(fd,&read_fdset);
    struct timeval timeout;
    memset(&timeout,0,sizeof(struct timeval));
    timeout.tv_sec = timeout_sec;
    
    ret = select(fd + 1,&read_fdset,NULL,NULL,&timeout);
    if(ret < 0 && errno == EINTR)
    {
        return 0;
    }
    if(ret == 0)
    {
        return 0;
    }
    if(ret > 0)
    {
        return 1;
    }
    return 1;
}

