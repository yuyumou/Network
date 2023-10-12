#include <iostream>
#include <winsock2.h>
#include <thread>

using namespace std;

#pragma comment(lib, "ws2_32.lib")

void receiveMessages(SOCKET clientSocket) {
    char buffer[1024];
    int bytesRead;

    while (true) {
        bytesRead = recv(clientSocket, buffer, sizeof(buffer), 0);
        if (strcmp(buffer, "quit") == 0 || bytesRead <= 0) {
            cout << "Client disconnected." << endl;
            closesocket(clientSocket);
            WSACleanup();
            break;
            //exit(1);
        }
        buffer[bytesRead] = '\0';
        cout << "客户端: " << buffer << endl;
    }
}

int main() {
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        cout << "初始化失败" << endl;
        return 1;
    }

    // 创建套接字
    SOCKET serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (serverSocket == INVALID_SOCKET) {
        cout << "socket创建失败" << endl;
        WSACleanup();
        return 1;
    }

    // 绑定地址和端口
    sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = inet_addr("192.168.101.1");
    serverAddr.sin_port = htons(8000);

    if (bind(serverSocket, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        cout << "连接失败" << endl;
        closesocket(serverSocket);
        WSACleanup();
        return 1;
    }

    cout << "等待连接ing." << endl;

    // 等待客户端连接
    SOCKET clientSocket;
    sockaddr_in clientAddr;
    int addrLen = sizeof(clientAddr);
    listen(serverSocket, 5);
    clientSocket = accept(serverSocket, (struct sockaddr*)&clientAddr, &addrLen);
    if (clientSocket == INVALID_SOCKET) {
        cout << "接受失败" << endl;
        closesocket(serverSocket);
        WSACleanup();
        return 1;
    }

    thread receiveThread(receiveMessages, clientSocket);

    char buffer[1024];
    int bytesRead;
    cout << "---------------连接成功了--------------" << endl;
    cout << "---------------这里是服务端--------------" << endl;
    while (true) {
       // cout << "服务器端: ";
        cin.getline(buffer, sizeof(buffer));

        send(clientSocket, buffer, strlen(buffer), 0);
        if (strcmp(buffer, "quit") == 0) {
            cout << "Server disconnected." << endl;
            closesocket(clientSocket);
            closesocket(serverSocket);
            WSACleanup();
            exit(1);

        }
    }

    // 关闭套接字
    closesocket(clientSocket);
    closesocket(serverSocket);
    WSACleanup();

    return 0;
}
