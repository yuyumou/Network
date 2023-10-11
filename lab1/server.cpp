#include <iostream>
#include <winsock2.h>
#include<time.h>
#include<cstring>
#include<windows.h>
#pragma comment(lib, "ws2_32.lib")
using namespace std;
SOCKET socket_serve, socket_client;
SOCKADDR_IN addr_serve, addr_client;
int size_addr = sizeof(SOCKADDR_IN);
const int buf_size = 2048;
char send_buf[buf_size];
char input_buf[buf_size];
DWORD WINAPI handlerRequest_recv(LPVOID lparam);
DWORD WINAPI handlerRequest_send(LPVOID lparam);

int main() {
    int FIRST_JUDGE;
    WSADATA wsaData;
    FIRST_JUDGE = WSAStartup(MAKEWORD(2, 2), &wsaData);

    //cout<< MAKEWORD(2, 2) << wsaData.wVersion << " " << wsaData.wHighVersion << endl;
    //指定版本wVersionRequested=MAKEWORD(a,b)。MAKEWORD()是一个宏把两个数字组合成一个WORD，无符号的短整型
    if (FIRST_JUDGE != 0) {
        //初始化失败
        return 0;
    }

    //创建socket。这里我们使用流式socket。
    socket_serve = socket(AF_INET, SOCK_STREAM, 0);
    //初始化服务器端地址
    addr_serve.sin_addr.s_addr = inet_addr("192.168.101.1");//把我们本机的地址转换成网络字节二进制值序
    addr_serve.sin_family = AF_INET;//使用ipv4
    addr_serve.sin_port = htons(8000);//转换函数，也是转换成网络字节序。


    if (bind(socket_serve, (SOCKADDR*)&addr_serve, sizeof(SOCKADDR)) == -1) {
        //绑定失败
        return 0;
    }
    //绑定
    bind(socket_serve, (SOCKADDR*)&addr_serve, sizeof(SOCKADDR));


    //监听
    listen(socket_serve, 5);
    HANDLE hThread[2];

    while (1) {
        socket_client = accept(socket_serve, (SOCKADDR*)&addr_client, &size_addr);

        //接受，返回的是一个socket
        if (socket_client != INVALID_SOCKET)//判断连接成功
        {
            cout << "--------------------这里是服务器端-----------------" << endl;
        }
        else {
            //连接失败
            return 0;
        }

        hThread[0] = CreateThread(NULL, NULL, handlerRequest_recv, LPVOID(socket_client), 0, NULL);
        hThread[1] = CreateThread(NULL, NULL, handlerRequest_send, LPVOID(socket_client), 0, NULL);
        CloseHandle(hThread[0]);
        CloseHandle(hThread[1]);
    }
    closesocket(socket_serve);
    WSACleanup();

}

DWORD WINAPI handlerRequest_recv(LPVOID lparam)
{
    char receive_buf_loop[buf_size] = {};
    //        int NetTimeout = 500; //超时时长
    //        setsockopt(socket_client, SOL_SOCKET,SO_RCVTIMEO,(char *)&NetTimeout,sizeof(int));
    recv(socket_client, receive_buf_loop, 2048, 0);
    //判断是否对方要退出
    if (!strcmp("quit", receive_buf_loop))
    {
        cout << "客户端选择了结束聊天" << endl;
        return 0;
    }
    else {
        time_t now_time = time(NULL);
        tm* t_tm = localtime(&now_time);
        cout << "客户端发的：" << receive_buf_loop << "  " << "自己收到时间: " << asctime(t_tm);
    }
}
DWORD WINAPI handlerRequest_send(LPVOID lparam) {
    cin.getline(input_buf, 2048, '\n');
    if (!strcmp("quit", input_buf))
    {
        //            strcpy(send_buf, '\0');
        send(socket_client, input_buf, 2048, 0);
        cout << "服务器端选择结束聊天" << endl;
        return 0;
    }
    else {
        strcpy(send_buf, input_buf);
        send(socket_client, send_buf, 2048, 0);
    }
    Sleep(30);
}
