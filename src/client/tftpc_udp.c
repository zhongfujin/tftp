#include <errno.h>
#include <sys/types.h>
#include <dirent.h>
#include "tftp_udp.h"

TFTP_UDP_INFO *g_udp_info;    /*the sock info*/
unsigned int cmd_type;			/*the type of cmd,get or put?*/

int main(int argc,char *argv[])
{
    g_udp_info = malloc(sizeof(TFTP_UDP_INFO));
    memset(g_udp_info,0,sizeof(TFTP_UDP_INFO));
    g_udp_info->sockfd = socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);
    if(g_udp_info->sockfd < 0)
    {
       printf("socket failed!with error %s\n",strerror(errno));
        return 0;
    }
    g_udp_info->client.sin_family = AF_INET;
    g_udp_info->client.sin_port = htons(10000);
    inet_pton(AF_INET, "172.0.0.1", &(g_udp_info->client.sin_addr.s_addr));
	char cmd[4101] = {'\0'};
	char filename[4096];
	while(1)
	{
		printf("tftp>");
		memset(cmd,0,4101);
		memset(filename,0,4096);
		fgets(cmd,4096,stdin);
		cmd[strlen(cmd) - 1] = '\0';
		tftpc_select_opt(cmd,filename);
		switch(cmd_type)
		{
			case GET_FILE:
				tftpc_get_file(filename);
				break;
			case PUT_FILE:
				if(access(filename,F_OK) != 0)
				{
					fprintf(stderr,"No such file%s\n",filename);
					break;
				}
				tftpc_send_file(filename);
				break;
			default:
				printf("Invailed opt!\n");
		}
		
	}
    return 1;
}

int tftpc_send_file(char *filename)
{
    struct sockaddr_in sender;
    int send_ret = 0;
    unsigned int send_all_size = 0;
    socklen_t addr_len = sizeof(struct sockaddr);
    TFTP_PACKET send_packet;
    TFTP_OPT_PACK opt_packet;
    TFTP_PACKET recv_packet;
    unsigned int resend_count = 0;
    
    memset(&opt_packet,0,sizeof(TFTP_OPT_PACK));
    memset(&send_packet,0,sizeof(TFTP_PACKET));
    memset(&recv_packet,0,sizeof(TFTP_PACKET));
    memset(&sender,0,sizeof(struct sockaddr_in));

    if(access(filename,F_OK) != 0)
    {
        printf("no such file %s\n",filename);
        return 0;
    }
    
    FILE *fp = fopen(filename,"rb");
    if(NULL == fp)
    {
        printf("open failed!\n");
        return 0;
    }
	struct stat info;
	memset(&info,0,sizeof(struct stat));
	stat(filename,&info);
	unsigned int buflen = info.st_size;

    opt_packet.cmd = WRQ_CODE;
    sprintf(opt_packet.opt,"%s%c%s%c",filename,0,"octet",0);
    
    /*send WRQ request*/
AGAIN:
    resend_count++;
    if(resend_count > MAX_RESEND_TIME)
    {
        printf("send WRQ request more than %d times,file %s\n",MAX_RESEND_TIME,filename);
        fclose(fp);
        return 0;
    }
    send_ret = sendto(g_udp_info->sockfd,&opt_packet,sizeof(TFTP_OPT_PACK),0, \
            (struct sockaddr *)&g_udp_info->client,sizeof(struct sockaddr_in));
    
    /*sendto failed! resend*/
    if(send_ret == -1)
    {
        printf("send request failed!with error %s\n",strerror(errno));
        fclose(fp);
        return 0;
    }
    TFTP_ACK recv_ack;
    memset(&recv_ack,0,sizeof(TFTP_ACK));
    memset(&send_packet,0,sizeof(send_packet));

    /*recv the server ack*/
    if(1 ==  tftp_timeout_check(g_udp_info->sockfd,TFTP_TIMEOUT))
    {
        if(-1 == recvfrom(g_udp_info->sockfd,&recv_ack,sizeof(TFTP_ACK),0, \
                (struct sockaddr *)&sender,&addr_len))
        {
            /*recv failed!again*/
            printf("recvfrom failed!with error:%s\n",strerror(errno));
            goto AGAIN;
        }
        
        /*recv packet,and type is confirm code*/
        if(recv_ack.cmd == CONF_CODE && recv_ack.block == 0)
        {
            
            printf("recv confirm ACK!file is %s\n",filename);
        }
        
        /*recv error */
        else if(recv_ack.cmd == ERROR_CODE)
        {
            printf("error_code %d,file is %s\n",recv_ack.block,filename);
            tftpc_error_handle(recv_ack.block);
            goto AGAIN;
        }
        
        /*invailed ack,again*/
        else
        {
            printf("recv ack invailed %d!file is %s\n",recv_ack.cmd,filename);
            goto AGAIN;
        }
    }
    
    /*recv timeout,again*/
    else
    {
        printf("time out,resend WRQ request,file is %s\n",filename);
        goto AGAIN;
    }

    resend_count = 0;
    unsigned int block = 1;
    int true_ack = 0;
    int send_size = 0;
    int read_ret = 0;

    while(1)
    {
		/*resend time count*/
        resend_count++;
        if(resend_count > MAX_RESEND_TIME)
        {
            printf("send %d bolck more than %d times,file %s\n",block,MAX_RESEND_TIME,filename);
            fclose(fp);
            return 0;
        }
        
        /*fseek*/
        unsigned int ret = fseek(fp,(block - 1) * MAX_DATA_SIZE,SEEK_SET);
        if(-1 == ret)
        {
            printf("fseek failed!with error %s\n",strerror(errno));
            return 0;
        }
  
		/*read file and fill the data packet*/
        read_ret = fread(&send_packet.data,1,MAX_DATA_SIZE,fp);
        if(read_ret < 0)
        {
            printf("read failed!,ret = %d,error: %s\n",read_ret,strerror(errno));
            fclose(fp);
            return 0;
        }
		
		/*opt code*/
        send_packet.cmd = DATA_CODE;
		
		/*block*/
        send_packet.block = block;
		
		/*the send size*/
        send_size = read_ret + PACKET_HEAD_SIZE;
        
        /*send data packet*/
        send_ret = sendto(g_udp_info->sockfd,&send_packet,send_size,0, \
            (struct sockaddr *)&g_udp_info->client,sizeof(struct sockaddr_in));
        
        /*send failed!over*/
        if(send_ret == -1)
        {
            printf("send block failed!block is %d\n",block);
            fclose(fp);
            return 0;
        }
		
		/*send byte not equal the packet size*/
        if(send_ret != send_size)
        {
            //printf("send failed!block is %d,resend\n",block);
            continue;
        }

        /*recv ack,if send ret is MAX_DATA_SIZE + PACKET_HEAD_SIZE,then this is a normal packet,and the next data packet should be sent*/
        if(send_ret == MAX_DATA_SIZE + PACKET_HEAD_SIZE)
        {
            while(1)
            {
                if(1 == tftp_timeout_check(g_udp_info->sockfd,TFTP_TIMEOUT))
                {
                    int size = recvfrom(g_udp_info->sockfd,&recv_ack,sizeof(TFTP_ACK),0, \
                        (struct sockaddr *)&g_udp_info->client,&addr_len);
                    if(size < 0 )
                    {
                        printf("recv error!with error %s\n",strerror(errno));
                        fclose(fp);
                        return 0;
                    }
                    
                    /*recv error*/
                    if(recv_ack.cmd == ERROR_CODE)
                    {
                        int error_code = recv_ack.block;
						
						/*if error code is ILLEGAL_OPER,so the server get function is over*/
                        if(recv_ack.block == ILLEGAL_OPER)
                        {
                            printf("server write over\n");
                            fclose(fp);
                            return 1;
                        }
                        tftpc_error_handle(error_code);

                    }
                    if(recv_ack.cmd == ACK_CODE)
                    {
                    
                        /*recv ack and check successful*/
                        if(recv_ack.block == block)
                        {
                            true_ack = 1;
                            resend_count = 0;
                            send_all_size = send_all_size + send_ret - PACKET_HEAD_SIZE;
                            block++;
                            break;
                        }
                        
                        /*check failed*/
                        else
                        {
                            printf("server recv a wrong block,block is %d\n!",block);
                            true_ack = 0;
                            break;
                        }
                    }
                }
                else
                {
                    /*recv timeout*/                  
                    true_ack = 0;
                    printf("recv ack timeout!block is %d\n",block);
                    break;
                }
            }
            
            /*not recv ack or ack is invailed!*/
            if(true_ack == 0)
            {
                continue;
            }  
        }
        
        /*如果发送的数据小于516，表明发送完成，等待服务端的最后一个ACK*/
        if(send_ret < MAX_DATA_SIZE + PACKET_HEAD_SIZE)
        {
            while(1)
            {
                resend_count++;
                if(resend_count > MAX_RESEND_TIME)
                {
                    printf("recv last ack more than %d times,file %s\n",MAX_RESEND_TIME,filename);
                    fclose(fp);
                    return 0;
                }
                
                if(1 == tftp_timeout_check(g_udp_info->sockfd,TFTP_TIMEOUT))
                {
                    int size = recvfrom(g_udp_info->sockfd,&recv_ack,sizeof(TFTP_ACK),0, \
                        (struct sockaddr *)&g_udp_info->client,&addr_len);
                    if(size < 0 )
                    {
                        printf("recv error!with error %s\n",strerror(errno));
                        fclose(fp);
                        return 0;
                    }
                    
                    /*收到ERROR_CODE*/
                    if(recv_ack.cmd == ERROR_CODE)
                    {
                        /*收到ILLEGAL_OPER类型错误，表示服务端已接受完文件，退出了*/
                        int error_code = recv_ack.block;
                        
                        if(error_code == ILLEGAL_OPER)
                        {
                            printf("server write over!\n");
                            fclose(fp);
                            return 1;
                        }
                        tftpc_error_handle(error_code);
                        fclose(fp);
                        return 0;
                    }
                    if(recv_ack.cmd == ACK_CODE)
                    {
                        /*ACK_CODE的block为0时，表示结束码*/
                        if(recv_ack.block == 0)
                        {
                            send_all_size = send_all_size + send_ret - PACKET_HEAD_SIZE;
                            true_ack = 1;

                            /*进行size校验*/
                            
                            if(send_all_size == buflen)
                            {
                                if(1 == tftpc_size_check(g_udp_info,send_all_size))
                                {
                                    printf("size check successful,send size is %d,file is %s\n",
                                            send_all_size,filename);
                                    fclose(fp);
                                    return 1;
                                }
                                else
                                {
                                    printf("size check failed,send size is %d,file is %s\n",
                                            send_all_size,filename);
                                    fclose(fp);
                                    return 0;
                                }
                            }
                            break;
                        }
                        else if(recv_ack.block > 0)
                        {
                            true_ack = 0;
                            continue;
                        }
                    }
                }
                else
                {
                    /*如果接收ACK超时，重新发送数据*/
                    true_ack = 0;
                    printf("recv finally ack timeout!\n");
                    break;
                }
                if(true_ack == 0)
                {
                    break;
                }
            }
            
            if(true_ack == 0)
            {
                printf("lost finally package %d\n",block);
                continue;
            } 
        }        
    }
    fclose(fp);
    return 1;
}

int tftpc_send_file_md_str(char *filename,char *md5)
{
    struct sockaddr_in sender;
    int send_ret = 0;
    socklen_t addr_len = sizeof(struct sockaddr);
    TFTP_PACKET send_packet;
    TFTP_OPT_PACK opt_packet;
    TFTP_PACKET recv_packet;
    unsigned int resend_count = 0;
    char md5_filename[256];
    memset(md5_filename,0,256);
    strcpy(md5_filename,filename);
    strcat(md5_filename,".md5");
    memset(&opt_packet,0,sizeof(TFTP_OPT_PACK));
    memset(&send_packet,0,sizeof(TFTP_PACKET));
    memset(&recv_packet,0,sizeof(TFTP_PACKET));
    memset(&sender,0,sizeof(struct sockaddr_in));

    opt_packet.cmd = MD5_CODE;
    sprintf(opt_packet.opt,"%s%c%s",md5_filename,0,md5);
    
    /*发送写请求*/
    
AGAIN:

    resend_count++;
    if((resend_count - 1) > MAX_RESEND_TIME)
    {
        printf("send MD5 request more than %d times,file %s\n",MAX_RESEND_TIME,filename);
        return 0;
    }
    send_ret = sendto(g_udp_info->sockfd,&opt_packet,sizeof(TFTP_OPT_PACK),0, \
            (struct sockaddr *)&g_udp_info->client,sizeof(struct sockaddr_in));
    
    /*发送失败，重发*/
    if(send_ret == -1)
    {
        printf("send request failed!with error %s\n",strerror(errno));
        return 0;
    }
    TFTP_ACK recv_ack;
    memset(&recv_ack,0,sizeof(TFTP_ACK));

    memset(&send_packet,0,sizeof(send_packet));

    
    /*接收服务端回应*/
    if(1 ==  tftp_timeout_check(g_udp_info->sockfd,TFTP_TIMEOUT))
    {
        if(-1 == recvfrom(g_udp_info->sockfd,&recv_ack,sizeof(TFTP_ACK),0, \
                (struct sockaddr *)&sender,&addr_len))
        {
            /*接收回应失败，重新发请求*/
            printf("recvfrom failed!with error:%s\n",strerror(errno));
            goto AGAIN;
        }
        
        /*收到回应*/
        if(recv_ack.cmd == CONF_CODE && recv_ack.block == 0)
        {
            
            printf("recv confirm ACK!file is %s\n",filename);
            return 1;
        }
        
        /*收到错误*/
        else if(recv_ack.cmd == ERROR_CODE)
        {
            printf("error_code %d,file is %s\n",recv_ack.block,filename);
            tftpc_error_handle(recv_ack.block);
            goto AGAIN;
        }
        
        /*无效ACK,重发*/
        else
        {
            printf("recv ack invailed %d!file is %s\n",recv_ack.cmd,filename);
            goto AGAIN;
        }
    }
    
    /*接受超时，重发请求*/
    else
    {
        printf("time out,resend MD5 request,file is %s\n",filename);
        goto AGAIN;
    }
   
    return 0;
}

/*****************************************************************************/
/** 
* \author      zhongfujin
* \date        2018/11/8
* \brief       udp文件传输下载文件
* \return      无
* \ingroup     无
* \remarks     无
******************************************************************************/
int tftpc_get_file(char *filename)
{
	struct sockaddr_in recver;
    socklen_t addr_len = sizeof(struct sockaddr);
    TFTP_PACKET send_packet;
    TFTP_OPT_PACK opt_packet;
    TFTP_PACKET recv_packet;
    memset(&opt_packet,0,sizeof(TFTP_OPT_PACK));
    memset(&send_packet,0,sizeof(TFTP_PACKET));
    memset(&recv_packet,0,sizeof(TFTP_PACKET));
	 memset(&recver,0,sizeof(struct sockaddr_in));


    opt_packet.cmd = RRQ_CODE;
    sprintf(opt_packet.opt,"%s%c%s%c",filename,0,"octet",0);
    unsigned int ret = 1;
AGAIN:
    
    /*发送读请求*/
    ret = sendto(g_udp_info->sockfd,&opt_packet,sizeof(TFTP_OPT_PACK),0, \
        (struct sockaddr *)&g_udp_info->client,sizeof(struct sockaddr_in));
    if(ret <= 0)
    {
        printf("send request failed!\n");
        goto AGAIN;
    }
    
    TFTP_ACK recv_ack;
    memset(&recv_ack,0,sizeof(TFTP_ACK));
    
        /*接收服务端回应*/
    if(1 ==  tftp_timeout_check(g_udp_info->sockfd,TFTP_TIMEOUT))
    {
        if(-1 == recvfrom(g_udp_info->sockfd,&recv_ack,sizeof(TFTP_ACK),0, \
            (struct sockaddr *)&g_udp_info->client,&addr_len))
        {
            /*接收回应失败，重新发请求*/
            printf("recvfrom failed!with error:%s\n",strerror(errno));
            goto AGAIN;
        }
        
        /*收到回应*/
        if(recv_ack.cmd == CONF_CODE && recv_ack.block == 0)
        {
            printf("recv confirm ACK!file is %s\n",filename);
        }
        
        /*无效ACK,重发*/
        else if(recv_ack.cmd == DATA_CODE)
        {   
            goto AGAIN;
        }
        else if(recv_ack.cmd == ERROR_CODE)
		{
			tftpc_error_handle(recv_ack.block);
			return 0;
		}
		else
        {
            printf("recv ack invailed %d!file is %s\n",recv_ack.cmd,filename);
            goto AGAIN;
        }
    }
    
    /*接受超时，重发请求*/
    else
    {
        printf("time out,resend WRQ request,file is %s\n",filename);
        goto AGAIN;
    }
   
    int block = 1;

    int recv_ret = 0;
    int write_ret = 0;
    FILE *fp = NULL;
    fp = fopen(filename,"ab+");
    if(fp == NULL)
    {
        printf("file open failed,haha!\n");
        return -1;
    }
    
    while(1)
    {
        recv_ret = 0;
        memset(&recv_packet,0,sizeof(TFTP_PACKET));
        if(1 == tftp_timeout_check(g_udp_info->sockfd,TFTP_TIMEOUT))
        {
            while(1)
            {   
                /*客户端接收来自服务端的数据报文*/
                recv_ret = recvfrom(g_udp_info->sockfd,&recv_packet,sizeof(TFTP_PACKET),0,
                    (struct sockaddr *)&g_udp_info->client,&addr_len);
                
                /* <0表示recvfrom函数出错*/
                if(recv_ret < PACKET_HEAD_SIZE)
                {
                    printf("recv error!\n");
                    return -1;
                }
                else if(recv_ret > PACKET_HEAD_SIZE)
                {
                    if(recv_packet.block < block)
                    {
                        printf("recv repeat block %d!\n",recv_packet.block);
                        continue;
                    }
                    break;
                }
                else if(recv_ret == PACKET_HEAD_SIZE)
                {
                    if(recv_packet.cmd == DATA_CODE)
                    {
                        if(recv_packet.block == 0)
                        {
                            printf("recv a last empty block!all block is %d,file is %s\n",block,filename);
                            
                        }
                        break;
                    }
                    if(recv_packet.cmd == ERROR_CODE)
                    {
                        if(recv_packet.block == ILLEGAL_OPER)
                        {
                            printf("recv file %s error!\n",filename);
                            return 0;
                        }
                        break;
                    }
                }
            }
        }
        
        /*超时通知重传*/
        if(recv_ret == 0)
        {
            printf("timeout block %d lost!\n",block);
            continue;
        }
        
        /*接收数据出错，通知重传*/
        if(recv_ret < PACKET_HEAD_SIZE)
        {
            printf("bad size!size is %d\n",recv_ret);
            continue;
        }
        printf("recv %d block,size %d\n",recv_packet.block,recv_ret);
        

        /*客户端写文件*/
        if(recv_ret > PACKET_HEAD_SIZE)
        {
            fseek(fp,(recv_packet.block - 1) * MAX_DATA_SIZE,SEEK_SET);
            write_ret = fwrite(&recv_packet.data,1,recv_ret - 4,fp);
            printf("write size %d\n",write_ret);
            if(write_ret < 0)
            {
                /*磁盘空间满的出错处理*/
                if(errno == ENOSPC)
                {
                    printf("ERROR: No space left!\n");
                    remove(filename);
                    return 0;
                }
            }
            fflush(fp);
            
            /*最后一个报文的数据小于MAX_DATA_SIZE，表示写文件结束*/
            if(write_ret < MAX_DATA_SIZE)
            {
                printf("recv end!\n");
                tftp_client_send_ack(g_udp_info,ACK_CODE,0);
                break;
            }
            block++;
        }
        
        /*若等于4，表示接收到的包为数据空包，传输结束*/
        else if(recv_ret == PACKET_HEAD_SIZE)
        {
            printf("recv end!\n");
            printf("recv end,all block is %d!file is %s\n",recv_packet.block,filename);
            tftp_client_send_ack(g_udp_info,ACK_CODE,0);
            break;
        }

        block = recv_packet.block;
        if(-1 == tftp_client_send_ack(g_udp_info,ACK_CODE,recv_packet.block))
        {
            printf("send ack failed!\n");
            continue;
        }  
    }
    fclose(fp);
    return 1;
}

/*****************************************************************************/
/** 
* \author      zhongfujin
* \date        2018/11/8
* \brief       udp文件传输的size校验
* \return      无
* \ingroup     无
* \remarks     无
******************************************************************************/
int tftpc_size_check(TFTP_UDP_INFO *udp_info,unsigned int len)
{
    TFTP_ACK recv_ack;
    socklen_t addr_len = sizeof(struct sockaddr);
    unsigned int resend_time = 0;
    memset(&recv_ack,0,sizeof(TFTP_ACK));
AGAIN:
    if(resend_time > MAX_RESEND_TIME)
    {
        printf("timeout,return !\n");
        return 0;
    }
    
    if(0 == tftp_client_send_ack(udp_info,SIZE_CODE,len))
    {
        printf("size check ack send failed!\n");
        resend_time++;
        goto AGAIN;
    }

    if(1 == tftp_timeout_check(udp_info->sockfd,TFTP_TIMEOUT))
    {
        int size = recvfrom(udp_info->sockfd,&recv_ack,sizeof(TFTP_ACK),0, \
                        (struct sockaddr *)&udp_info->client,&addr_len);
        if(size > 0)
        {
            if(recv_ack.cmd == SIZE_CODE)
            {
                if(TFTP_SIZEOK == recv_ack.block)
                {
                    return 1;
                }
                else if(TFTP_SIZEBAD == recv_ack.block)
                {
                    return 0;
                } 
            }
            else if(recv_ack.cmd == ERROR_CODE)
            {
                /*收到此种错误，表示服务端已经写结束，退出*/
                if(recv_ack.block == ILLEGAL_OPER)
                {
                    printf("server write over!\n");
                    return 0;
                }
                else
                {
                    tftpc_error_handle(recv_ack.block);
                }
               
            }
            else
            {
                 printf("invailed opt,cmd is %d\n",recv_ack.cmd);
                 return 0;
            }
            
        }
    }
    else
    {
        if(resend_time < MAX_RESEND_TIME)
        {
            goto AGAIN;
        }
        else
        {
            return 0;
        }
    }
    return 0;
}

/*****************************************************************************/
/** 
* \author      zhongfujin
* \date        2018/11/8
* \brief       udp文件传输发出错打印
* \return      无
* \ingroup     无
* \remarks     无
******************************************************************************/
int tftpc_error_handle(unsigned int error_code)
{
    switch(error_code)
    {
        case FILE_NOT_FOUND:
            printf("File not found.\n");
            break;
        case ACCESS_VIOLATION:
            printf("Access violation.\n");
            break;
        case DISK_FULL:
            printf("Disk full or allocation exceeded.\n");
            break;
        case ILLEGAL_OPER:
            printf("Illegal HAFS operation.\n");
            break;
        case UNKNOW_TRANS_ID:
            printf("Unknown transfer ID.\n");
            break;
        case FILE_ALREADY_EXIST:
            printf("File already exists.\n");
            break;
        case NO_SUCH_USER:
            printf("No such user.\n");
            break;
        default:
            printf("Invailed error code!\n");
    }
    return 1;
}

void tftpc_select_opt(char *opt,char *filename)
{
	char *opt_type = strtok(opt," ");
	char *name = strtok(NULL," ");
	printf("%s\n",name);
	if(name == NULL)
	{
		cmd_type = INVAILED;
		return;
	}
	if(strncmp(opt_type,"get",3) == 0)
	{
		cmd_type = GET_FILE;
	}
	else if(strncmp(opt_type,"put",3) == 0)
	{
		cmd_type = PUT_FILE;
		
	}
	else
	{
		cmd_type = INVAILED;
		
	}
	strncpy(filename,name,strlen(name));
}



