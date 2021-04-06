/*
    * THIS FILE IS FOR IP TEST
    * IPV4 分组收发实验部分
    */
// system support
#include "sysInclude.h"
#include <stdio.h>
#include <malloc.h>
extern void ip_DiscardPkt(char *pBuffer, int type);

extern void ip_SendtoLower(char *pBuffer, int length);

extern void ip_SendtoUp(char *pBuffer, int length);

extern unsigned int getIpv4Address();

// implemented by students

int stud_ip_recv(char *pBuffer, unsigned short length)
{
    //pBuffer[0]为第一个字节，前4位为版本号，后4位为首部长度
    short version = pBuffer[0] >> 4;     //右移4位得到版本号(0000version)
    short head_length = pBuffer[0] & 0x0f;   //用0f=00001111相与来提取首部长度
    short ttl = (unsigned short)pBuffer[8];  //ttl为第9个字节
    //将2字节的checksum转为主机字节顺序
    short checksum = ntohs(*(unsigned short *)(pBuffer + 10));
    //将4字节的目的IP地址转为主机字节顺序
    int destination = ntohl(*(unsigned int *)(pBuffer + 16));

    //version!=4即不是IPv4版本
    if (version != 4)
    {
        ip_DiscardPkt(pBuffer, STUD_IP_TEST_VERSION_ERROR);
        return 1;
    }
    //首部长度应>=20字节
    if (head_length < 5)
    {
        ip_DiscardPkt(pBuffer, STUD_IP_TEST_HEADLEN_ERROR);
        return 1;
    }
    //ttl=0即应被丢弃
    if (ttl == 0)
    {
        ip_DiscardPkt(pBuffer, STUD_IP_TEST_TTL_ERROR);
        return 1;
    }
    //目的地址非本地IP且目的地址并不是广播地址
    if (destination!=getIpv4Address()&&destination != 0xffffffff)
    {
        ip_DiscardPkt(pBuffer, STUD_IP_TEST_DESTINATION_ERROR);
        return 1;
    }

    //检验校验和的值
    //checksum本身为16bits，但在计算过程中有可能会出现溢出
    //因此用32位的变量来保存
    unsigned int sum = 0;
    unsigned int temp = 0;
    int i;
    //head_length以4字节为单位
    for (i = 0; i < head_length*4; i+=2)
    {
        //应以16bits为单位进行加和
        //因此以char为单位提取时要将前8bits扩展为16bits的高8位
        temp += (unsigned char)pBuffer[i] << 8;
        //后8bits,作为16bits的低8位
        temp += (unsigned char)pBuffer[i+1];
        //16bits加和
        sum += temp;
        temp = 0;
    }
    //考虑加和过程中出现的溢出
    //l_word表示sum中的低16位
    unsigned short l_word = sum&0xffff;
    //h_word表示sum中的高16位，如果不为0，说明加和过程中出现溢出
    //根据计算原则，需要将溢出部分重新加和
    //而如果没溢出，那么值为0，加和也不影响结果
    unsigned short h_word = sum>>16;
    //最终计算结果
    unsigned int check=h_word+l_word;
    //计算仍然可能产生进位，不断加和直至不产生进位为止
    while(check>>16!=0){
        l_word=check&0xffff;
        h_word=check>>16;
        check=h_word+l_word;
    }
    //16bits 全1
    if (check!=0xffff)
    {
        ip_DiscardPkt(pBuffer, STUD_IP_TEST_CHECKSUM_ERROR);
        return 1;
    }
    //正确无误，交给系统后续处理
    ip_SendtoUp(pBuffer, length);
    return 0;
}

int stud_ip_Upsend(char *pBuffer, unsigned short len, unsigned int srcAddr,
                   unsigned int dstAddr, byte protocol, byte ttl)
{
    //默认head_length=5(20Bytes)
    short ip_len = len + 20;
    //申请内存空间，初始化全0
    char *buffer = (char *)malloc(ip_len * sizeof(char));
    memset(buffer, 0, ip_len);
    //version=4,head_length=5
    buffer[0] = 0x45;
    buffer[8] = ttl;
    buffer[9] = protocol;
    //转换为网络字节序
    unsigned short net_len = htons(ip_len);
    //总长度字段，第2与第3字节
    memcpy(buffer + 2, &net_len, 2);
    //源地址与目的地址转为网络字节序
    unsigned int src = htonl(srcAddr);
    unsigned int dst = htonl(dstAddr);
    memcpy(buffer + 12, &src, 4);
    memcpy(buffer + 16, &dst, 4);

    //计算首部校验和

    unsigned long sum = 0;
    unsigned long temp = 0;
    int i;
    //与接收方计算方法类似
    //只不过此时校验和字段仍是初始的0，以16bits的0加入计算
    for (i = 0; i < 20; i += 2)
    {
        temp += (unsigned char)buffer[i] << 8;
        temp += (unsigned char)buffer[i + 1];
        sum += temp;
        temp = 0;
    }
    unsigned short l_word = sum & 0xffff;
    unsigned short h_word = sum >> 16;
    //带进位的加和结果
    unsigned int sum_carry=l_word+h_word;
    //当存在进位时不断与进位加和
    while(sum_carry>>16!=0){
        l_word=sum_carry&0xffff;
        h_word=sum_carry>>16;
        sum_carry=h_word+l_word;
    }
    //无进位的最终结果
    unsigned short checksum = l_word + h_word;
    //取反
    checksum = ~checksum;
    unsigned short header_checksum = htons(checksum);

    //将校验和字段写入
    memcpy(buffer + 10, &header_checksum, 2);
    //将数据填入
    memcpy(buffer + 20, pBuffer, len);
    //传给下一层
    ip_SendtoLower(buffer, ip_len);
    return 0;
}
