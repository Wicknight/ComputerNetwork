/*
* THIS FILE IS FOR IP FORWARD TEST
* IPV4 分组收发实验部分
*/
#include "sysInclude.h"
#include <stdio.h>
#include <vector>
using std::vector;
// system support
extern void fwd_LocalRcv(char *pBuffer, int length);

extern void fwd_SendtoLower(char *pBuffer, int length, unsigned int nexthop);

extern void fwd_DiscardPkt(char *pBuffer, int type);

extern unsigned int getIpv4Address();

// implemented by students

vector<stud_route_msg> route;

void stud_Route_Init()
{
	route.clear();
	return;
}

//增加路由
void stud_route_add(stud_route_msg *proute)
{
    //将proute中的信息转移到temp中，然后将temp加入路由表中
	stud_route_msg temp;
	//提取实验指导中给出的路由项数据结构中的信息：目的地址，子网掩码的位数、下一跳地址
	unsigned int dest = ntohl(proute->dest);
	unsigned int masklen = ntohl(proute->masklen);
	unsigned int nexthop = ntohl(proute->nexthop);
	temp.dest = dest;
	temp.masklen = masklen;
	temp.nexthop = nexthop;
	//加入路由表中
	route.push_back(temp);
	return;
}

//在该函数之前已完成报文其他合法性的检查
int stud_fwd_deal(char *pBuffer, int length)
{
    //提取版本号，0号字节的高4位
	int version = pBuffer[0] >> 4;
	//提取首部长度，0号字节的低4位
	int head_length = pBuffer[0] & 0xf;
	//提取ttl，第8字节
	short ttl = (unsigned short)pBuffer[8];
	//提取首部校验和，共16bits，转为主机字节顺序
	short checksum = ntohs(*(unsigned short *)(pBuffer + 10));
	//提取目的地址，32bits，转为主机字节顺序
	int destination = ntohl(*(unsigned int *)(pBuffer + 16));

    //ttl<=0,应被丢弃
	if (ttl <= 0)
	{
		fwd_DiscardPkt(pBuffer, STUD_FORWARD_TEST_TTLERROR);
		return 1;
	}
	//判定是否为本机接收的分组
	if (destination == getIpv4Address())
	{
	    //本机接收，交付上层
		fwd_LocalRcv(pBuffer, length);
		return 0;
	}

	stud_route_msg *ans_route = NULL;
	int temp_dest = destination;
	int max_len=0;
	int flag=0;
	for (int i = 0; i < route.size(); i++)
	{
	    //(1 << 31) >> (route[i].masklen - 1)构造一个字节：其中前masklen位均为1，其余位均为0，即子网掩码
        int mask=(1 << 31) >> (route[i].masklen - 1);
        //得到目的ip对应的子网地址
        int dest_net=mask&temp_dest;
        //遍历所有表项，找到包含目的子网的最具体的子网，获取下一跳
	    if(route[i].masklen>max_len&&dest_net==route[i].dest){
            ans_route=&route[i];
            max_len=route[i].masklen;
        }
	}

	//路由表中未查询到路径信息
	if (!ans_route)
	{
		fwd_DiscardPkt(pBuffer, STUD_FORWARD_TEST_NOROUTE);
		return 1;
	}
	else
	{
		char *buffer = new char[length];
		memcpy(buffer, pBuffer, length);
		//转发，ttl-1
		buffer[8] = ttl - 1;
		memset(buffer + 10, 0, 2);
		unsigned long sum = 0;
		unsigned long temp = 0;

		//重新计算校验和
		for (int i = 0; i < head_length * 2; i++)
		{
			temp += (unsigned char)buffer[i * 2] << 8;
			temp += (unsigned char)buffer[i * 2 + 1];
			sum += temp;
			temp = 0;
		}
		unsigned short l_word = sum & 0xffff;
		unsigned short h_word = sum >> 16;
        //加和结果(可能仍然有进位)
        unsigned int check=h_word+l_word;
        //计算仍然可能产生进位，不断加和直至不产生进位为止
        while(check>>16!=0){
            l_word=check&0xffff;
            h_word=check>>16;
            check=h_word+l_word;
        }
        unsigned short checksum=h_word+l_word;
		checksum = ~checksum;
		//转为网络字节顺序
		unsigned short header_checksum = htons(checksum);
		memcpy(buffer + 10, &header_checksum, 2);
		//发送报文
		fwd_SendtoLower(buffer, length, ans_route->nexthop);
	}
	return 0;
}
