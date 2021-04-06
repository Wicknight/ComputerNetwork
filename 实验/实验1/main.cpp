//#include "stdafx.h"
#include <stdio.h>
#include <Windows.h>
#include <process.h>
#include <string.h>
//������Ԥ����ָ���ʾ����Ws2_32.lib��
// Codeblocks ʹ�õ��� MingGW �����룬MingGW��֧��#pragma comment(lib,"Ws2_32.lib") ��д��
// �������Ǿ�̬���� Ws2_32.lib �⣬ ��������������� -lws2_32 �� -lwsock32
//#pragma comment(lib,"Ws2_32.lib")

#define MAXSIZE 65507 //�������ݱ��ĵ���󳤶�
#define HTTP_PORT 80 //http �������˿�
#define MAXFILENAME 1024

#define shield_web "http://www.qq.com/"          //�����ε���վ
#define fishing_source "http://today.hit.edu.cn/"    //�����Դ��ַ
#define fishing_target "http://jwes.hit.edu.cn/"    //�����Ŀ����ַ
#define fishing_host "jwes.hit.edu.cn"            //����Ŀ�ĵ�ַ��������

//Http ��Ҫͷ������
struct HttpHeader{
    char method[4]; // POST ���� GET��ע����ЩΪ CONNECT����ʵ���� ������
    char url[1024]; // ����� url
    char host[1024]; // Ŀ������
    char cookie[1024 * 10]; //cookie
    HttpHeader(){
        ZeroMemory(this,sizeof(HttpHeader));//΢���һ���꣬������һ���ڴ���0
    }
};

//socket��ʼ��
BOOL InitSocket();
//��ȡhttp����ͷ����Ϣ
void ParseHttpHead(char *buffer,HttpHeader * httpHeader);
//�ͷ�������������
BOOL ConnectToServer(SOCKET *serverSocket,char *host);

//�߳�ִ�к���
//����__stdcall����C++�ı�׼���÷�ʽ
//����������������ѹ���ջ.�ɱ����ú����Լ����ָ���ջ����Ϊ�Զ���ջ��
unsigned int __stdcall ProxyThread(LPVOID lpParameter);
//��ȡ�����е�Date��Ϣ
BOOL getDate(char *buffer, char *Date);
//������http����(����if-modified-since�ֶ�)
void modifyHeader(char *buffer, char *value);
//����url���ɻ����ļ���
void urlToFile(char *url, char *filename);
//������ҳ
void save_cache(char *buffer, char *url);
//�жϷ������ڻ�����Ƿ���й�����
void getModified(char *buffer, char *filename);

//������ز���
SOCKET ProxyServer;
sockaddr_in ProxyServerAddr;
const int ProxyPort = 10240;

//������ز���
boolean cache_flag = FALSE;
boolean cache_change = TRUE;


struct ProxyParam{
    //clientSocket�Ǵ����������������client���ӵĲ��֣�����client��˵�൱��ģ����һ��������
    SOCKET clientSocket;
    //serverSocket�Ǵ����������������server���ӵĲ��֣�����server��˵��client��������������client������server
    SOCKET serverSocket;
};

int main(int argc, char* argv[]) {
    printf("�����������������\n");
    printf("��ʼ��...\n");
    if(!InitSocket()){
        printf("socket ��ʼ��ʧ��\n");
        return -1;
    }
    printf("����������������У������˿� %d\n",ProxyPort);
    SOCKET acceptSocket = INVALID_SOCKET;
    ProxyParam *lpProxyParam;
    HANDLE hThread;
    //������������ϼ���
    while(true){
        //����Init��ProxyServer�Ѿ����ڼ���(listen)״̬
		//acceptSocketΪ�´������׽���
        acceptSocket = accept(ProxyServer,NULL,NULL);
        lpProxyParam = new ProxyParam;
        if(lpProxyParam == NULL){
            continue;
        }
        lpProxyParam->clientSocket = acceptSocket;
        //�����߳�ִ��ProxyThread����
        hThread = (HANDLE)_beginthreadex(NULL, 0, &ProxyThread,(LPVOID)lpProxyParam, 0, 0);
        //�߳�ִ����ϣ��ر��ں˶���
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
// Qualifier: ��ʼ���׽���
//************************************
BOOL InitSocket(){
    //�����׽��ֿ⣨���룩
    WORD wVersionRequested;
    WSADATA wsaData;        //WSADATA�ṹ������Ҫ������ϵͳ��֧�ֵ�Winsock�汾��Ϣ
    //�׽��ּ���ʱ������ʾ
    int err;
    //�汾 2.2
    wVersionRequested = MAKEWORD(2, 2);
    //���� dll �ļ� Scoket ��
    err = WSAStartup(wVersionRequested, &wsaData);
    if(err != 0){
        //�Ҳ��� winsock.dll
        printf("���� winsock ʧ�ܣ��������Ϊ: %d\n", WSAGetLastError());
        return FALSE;
    }
    //LOBYTE()�õ�һ��16bit����ͣ����ұߣ��Ǹ��ֽ�
    //HIBYTE()�õ�һ��16bit����ߣ�����ߣ��Ǹ��ֽ�
    //�жϴ򿪵��Ƿ���2.2�汾
    if(LOBYTE(wsaData.wVersion) != 2 || HIBYTE(wsaData.wVersion) !=2)   {
        printf("�����ҵ���ȷ�� winsock �汾\n");
        WSACleanup();
        return FALSE;
    }
    //AF_INET,PF_INET	IPv4 InternetЭ��
    //SOCK_STREAM	Tcp���ӣ��ṩ���л��ġ��ɿ��ġ�˫�����ӵ��ֽ�����֧�ִ������ݴ���
    ProxyServer = socket(AF_INET, SOCK_STREAM, 0);
    if(INVALID_SOCKET == ProxyServer){
        printf("�����׽���ʧ�ܣ��������Ϊ��%d\n",WSAGetLastError());
        return FALSE;
    }
    //Э���
    ProxyServerAddr.sin_family = AF_INET;
    //�󶨶˿�
    ProxyServerAddr.sin_port = htons(ProxyPort);      //�����ͱ����������ֽ�˳��ת��������ֽ�˳��

    //�����û�
    //ProxyServerAddr.sin_addr.S_un.S_addr = INADDR_ANY;
    ProxyServerAddr.sin_addr.S_un.S_addr = inet_addr("127.0.0.1");//�������û��ɷ��ʷ�����
    //ProxyServerAddr.sin_addr.S_un.S_addr = inet_addr("127.0.0.2");  //�����û�
    if(bind(ProxyServer,(SOCKADDR*)&ProxyServerAddr,sizeof(SOCKADDR)) == SOCKET_ERROR){
        printf("���׽���ʧ��\n");
        return FALSE;
    }
    if(listen(ProxyServer, SOMAXCONN) == SOCKET_ERROR){
        printf("�����˿�%d ʧ��",ProxyPort);
        return FALSE;
    }
    return TRUE;
}
//************************************
// Method:    ProxyThread
// FullName:  ProxyThread
// Access:    public
// Returns:   unsigned int __stdcall
// Qualifier: �߳�ִ�к���
// Parameter: LPVOID lpParameter
//************************************
unsigned int __stdcall ProxyThread(LPVOID lpParameter){
    //ÿ������Ļ����Ӧ��һ��URL�����ļ�(����л���)
	//filecache�������Ӧ����
    char Buffer[MAXSIZE], filecache[MAXSIZE];
    char *CacheBuffer;
    HttpHeader* httpHeader = new HttpHeader();
    ZeroMemory(Buffer,MAXSIZE);

    int recvSize;
    int ret;
    //���մ��ڴ�С
    recvSize = recv(((ProxyParam *)lpParameter)->clientSocket,Buffer,MAXSIZE,0);

    //Buffer֮�������洢����������Ϣ������CacheBuffer����������
    CacheBuffer = new char[recvSize + 1];
    ZeroMemory(CacheBuffer, recvSize + 1);
    memcpy(CacheBuffer, Buffer, recvSize);

    //��ȡͷ����Ϣ
    ParseHttpHead(CacheBuffer, httpHeader);

    //�쳣���
	if (recvSize <= 0) {
		goto error;
	}

    //��վ���ˣ�������վ
    if (strcmp (httpHeader->url, shield_web) == 0) {;
        printf("������վ��%s\n\n",shield_web);
        goto error;
    }
    //��վ����������
	if (strstr(httpHeader->url, fishing_source) != NULL) {
		printf("       Դ��ַ��%s\n", fishing_source);
        printf("ת��Ŀ����ַ ��%s\n\n",fishing_target);
		memcpy(httpHeader->host, fishing_host, strlen(fishing_host) + 1);
        memcpy(httpHeader->url, fishing_target, strlen(fishing_target));
	}

    //Ѱ�һ���
	//���������ļ�ʱ�޶��ļ�����С������1024�ֽ�
	char filename[MAXFILENAME];
	ZeroMemory(filename, MAXFILENAME);
	urlToFile(httpHeader->url, filename);

	//���ڴ����Ӧ�����е�ʱ����Ϣ
	char date_info[30];
	ZeroMemory(date_info, 30);
	ZeroMemory(filecache, MAXSIZE);
	FILE *in;
	if ((in = fopen(filename, "rb")) != NULL) {
        //�ҵ����ļ�����ͬ�Ļ����ļ�
		printf("\n������������ڻ����ļ���\n");
        //filecache��ȡ�ļ���Ļ�������
		fread(filecache, sizeof(char), MAXSIZE, in);
		fclose(in);
		//�ӻ�����ȡ��ʱ����Ϣ,�洢��date_info��
		getDate(filecache, date_info);
		printf("date_info:%s\n", date_info);

		//�޸�http���ģ�ͷ��������If-Modified-Since�ֶ�
		modifyHeader(Buffer, date_info);

		//��ʶ���ڻ���
		cache_flag = TRUE;
	}

    delete CacheBuffer;

    if(!ConnectToServer(&((ProxyParam *)lpParameter)->serverSocket,httpHeader->host)) {
        printf("����Ŀ�������ʧ�ܣ�����\n");
        goto error;
    }
    printf("������������ %s �ɹ�\n",httpHeader->host);
    //���ͻ��˷��͵� HTTP ���ݱ���ֱ��ת����Ŀ�������
    ret = send(((ProxyParam *)lpParameter)->serverSocket,Buffer,strlen(Buffer) + 1,0);
    //�ȴ�Ŀ���������������
    recvSize = recv(((ProxyParam *)lpParameter)->serverSocket,Buffer,MAXSIZE,0);
    if(recvSize <= 0){
        printf("����Ŀ�������������ʧ�ܣ�����\n");
        goto error;
    }
	//��������л��棬�ж������Ƿ���¹�
	if (cache_flag== TRUE) {
		getModified(Buffer, filename);
	}
	//���ݸ��¹�����û�л��棬����Ҫ���»���
	if (cache_change == TRUE) {
		save_cache(Buffer, httpHeader->url);
	}

    //��Ŀ����������ص�����ֱ��ת�����ͻ���
    ret = send(((ProxyParam *)lpParameter)->clientSocket,Buffer,sizeof(Buffer),0);

//������
error:
    printf("�ر��׽���\n");
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
// Qualifier: ���� TCP �����е� HTTP ͷ��
// Parameter: char * buffer
// Parameter: HttpHeader * httpHeader
//************************************
void ParseHttpHead(char *buffer,HttpHeader * httpHeader){
    char *p;
    const char * delim = "\r\n";
    //����strtok_s�����з�,��ȡ��һ�У�ʣ�ಿ�ֱ�����p��
    p = strtok(buffer,delim);
    //��ȡ��һ��
    //printf("%s\n",p);
    if(p[0] == 'G'){
        //GET ��ʽ
        memcpy(httpHeader->method,"GET",3);
        //��ȡĿ��URL
		//p[0-2]:GET,p[3]:" "
		//���󷽷�+Э��汾������Ϊ13
        memcpy(httpHeader->url,&p[4],strlen(p) -13);
    }
    else if(p[0] == 'P'){
        //POST ��ʽ
        memcpy(httpHeader->method,"POST",4);
        memcpy(httpHeader->url,&p[5],strlen(p) - 14); //'Post' �� 'HTTP/1.1' ��ռ 4 �� 8 �����ټ������ո�һ��14��
    }
    else if (p[0] == 'C') {
        memcpy(httpHeader->method, "CONNECT", 7);
        memcpy(httpHeader->url, &p[8], strlen(p) - 17);
    }

    //��ӡ��ȡ����URL
    printf("����URL�� %s\n",httpHeader->url);
    //��ʣ�ಿ�ּ�������
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
// Qualifier: ������������Ŀ��������׽��֣�������
// Parameter: SOCKET * serverSocket
// Parameter: char * host
//************************************
BOOL ConnectToServer(SOCKET *serverSocket,char *host){
    sockaddr_in serverAddr;
    //TCP/IP-IPv4
    serverAddr.sin_family = AF_INET;
    //Http�˿�:80
    serverAddr.sin_port = htons(HTTP_PORT);

    //�����������Լ���ַ��Ϣ��
    HOSTENT *hostent = gethostbyname(host);
    if(!hostent){
        return FALSE;
    }
    //��ַ
    in_addr Inaddr = *( (in_addr*) *hostent->h_addr_list);
    //����ַתΪ���ʮ���ƣ���תΪ�޷�������
    serverAddr.sin_addr.s_addr = inet_addr(inet_ntoa(Inaddr));
    //������������������ڷ���������������client���׽���
    *serverSocket = socket(AF_INET,SOCK_STREAM,0);
    //����ʧ��
    if(*serverSocket == INVALID_SOCKET){
        return FALSE;
    }
    //����������������TCP����
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
// Qualifier: ��ȡ������Date�ֶε���Ϣ������Date����
// Parameter: char * buffer
// Parameter: char * Date
//************************************
BOOL getDate(char *buffer , char *Date) {
	char *p, temp[5];
	//Ѱ��Buffer��Date�ֶε���Ϣ,����Date
	char field[]="Date";
	const char *delim = "\r\n";
	ZeroMemory(temp, 5);
	p = strtok(buffer, delim);
	int len = strlen(field) + 2;

	//ѭ��ֱ���ҵ���Ӧ����
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
// Qualifier: ��buffer�е����ݴ洢��url��Ӧ�Ļ����ļ���
// Parameter: char * buffer
// Parameter: char * url
//************************************
void save_cache(char *buffer, char *url) {
	char *p,num[10], tempBuffer[MAXSIZE + 1];
	const char * delim = "\r\n";

	//���ڴ��״̬��
	ZeroMemory(num, 10);

    //�������еĻ���buffer�е����ݱ���һ��������tempBuffer�У�����Ի������ݸ���
	ZeroMemory(tempBuffer, MAXSIZE + 1);
	memcpy(tempBuffer, buffer, strlen(buffer));
	p = strtok(tempBuffer, delim);//��ȡ��һ��

	//p[0-8]:"HTTP/1.1 "
	memcpy(num, &p[9], 3);
	if (strcmp(num, "200") == 0) {  //״̬����200(OK)ʱ����
		char filename[MAXFILENAME] = { 0 };
		//��url����ȡ�ļ���
		urlToFile(url, filename);
		printf("filename : %s\n", filename);
		FILE *out;
		out = fopen(filename, "w");
		//����
		fwrite(buffer, sizeof(char), strlen(buffer), out);
		fclose(out);
		printf("\n=====================================\n\n");
		printf("\n��ҳ�Ѿ�����\n");
	}
}

//************************************
// Method: getModified
// FullName: getModified
// Access: public
// Returns: BOOL
// Qualifier: ������Ӧ�����е�״̬���жϻ��������Ƿ������°汾
// Parameter: char * buffer
// Parameter: char * filename
//************************************
void getModified(char *buffer, char *filename) {
	char *p, num[10], tempBuffer[MAXSIZE + 1];
	const char * delim = "\r\n";
	ZeroMemory(num, 10);
	ZeroMemory(tempBuffer, MAXSIZE + 1);
	memcpy(tempBuffer, buffer, strlen(buffer));
	p = strtok(tempBuffer, delim);//��ȡ��һ��
	memcpy(num, &p[9], 3);
	if (strcmp(num, "304") == 0) {  //�������صı����е�״̬��Ϊ304ʱ�����ѻ��������
        printf("\n=====================================\n\n");
		printf("��ȡ���ػ���\n");//�����ѻ��������
		ZeroMemory(buffer, strlen(buffer));
		FILE *in = NULL;
		if ((in = fopen(filename, "r")) != NULL) {
			fread(buffer, sizeof(char), MAXSIZE, in);
			fclose(in);
		}
		cache_change = FALSE;//�������
	}
}

//************************************
// Method: modifyHeader
// FullName: modifyHeader
// Access: public
// Returns: void
// Qualifier: ����http���󲿣�����if-modified-since�ֶ�
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
	//����if-modified
	while (*newfield != '\0') {
		*pos++ = *newfield++;
	}
	//���µ���Ϣ
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
// Qualifier: ����urlֵ������Ӧ���ֵ��ļ�
// Parameter: char * url
// Parameter: char * filename
//************************************
void urlToFile(char *url, char *filename) {
    ZeroMemory(filename, MAXFILENAME);
	char* p = filename;
	int i = 0;

	//�����ļ��������ޣ����ⷢ���ڴ�й©
	while (*url != '\0' && i < 1024) {
		//��Ҫע�⣿��ҲҪȥ����windows��֧��
		if (*url != '/' && *url != ':' && *url != '.' && *url != '?') {
			*p++ = *url;
		}
		i++;
		url++;
	}
}
