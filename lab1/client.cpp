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
            cout << "Server disconnected." << endl;
            closesocket(clientSocket);
            WSACleanup();
            exit(1);
        }
        buffer[bytesRead] = '\0';
        cout << "服务器端: " << buffer << endl;
    }
}

int main() {
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        cout << "初始化失败" << endl;
        return 1;
    }

    // 创建套接字
    SOCKET clientSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (clientSocket == INVALID_SOCKET) {
        cout << "socket创建失败" << endl;
        WSACleanup();
        return 1;
    }

    // 连接到服务器
    sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = inet_addr("192.168.101.1");  // 服务器的IP地址
    serverAddr.sin_port = htons(8000);  // 服务器的端口号

    if (connect(clientSocket, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        cout << "连接失败" << endl;
        closesocket(clientSocket);
        WSACleanup();
        return 1;
    }

    thread receiveThread(receiveMessages, clientSocket);

    char buffer[1024];

    while (true) {
       // cout << "客户端: ";
        cin.getline(buffer, sizeof(buffer));

        send(clientSocket, buffer, strlen(buffer), 0);
        if (strcmp(buffer, "quit") == 0) {
            cout << "Client disconnected." << endl;
            closesocket(clientSocket);
            WSACleanup();
            exit(1);
        }
    }

    // 关闭套接字
    closesocket(clientSocket);
    WSACleanup();

    return 0;
}
