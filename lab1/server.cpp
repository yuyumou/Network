#include <iostream>
#include <winsock2.h>
#include <vector>
#include <thread>
#include <mutex>

using namespace std;

#pragma comment(lib, "ws2_32.lib")

vector<SOCKET> clients;
mutex mtx;

void handleClient(SOCKET clientSocket) {
    char buffer[1024];
    int bytesRead;

    while (true) {
        bytesRead = recv(clientSocket, buffer, sizeof(buffer), 0);
        if (bytesRead <= 0) {
            unique_lock<mutex> lock(mtx);
            auto it = find(clients.begin(), clients.end(), clientSocket);
            if (it != clients.end()) {
                clients.erase(it);
            }
            closesocket(clientSocket);
            lock.unlock();
            break;
        }
        buffer[bytesRead] = '\0';

        unique_lock<mutex> lock(mtx);
        for (SOCKET otherClient : clients) {
            if (otherClient != clientSocket) {
                send(otherClient, buffer, strlen(buffer), 0);
            }
        }
        lock.unlock();
    }
}

int main() {
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        cout << "初始化失败" << endl;
        return 1;
    }

    SOCKET serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (serverSocket == INVALID_SOCKET) {
        cout << "socket创建失败" << endl;
        WSACleanup();
        return 1;
    }

    sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = inet_addr("192.168.101.1");;
    serverAddr.sin_port = htons(8000);

    if (bind(serverSocket, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        cout << "绑定失败" << endl;
        closesocket(serverSocket);
        WSACleanup();
        return 1;
    }

    cout << "服务器已开机" << endl;

    listen(serverSocket, 5);

    while (true) {
        SOCKET clientSocket;
        sockaddr_in clientAddr;
        int addrLen = sizeof(clientAddr);
        clientSocket = accept(serverSocket, (struct sockaddr*)&clientAddr, &addrLen);
        if (clientSocket == INVALID_SOCKET) {
            cout << "接受失败" << endl;
            closesocket(serverSocket);
            WSACleanup();
            return 1;
        }

        unique_lock<mutex> lock(mtx);
        clients.push_back(clientSocket);
        lock.unlock();

        thread clientThread(handleClient, clientSocket);
        clientThread.detach(); // 分离线程，不等待线程退出
    }

    closesocket(serverSocket);
    WSACleanup();

    return 0;
}
