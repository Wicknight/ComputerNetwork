//#include "stdafx.h"
#include <stdio.h>
#include <Windows.h>
#include <process.h>
#include <string.h>
//编译器预处理指令，表示链接Ws2_32.lib库
// Codeblocks 使用的是 MingGW 来编译，MingGW不支持#pragma comment(lib,"Ws2_32.lib") 的写法
// 该命令是静态链接 Ws2_32.lib 库， 可以在设置里，加上 -lws2_32 或 -lwsock32
//#pragma comment(lib,"Ws2_32.lib")

#define MAXSIZE 65507 //发送数据报文的最大长度
#define HTTP_PORT 80 //http 服务器端口
#define MAXFILENAME 1024

#define shield_web "http://www.qq.com/"          //被屏蔽的网站
#define fishing_source "http://today.hit.edu.cn/"    //钓鱼的源网址
#define fishing_target "http://jwes.hit.edu.cn/"    //钓鱼的目的网址
#define fishing_host "jwes.hit.edu.cn"            //钓鱼目的地址的主机名

//Http 重要头部数据
struct HttpHeader{
    char method[4]; // POST 或者 GET，注意有些为 CONNECT，本实验暂 不考虑
    char url[1024]; // 请求的 url
    char host[1024]; // 目标主机
    char cookie[1024 * 10]; //cookie
    HttpHeader(){
        ZeroMemory(this,sizeof(HttpHeader));//微软的一个宏，用来将一段内存置0
    }
};

//socket初始化
BOOL InitSocket();
//提取http报文头部信息
void ParseHttpHead(char *buffer,HttpHeader * httpHeader);
//和服务器进行连接
BOOL ConnectToServer(SOCKET *serverSocket,char *host);

//线程执行函数
//利用__stdcall这种C++的标准调用方式
//参数从右向左依次压入堆栈.由被调用函数自己来恢复堆栈，称为自动清栈。
unsigned int __stdcall ProxyThread(LPVOID lpParameter);
//获取报文中的Date信息
BOOL getDate(char *buffer, char *Date);
//构造新http报文(加入if-modified-since字段)
void modifyHeader(char *buffer, char *value);
//根据url生成缓存文件名
void urlToFile(char *url, char *filename);
//缓存网页
void save_cache(char *buffer, char *url);
//判断服务器在缓存后是否进行过更新
void getModified(char *buffer, char *filename);

//代理相关参数
SOCKET ProxyServer;
sockaddr_in ProxyServerAddr;
const int ProxyPort = 10240;

//缓存相关参数
boolean cache_flag = FALSE;
boolean cache_change = TRUE;


struct ProxyParam{
    //clientSocket是代理服务器中用于与client连接的部分，对于client来说相当于模拟了一个服务器
    SOCKET clientSocket;
    //serverSocket是代理服务器中用于与server连接的部分，对于server来说是client，即代替真正的client来访问server
    SOCKET serverSocket;
};

int main(int argc, char* argv[]) {
    printf("代理服务器正在启动\n");
    printf("初始化...\n");
    if(!InitSocket()){
        printf("socket 初始化失败\n");
        return -1;
    }
    printf("代理服务器正在运行，监听端口 %d\n",ProxyPort);
    SOCKET acceptSocket = INVALID_SOCKET;
    ProxyParam *lpProxyParam;
    HANDLE hThread;
    //代理服务器不断监听
    while(true){
        //经过Init，ProxyServer已经处于监听(listen)状态
		//acceptSocket为新创建的套接字
        acceptSocket = accept(ProxyServer,NULL,NULL);
        lpProxyParam = new ProxyParam;
        if(lpProxyParam == NULL){
            continue;
        }
        lpProxyParam->clientSocket = acceptSocket;
        //创建线程执行ProxyThread函数
        hThread = (HANDLE)_beginthreadex(NULL, 0, &ProxyThread,(LPVOID)lpProxyParam, 0, 0);
        //线程执行完毕，关闭内核对象
        CloseHandle(hThread);
        Sleep(200);
    }
    closesocket(ProxyServer);
    WSACleanup();
    return 0;
}

//************************************
// Method:    InitSocket
// FullName:  InitSocket
// Access:    public
// Returns:   BOOL
// Qualifier: 初始化套接字
//************************************
BOOL InitSocket(){
    //加载套接字库（必须）
    WORD wVersionRequested;
    WSADATA wsaData;        //WSADATA结构体中主要包含了系统所支持的Winsock版本信息
    //套接字加载时错误提示
    int err;
    //版本 2.2
    wVersionRequested = MAKEWORD(2, 2);
    //加载 dll 文件 Scoket 库
    err = WSAStartup(wVersionRequested, &wsaData);
    if(err != 0){
        //找不到 winsock.dll
        printf("加载 winsock 失败，错误代码为: %d\n", WSAGetLastError());
        return FALSE;
    }
    //LOBYTE()得到一个16bit数最低（最右边）那个字节
    //HIBYTE()得到一个16bit数最高（最左边）那个字节
    //判断打开的是否是2.2版本
    if(LOBYTE(wsaData.wVersion) != 2 || HIBYTE(wsaData.wVersion) !=2)   {
        printf("不能找到正确的 winsock 版本\n");
        WSACleanup();
        return FALSE;
    }
    //AF_INET,PF_INET	IPv4 Internet协议
    //SOCK_STREAM	Tcp连接，提供序列化的、可靠的、双向连接的字节流。支持带外数据传输
    ProxyServer = socket(AF_INET, SOCK_STREAM, 0);
    if(INVALID_SOCKET == ProxyServer){
        printf("创建套接字失败，错误代码为：%d\n",WSAGetLastError());
        return FALSE;
    }
    //协议簇
    ProxyServerAddr.sin_family = AF_INET;
    //绑定端口
    ProxyServerAddr.sin_port = htons(ProxyPort);      //将整型变量从主机字节顺序转变成网络字节顺序

    //屏蔽用户
    //ProxyServerAddr.sin_addr.S_un.S_addr = INADDR_ANY;
    ProxyServerAddr.sin_addr.S_un.S_addr = inet_addr("127.0.0.1");//仅本机用户可访问服务器
    //ProxyServerAddr.sin_addr.S_un.S_addr = inet_addr("127.0.0.2");  //屏蔽用户
    if(bind(ProxyServer,(SOCKADDR*)&ProxyServerAddr,sizeof(SOCKADDR)) == SOCKET_ERROR){
        printf("绑定套接字失败\n");
        return FALSE;
    }
    if(listen(ProxyServer, SOMAXCONN) == SOCKET_ERROR){
        printf("监听端口%d 失败",ProxyPort);
        return FALSE;
    }
    return TRUE;
}
//************************************
// Method:    ProxyThread
// FullName:  ProxyThread
// Access:    public
// Returns:   unsigned int __stdcall
// Qualifier: 线程执行函数
// Parameter: LPVOID lpParameter
//************************************
unsigned int __stdcall ProxyThread(LPVOID lpParameter){
    //每个对象的缓存对应于一个URL名的文件(如果有缓存)
	//filecache用来存对应内容
    char Buffer[MAXSIZE], filecache[MAXSIZE];
    char *CacheBuffer;
    HttpHeader* httpHeader = new HttpHeader();
    ZeroMemory(Buffer,MAXSIZE);

    int recvSize;
    int ret;
    //接收窗口大小
    recvSize = recv(((ProxyParam *)lpParameter)->clientSocket,Buffer,MAXSIZE,0);

    //Buffer之后用来存储其他报文信息，故用CacheBuffer缓存请求报文
    CacheBuffer = new char[recvSize + 1];
    ZeroMemory(CacheBuffer, recvSize + 1);
    memcpy(CacheBuffer, Buffer, recvSize);

    //提取头部信息
    ParseHttpHead(CacheBuffer, httpHeader);

    //异常情况
	if (recvSize <= 0) {
		goto error;
	}

    //网站过滤：屏蔽网站
    if (strcmp (httpHeader->url, shield_web) == 0) {;
        printf("屏蔽网站：%s\n\n",shield_web);
        goto error;
    }
    //网站引导：钓鱼
	if (strstr(httpHeader->url, fishing_source) != NULL) {
		printf("       源网址：%s\n", fishing_source);
        printf("转到目的网址 ：%s\n\n",fishing_target);
		memcpy(httpHeader->host, fishing_host, strlen(fishing_host) + 1);
        memcpy(httpHeader->url, fishing_target, strlen(fishing_target));
	}

    //寻找缓存
	//创建缓存文件时限定文件名大小不超过1024字节
	char filename[MAXFILENAME];
	ZeroMemory(filename, MAXFILENAME);
	urlToFile(httpHeader->url, filename);

	//用于存放响应报文中的时间信息
	char date_info[30];
	ZeroMemory(date_info, 30);
	ZeroMemory(filecache, MAXSIZE);
	FILE *in;
	if ((in = fopen(filename, "rb")) != NULL) {
        //找到了文件名相同的缓存文件
		printf("\n代理服务器存在缓存文件！\n");
        //filecache读取文件里的缓存内容
		fread(filecache, sizeof(char), MAXSIZE, in);
		fclose(in);
		//从缓存中取出时间信息,存储在date_info中
		getDate(filecache, date_info);
		printf("date_info:%s\n", date_info);

		//修改http报文，头部中增加If-Modified-Since字段
		modifyHeader(Buffer, date_info);

		//标识存在缓存
		cache_flag = TRUE;
	}

    delete CacheBuffer;

    if(!ConnectToServer(&((ProxyParam *)lpParameter)->serverSocket,httpHeader->host)) {
        printf("连接目标服务器失败！！！\n");
        goto error;
    }
    printf("代理连接主机 %s 成功\n",httpHeader->host);
    //将客户端发送的 HTTP 数据报文直接转发给目标服务器
    ret = send(((ProxyParam *)lpParameter)->serverSocket,Buffer,strlen(Buffer) + 1,0);
    //等待目标服务器返回数据
    recvSize = recv(((ProxyParam *)lpParameter)->serverSocket,Buffer,MAXSIZE,0);
    if(recvSize <= 0){
        printf("返回目标服务器的数据失败！！！\n");
        goto error;
    }
	//如果本地有缓存，判断内容是否更新过
	if (cache_flag== TRUE) {
		getModified(Buffer, filename);
	}
	//内容更新过或是没有缓存，则需要更新缓存
	if (cache_change == TRUE) {
		save_cache(Buffer, httpHeader->url);
	}

    //将目标服务器返回的数据直接转发给客户端
    ret = send(((ProxyParam *)lpParameter)->clientSocket,Buffer,sizeof(Buffer),0);

//错误处理
error:
    printf("关闭套接字\n");
    delete Buffer;
    delete filecache;
    delete filename;
    Sleep(200);
    closesocket(((ProxyParam*)lpParameter)->clientSocket);
    closesocket(((ProxyParam*)lpParameter)->serverSocket);
    delete  lpParameter;
    _endthreadex(0);
    return 0;
}
//************************************
// Method:    ParseHttpHead
// FullName:  ParseHttpHead
// Access:    public
// Returns:   void
// Qualifier: 解析 TCP 报文中的 HTTP 头部
// Parameter: char * buffer
// Parameter: HttpHeader * httpHeader
//************************************
void ParseHttpHead(char *buffer,HttpHeader * httpHeader){
    char *p;
    const char * delim = "\r\n";
    //利用strtok_s函数切分,提取第一行，剩余部分保存在p中
    p = strtok(buffer,delim);
    //提取第一行
    //printf("%s\n",p);
    if(p[0] == 'G'){
        //GET 方式
        memcpy(httpHeader->method,"GET",3);
        //提取目标URL
		//p[0-2]:GET,p[3]:" "
		//请求方法+协议版本，长度为13
        memcpy(httpHeader->url,&p[4],strlen(p) -13);
    }
    else if(p[0] == 'P'){
        //POST 方式
        memcpy(httpHeader->method,"POST",4);
        memcpy(httpHeader->url,&p[5],strlen(p) - 14); //'Post' 和 'HTTP/1.1' 各占 4 和 8 个，再加上俩空格，一共14个
    }
    else if (p[0] == 'C') {
        memcpy(httpHeader->method, "CONNECT", 7);
        memcpy(httpHeader->url, &p[8], strlen(p) - 17);
    }

    //打印提取出的URL
    printf("访问URL： %s\n",httpHeader->url);
    //对剩余部分继续处理
    p = strtok(NULL,delim);
    while(p){
        switch(p[0]){
            case 'H'://Host
                //p[0-5]:"Host: "
                memcpy(httpHeader->host,&p[6],strlen(p) - 6);
                break;
            case 'C'://Cookie
                if(strlen(p) > 8){
                    char header[8];
                    ZeroMemory(header,sizeof(header));
                    memcpy(header,p,6);
                    if(!strcmp(header,"Cookie")){
                        //p[0-7]:"Cookie: "
                        memcpy(httpHeader->cookie,&p[8],strlen(p) -8);
                    }
                }
                break;
            default:
                break;
        }
        p = strtok(NULL,delim);
    }
}
//************************************
// Method:    ConnectToServer
// FullName:  ConnectToServer
// Access:    public
// Returns:   BOOL
// Qualifier: 根据主机创建目标服务器套接字，并连接
// Parameter: SOCKET * serverSocket
// Parameter: char * host
//************************************
BOOL ConnectToServer(SOCKET *serverSocket,char *host){
    sockaddr_in serverAddr;
    //TCP/IP-IPv4
    serverAddr.sin_family = AF_INET;
    //Http端口:80
    serverAddr.sin_port = htons(HTTP_PORT);

    //保存主机名以及地址信息等
    HOSTENT *hostent = gethostbyname(host);
    if(!hostent){
        return FALSE;
    }
    //地址
    in_addr Inaddr = *( (in_addr*) *hostent->h_addr_list);
    //将地址转为点分十进制，再转为无符号整数
    serverAddr.sin_addr.s_addr = inet_addr(inet_ntoa(Inaddr));
    //创建代理服务器中用于访问真正服务器的client流套接字
    *serverSocket = socket(AF_INET,SOCK_STREAM,0);
    //创建失败
    if(*serverSocket == INVALID_SOCKET){
        return FALSE;
    }
    //与真正服务器建立TCP连接
    if(connect(*serverSocket,(SOCKADDR *)&serverAddr,sizeof(serverAddr)) == SOCKET_ERROR){
        closesocket(*serverSocket);
        return FALSE;
    }
    return TRUE;
}

//************************************
// Method: getDate
// FullName: getDate
// Access: public
// Returns: BOOL
// Qualifier: 提取报文中Date字段的信息，存入Date变量
// Parameter: char * buffer
// Parameter: char * Date
//************************************
BOOL getDate(char *buffer , char *Date) {
	char *p, temp[5];
	//寻找Buffer中Date字段的信息,存入Date
	char field[]="Date";
	const char *delim = "\r\n";
	ZeroMemory(temp, 5);
	p = strtok(buffer, delim);
	int len = strlen(field) + 2;

	//循环直到找到对应的域
	while (p) {
		if (strstr(p, field) != NULL) {
			memcpy(Date, &p[len], strlen(p) - len);
			return TRUE;
		}
		p = strtok(NULL, delim);
	}
	return FALSE;
}

//************************************
// Method: save_cache
// FullName: save_cache
// Access: public
// Returns: void
// Qualifier: 将buffer中的内容存储到url对应的缓存文件中
// Parameter: char * buffer
// Parameter: char * url
//************************************
void save_cache(char *buffer, char *url) {
	char *p,num[10], tempBuffer[MAXSIZE + 1];
	const char * delim = "\r\n";

	//用于存放状态码
	ZeroMemory(num, 10);

    //将参数中的缓存buffer中的内容保存一个副本到tempBuffer中，避免对缓存内容更改
	ZeroMemory(tempBuffer, MAXSIZE + 1);
	memcpy(tempBuffer, buffer, strlen(buffer));
	p = strtok(tempBuffer, delim);//提取第一行

	//p[0-8]:"HTTP/1.1 "
	memcpy(num, &p[9], 3);
	if (strcmp(num, "200") == 0) {  //状态码是200(OK)时缓存
		char filename[MAXFILENAME] = { 0 };
		//从url中提取文件名
		urlToFile(url, filename);
		printf("filename : %s\n", filename);
		FILE *out;
		out = fopen(filename, "w");
		//缓存
		fwrite(buffer, sizeof(char), strlen(buffer), out);
		fclose(out);
		printf("\n=====================================\n\n");
		printf("\n网页已经缓存\n");
	}
}

//************************************
// Method: getModified
// FullName: getModified
// Access: public
// Returns: BOOL
// Qualifier: 根据响应报文中的状态码判断缓存内容是否是最新版本
// Parameter: char * buffer
// Parameter: char * filename
//************************************
void getModified(char *buffer, char *filename) {
	char *p, num[10], tempBuffer[MAXSIZE + 1];
	const char * delim = "\r\n";
	ZeroMemory(num, 10);
	ZeroMemory(tempBuffer, MAXSIZE + 1);
	memcpy(tempBuffer, buffer, strlen(buffer));
	p = strtok(tempBuffer, delim);//提取第一行
	memcpy(num, &p[9], 3);
	if (strcmp(num, "304") == 0) {  //主机返回的报文中的状态码为304时返回已缓存的内容
        printf("\n=====================================\n\n");
		printf("获取本地缓存\n");//返回已缓存的内容
		ZeroMemory(buffer, strlen(buffer));
		FILE *in = NULL;
		if ((in = fopen(filename, "r")) != NULL) {
			fread(buffer, sizeof(char), MAXSIZE, in);
			fclose(in);
		}
		cache_change = FALSE;//无需更新
	}
}

//************************************
// Method: modifyHeader
// FullName: modifyHeader
// Access: public
// Returns: void
// Qualifier: 改造http请求部，加入if-modified-since字段
// Parameter: char * buffer
// Parameter: char * value
//************************************
void modifyHeader(char *buffer, char *value) {
	const char *field = "Host";
	const char *newfield = "If-Modified-Since: ";

	char temp[MAXSIZE];
	ZeroMemory(temp, MAXSIZE);
	char *pos = strstr(buffer, field);
    int i = 0;
	for (i = 0; i < strlen(pos); i++) {
		temp[i] = pos[i];
	}
	*pos = '\0';
	//插入if-modified
	while (*newfield != '\0') {
		*pos++ = *newfield++;
	}
	//余下的信息
	while (*value != '\0') {
		*pos++ = *value++;
	}
	*pos++ = '\r';
	*pos++ = '\n';
	for (i = 0; i < strlen(temp); i++) {
		*pos++ = temp[i];
	}
}

//************************************
// Method: urlTofile
// FullName: urlTofile
// Access: public
// Returns: void
// Qualifier: 根据url值创建对应名字的文件
// Parameter: char * url
// Parameter: char * filename
//************************************
void urlToFile(char *url, char *filename) {
    ZeroMemory(filename, MAXFILENAME);
	char* p = filename;
	int i = 0;

	//限制文件名的上限，避免发生内存泄漏
	while (*url != '\0' && i < 1024) {
		//需要注意？等也要去掉，windows不支持
		if (*url != '/' && *url != ':' && *url != '.' && *url != '?') {
			*p++ = *url;
		}
		i++;
		url++;
	}
}
