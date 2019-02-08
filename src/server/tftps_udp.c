#include "tftp_udp.h"
#include <errno.h>
#include <libgen.h>


TFTP_UDP_INFO *sock_info;
int main(int argc,char *argv[])
{
    sock_info = malloc(sizeof(TFTP_UDP_INFO));
    struct sockaddr_in server;
    socklen_t addr_len = sizeof(struct sockaddr);
    memset(&server,0,sizeof(struct sockaddr_in));
    memset(sock_info,0,sizeof(TFTP_UDP_INFO));
    int sockfd = socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);
    if(sockfd < 0)
    {
        printf("socket failed!with error %s:\n",strerror(errno));
        return 0;
    }
    server.sin_family = AF_INET;
    server.sin_port = htons(10000);
    server.sin_addr.s_addr = INADDR_ANY;

    if(bind(sockfd, (struct sockaddr *)&server, sizeof(server)) < 0)
    {
        close(sockfd);
        printf("work_thread bind failed.with error %s\n",strerror(errno));
        return 0;
    }

    sock_info->sockfd = sockfd;
    memcpy(&sock_info->server,&server,sizeof(struct sockaddr_in));
    TFTP_OPT_PACK recv_opt_packet;
    memset(&recv_opt_packet,0,sizeof(TFTP_OPT_PACK));
    
    int ret = 0;
    while(1)
    {
        /*接收客户端请求*/
        ret = recvfrom(sock_info->sockfd,&recv_opt_packet,sizeof(TFTP_OPT_PACK),0, \
            (struct sockaddr *)&sock_info->server,&addr_len);
        if(ret < MIN_PACK_SIZE)
        {
            printf("recvfrom failed!\n");
            usleep(10000);
            continue;
        }
        switch(recv_opt_packet.cmd)
        {
            case WRQ_CODE:
                tftps_handle_wrq(sock_info,&recv_opt_packet);
                break;
            case RRQ_CODE:
                tftps_handle_rrq(sock_info,&recv_opt_packet);
                break;
            case MD5_CODE:
                tftps_handle_md5(sock_info,&recv_opt_packet);
                break;
            default:
                tftp_server_send_ack(sock_info,ERROR_CODE,ILLEGAL_OPER);
        }
    }   
}

/*****************************************************************************/
/** 
* \author      zhongfujin
* \date        2018/11/8
* \brief       写请求处理函数
* \param[in] sockfd 套接字
* \param[in] request_packet 请求包
* \return      无
* \ingroup     无
* \remarks     无
******************************************************************************/
unsigned int tftps_handle_wrq(TFTP_UDP_INFO *sock_info,TFTP_OPT_PACK *request_packet)
{
    char *filename = request_packet->opt;
    
    char full_path[256] = {'\0'};
    socklen_t addr_len = sizeof(struct sockaddr);
	char *file = basename(filename);
    sprintf(full_path,"/tftpboot/%s",file);
    printf("write request,file name is :%s\n",full_path);
    FILE *fp = fopen(full_path,"wb");
    if(fp == NULL)
    {
        printf("open or create file %s failed!with error:%s\n",full_path,strerror(errno));
        return 0;
    }
START:    
    if(tftp_server_send_ack(sock_info,CONF_CODE,0) == 0)
    {
        printf("send confirm ack failed!file name is :%s,with error %s\n", \
            full_path,strerror(errno));
        fclose(fp);
        return 0;
    }
    int block = 0;
    TFTP_PACKET recv_packet;
    int recv_ret = 0;
    int write_ret = 0;
    struct stat info;
    memset(&info,0,sizeof(info));
    while(1)
    {
        recv_ret = 0;
        memset(&recv_packet,0,sizeof(TFTP_PACKET));
        
        /*select超时处理*/
        if(1 == tftp_timeout_check(sock_info->sockfd,TFTP_TIMEOUT))
        {
            while(1)
            {
                recv_ret = recvfrom(sock_info->sockfd,&recv_packet,sizeof(TFTP_PACKET),0, \
                        (struct sockaddr *)&sock_info->server,&addr_len);
                if(recv_ret < PACKET_HEAD_SIZE)
                {
                    printf("recv error!filename is %s\n",full_path);
                    fclose(fp);
                    return 0;
                }
                
                else if(recv_ret > PACKET_HEAD_SIZE)
                {
                    /*防止通知ACK客户端未收到，多次发送请求报文*/
                    if(recv_packet.cmd == WRQ_CODE && block == 0)
                    {
                        goto START;
                    }
                    
                    /*过期报文丢弃*/
                    if(recv_packet.block < block)
                    {
                        printf("recv repeat block %d!file is %s\n",recv_packet.block,full_path);
                        continue;
                    }
                    break;
                }
                
                /*recv_ret = 4，表示为空包*/
                else if(recv_ret == PACKET_HEAD_SIZE)
                {
                    if(recv_packet.cmd == DATA_CODE)
                    {
                        if(recv_packet.block == 0)
                        {
                            printf("recv a last empty block!all block is %d,file is %s\n",block,full_path);
                            
                        }
                        break;
                    }
                }
            }
        }
        
        /*超时重传*/
        if(recv_ret == 0)
        {
            printf("timeout block %d lost!filename is %s\n",block,full_path);
            continue;
        }
        
        /*报文不足丢弃重新接收*/
        if(recv_ret < PACKET_HEAD_SIZE)
        {
            printf("bad size!size is %d,file is %s\n",recv_ret,full_path);
            continue;
        }

        /*接收到正常报文*/
        if(recv_ret > PACKET_HEAD_SIZE)
        {
            fseek(fp,(recv_packet.block - 1) * MAX_DATA_SIZE,SEEK_SET);
            write_ret = fwrite(&recv_packet.data,1,recv_ret - PACKET_HEAD_SIZE,fp);
            if(write_ret < 0)
            {
                if(errno == ENOSPC)
                {
                    printf("no more space，file %s\n",full_path);
                    if(0 == tftp_server_send_ack(sock_info,ERROR_CODE,DISK_FULL))
                    {
                        printf("send error ack failed!with error %s\n",strerror(errno));
                        
                    }
                    fclose(fp);
                    remove(full_path);
                    return 0;
                }
                else
                {
                    printf("write error! with error:%s\n",strerror(errno));
                    fclose(fp);
                    return 0;
                }
            }
  
            
            /*写的字符小于整包数据大小，表示最后一个包了*/
            if(write_ret < MAX_DATA_SIZE)
            {
                fflush(fp);
                printf("recv end,all block is %d!file is %s\n",recv_packet.block,full_path);
                if(tftp_server_send_ack(sock_info,ACK_CODE,0) == 0)
                {
                    printf("send last ack failed,block is %d,file is %s!with error %s\n", \
                        recv_packet.block,full_path,strerror(errno));
                    fclose(fp);
                    return 0;
                }

                /*进行size校验*/
                stat(full_path,&info);
                tftps_size_check(sock_info,info.st_size);
            
                break;
            }
        }
        else if(recv_ret == PACKET_HEAD_SIZE)
        {
            printf("recv end,all block is %d!file is %s\n",recv_packet.block,full_path);
            fflush(fp);
            if(tftp_server_send_ack(sock_info,ACK_CODE,0) == 0)
            {
                printf("send last ack failed,block is %d,file is %s!with error %s\n", \
                    recv_packet.block,full_path,strerror(errno));
                fclose(fp);
                return 0;
            }

            /*进行size校验*/
            stat(full_path,&info);
            tftps_size_check(sock_info,info.st_size);
            break;
        }
        
        
        /*发送确认ACK*/
        block = recv_packet.block;
        if(tftp_server_send_ack(sock_info,ACK_CODE,recv_packet.block) == 0)
        {
            printf("send ack failed,block is %d,file is %s!\n",recv_packet.block,full_path);
            continue;
        }
    }
    fclose(fp);
    return 1;
}

unsigned int tftps_handle_rrq(TFTP_UDP_INFO *sock_info,TFTP_OPT_PACK *request_packet)
{
    char *filename = request_packet->opt;
	
    printf("filename:%s\n",filename);
	
    char full_path[256] = {'\0'};
	
	sprintf(full_path,"/tftpboot/%s",filename);

    if(access(full_path,R_OK) == -1)
    {
        printf("%s is not exist\n",full_path);
        tftp_server_send_ack(sock_info,ERROR_CODE,FILE_NOT_FOUND);
        return 0;
    }
    
    FILE *fp = fopen(full_path,"rb");
    if(fp == NULL)
    {
        printf("open or create file %s failed!\n",full_path);
        return 0;
    }
    
    socklen_t addr_len = sizeof(struct sockaddr);
    TFTP_PACKET send_packet;
    TFTP_OPT_PACK opt_packet;
    TFTP_PACKET recv_packet;
    memset(&opt_packet,0,sizeof(TFTP_OPT_PACK));
    memset(&send_packet,0,sizeof(TFTP_PACKET));
    memset(&recv_packet,0,sizeof(TFTP_PACKET));

START:

    /*回复确认消息*/
    tftp_server_send_ack(sock_info,CONF_CODE,0);
    TFTP_ACK recv_ack;
    memset(&recv_ack,0,sizeof(TFTP_ACK));
    memset(&send_packet,0,sizeof(send_packet));
    
    int read_ret = 0;
    int send_ret = 0;
    int block = 1;
    int true_ack;
    while(1)
    {

        fseek(fp,(block - 1) * MAX_DATA_SIZE,SEEK_SET);
        read_ret = fread(&send_packet.data,1,MAX_DATA_SIZE,fp);
        if(read_ret < 0)
        {
            printf("read failed!\n");
        }
        printf("read size :%d\n",read_ret);
        send_packet.cmd = DATA_CODE;
        send_packet.block = block;
        
        /*发送数据包*/
        send_ret = sendto(sock_info->sockfd,&send_packet,read_ret + PACKET_HEAD_SIZE,0, \
            (struct sockaddr *)&sock_info->server,sizeof(struct sockaddr_in));
        if(send_ret == 0)
        {
            continue;
        }


        if(send_ret != read_ret + PACKET_HEAD_SIZE)
        {
            printf("send failed!block is %d\n",block);
            fclose(fp);
            return 0;
        }
        else
        {
            printf("send success,block is %d\n",block);
        }

        if(send_ret == MAX_DATA_SIZE + PACKET_HEAD_SIZE)
        {
            while(1)
            {
                if(1 == tftp_timeout_check(sock_info->sockfd,TFTP_TIMEOUT))
                {
                    
                    int size = recvfrom(sock_info->sockfd,&recv_ack,sizeof(TFTP_ACK),0, \
                            (struct sockaddr *)&sock_info->server,&addr_len);
                    if(size < 0 )
                    {
                        printf("bad size!\n");
                        fclose(fp);
                        return 0;
                    }
                    
                    if(recv_ack.cmd == ACK_CODE)
                    {
                        if(recv_ack.block == block)
                        {
                            block++;
                            true_ack = 1;
                            break;
                        }
                        else if(recv_ack.block < block)
                        {
                            true_ack = 0;
                            continue;
                        }
                    }
                    else if(recv_ack.cmd == RRQ_CODE)
                    {
                        goto START;
                    }
                }
                else
                {
                    printf("recv timeout!\n");
                    true_ack = 0;
                    break;
                }
                
            }
            if(true_ack == 0)
            {
                continue;
            }
            
        }

        /*小于正常数据包大小，最后一个包*/
        if(send_ret < MAX_DATA_SIZE + PACKET_HEAD_SIZE)
        {
            while(1)
            {
                if(1 == tftp_timeout_check(sock_info->sockfd,TFTP_TIMEOUT))
                {
                    int size = recvfrom(sock_info->sockfd,&recv_ack,sizeof(TFTP_ACK),0, \
                            (struct sockaddr *)&sock_info->server,&addr_len);
                    if(size < 0 )
                    {
                        printf("bad size!\n");
                        fclose(fp);
                        return 0;
                    }
                    
                    if(recv_ack.cmd == ACK_CODE )
                    {
                        /*最后一个ACK的block为0*/
                        if(recv_ack.block == 0)
                        {
                            true_ack = 1;
                            fclose(fp);
                            return 0;
                        }
                        else if(recv_ack.block > 0)
                        {
                            true_ack = 0;
                            continue;
                        }
                    }
                    else if(recv_ack.cmd == RRQ_CODE || recv_ack.cmd == WRQ_CODE)
                    {
                        true_ack = 1;
                        break;
                    }
                }
                else
                {
                    true_ack = 0;
                    printf("recv timeout!\n");
                    break;
                }
            }
            if(true_ack == 0)
            {
                continue;
            }
			else
			{
				break;
			}
        }
    }
    fclose(fp);
    return 1;
}


int tftps_handle_md5(TFTP_UDP_INFO *sock_info,TFTP_OPT_PACK *request_packet)
{
    char *filename = request_packet->opt;
    
    char *name = filename;

    char *md5_str = request_packet->opt + strlen(filename) + 1;
    
    char full_path[256] = {'\0'};
	
	sprintf(full_path,"/tftpboot/%s",name);

    printf("md5 request,file name is :%s\n",full_path);
    FILE *fp = fopen(full_path,"wb");
    if(fp == NULL)
    {
        printf("open or create file %s failed!with error:%s\n",full_path,strerror(errno));
        return 0;
    }

    if(TFTP_MD5_LEN == fwrite(md5_str,1,TFTP_MD5_LEN,fp))
    {
        printf("write md5 file %s OK!\n",full_path);
        fclose(fp);
    }
    else
    {
        printf("write md5 file %s failed!\n",full_path);
        fclose(fp);
    }
    
    if(tftp_server_send_ack(sock_info,CONF_CODE,0) == 0)
    {
        printf("send confirm ack failed!file name is :%s\n",full_path);
        return 0;
    }
    return 1;
  
}

/*****************************************************************************/
/** 
* \author      zhongfujin
* \date        2018/11/8
* \brief      size校验
* \param[in] sockfd 套接字
* \param[in] 文件长度
* \return      无
* \ingroup     无
* \remarks     无
******************************************************************************/
int tftps_size_check(TFTP_UDP_INFO *udp_info,unsigned int len)
{
    TFTP_ACK recv_ack;
    socklen_t addr_len = sizeof(struct sockaddr);
    unsigned int recv_time = 0;
    memset(&recv_ack,0,sizeof(TFTP_ACK));
    int ret = 0;
    while(recv_time < MAX_RESEND_TIME)
    {
        if(1 == tftp_timeout_check(udp_info->sockfd,TFTP_TIMEOUT))
        {
            int size = recvfrom(udp_info->sockfd,&recv_ack,sizeof(TFTP_ACK),0, \
                            (struct sockaddr *)&udp_info->client,&addr_len);
            if(size == PACKET_HEAD_SIZE)
            {
                if(recv_ack.cmd == SIZE_CODE)
                {
                    if(len == recv_ack.block)
                    {
                        ret = 1;
                        break;
                    }
                    else
                    {
                        ret = 0;
                        break;
                    }
                }
                
                /*若size校验时还收到DATA_CODE,表明客户端最后一个包的ack未收到，重发*/
                else if(recv_ack.cmd == DATA_CODE)
                {
                    tftp_client_send_ack(udp_info,ACK_CODE,0);
                    recv_time++;
                    continue;
                }
                else
                {
                    printf("invailed opt,cmd is %d\n",recv_ack.cmd);
                    ret = 0;
                    break;
                }
                
            }
            else
            {
                printf("bad size %d\n",size);
            }
        }
        else
        {
            recv_time++;
            continue;
        }
    }

   /*回复size校验结果*/
    if(ret == 1)
    {
        printf("size check success!\n");
        if(0 == tftp_client_send_ack(udp_info,SIZE_CODE,TFTP_SIZEOK))
        {
            printf("size check ack send failed!\n");
        }
    }
    else
    {
        printf("size check failed!\n");
        if(0 == tftp_client_send_ack(udp_info,SIZE_CODE,TFTP_SIZEBAD))
        {
            printf("size check ack send failed!\n");
        }
    }
    return ret;
}
