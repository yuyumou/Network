#include <iostream>
#include <WINSOCK2.h>
#include <ctime>
#include <fstream>
#include <cstdio>
#include <windows.h>
#include <iostream>
#include <thread>
#include <assert.h>
#include <chrono>
#include <mutex>
#include "rdt3.h"

#pragma comment(lib, "ws2_32.lib")  //加载 ws2_32.dll
#pragma comment(lib, "winmm.lib")
using namespace std;
#define min(a, b) a>b?b:a
#define max(a, b) a>b?a:b
static SOCKADDR_IN addrSrv;
static int addrLen = sizeof(addrSrv);
#define PORT 5200
double MAX_TIME = CLOCKS_PER_SEC / 4;

string ADDRSRV;
static int windowSize = 32;
static unsigned int base = 0;//握手阶段确定的初始序列号
static unsigned int nextSeqNum = 0;
static Packet* sendPkt = nullptr;
static int sendIndex = 0;
static clock_t start;
static int packetNum;


static int repeatAck = 0;
static int repeatAckcount = 0;
bool needquikresend = false;
mutex mutexLock;

bool connectToServer(SOCKET& socket, SOCKADDR_IN& addr) {
    int len = sizeof(addr);

    PacketHead head;
    head.flag |= SYN;
    head.seq = base;
    head.checkSum = CheckPacketSum((u_short*)&head, sizeof(head));

    char* buffer = new char[sizeof(head)];
    memcpy(buffer, &head, sizeof(head));
    sendto(socket, buffer, sizeof(head), 0, (sockaddr*)&addr, len);
    ShowPacket((Packet*)&head);
    cout << "[SYN_SEND]第一次握手成功" << endl;

    clock_t start_connect = clock(); //开始计时
    while (recvfrom(socket, buffer, sizeof(head), 0, (sockaddr*)&addr, &len) <= 0) {
        if (clock() - start_connect >= MAX_TIME) {
            memcpy(buffer, &head, sizeof(head));
            sendto(socket, buffer, sizeof(buffer), 0, (sockaddr*)&addr, len);
            start_connect = clock();
        }
    }

    memcpy(&head, buffer, sizeof(head));
    ShowPacket((Packet*)&head);
    if ((head.flag & ACK) && (CheckPacketSum((u_short*)&head, sizeof(head)) == 0) && (head.flag & SYN)) {
        cout << "[ACK_RECV]第二次握手成功" << endl;
    }
    else {
        return false;
    }

    //windowSize = head.windows;
    //服务器建立连接
    head.flag = 0;
    head.flag |= ACK;
    head.checkSum = 0;
    head.checkSum = (CheckPacketSum((u_short*)&head, sizeof(head)));
    memcpy(buffer, &head, sizeof(head));
    sendto(socket, buffer, sizeof(head), 0, (sockaddr*)&addr, len);
    ShowPacket((Packet*)&head);

    //等待两个MAX_TIME，如果没有收到消息说明ACK没有丢包
    start_connect = clock();
    while (clock() - start_connect <= 2 * MAX_TIME) {
        if (recvfrom(socket, buffer, sizeof(PacketHead), 0, (SOCKADDR*)&addr, &len) <= 0)
            continue;
        //说明这个ACK丢了
        memcpy(buffer, &head, sizeof(head));
        sendto(socket, buffer, sizeof(head), 0, (sockaddr*)&addr, len);
        start_connect = clock();
    }
    cout << "[ACK_SEND]三次握手成功" << endl;
    cout << "[CONNECTED]成功与服务器建立连接，准备发送数据" << endl;
    return true;
}

bool disConnect(SOCKET& socket, SOCKADDR_IN& addr) {

    char* buffer = new char[sizeof(PacketHead)];
    PacketHead closeHead;
    closeHead.flag |= FIN;
    closeHead.checkSum = CheckPacketSum((u_short*)&closeHead, sizeof(PacketHead));
    memcpy(buffer, &closeHead, sizeof(PacketHead));

    ShowPacket((Packet*)&closeHead);
    if (sendto(socket, buffer, sizeof(PacketHead), 0, (SOCKADDR*)&addr, addrLen) != SOCKET_ERROR)
        cout << "[FIN_SEND]第一次挥手成功" << endl;
    else
        return false;

    clock_t start = clock();
    while (recvfrom(socket, buffer, sizeof(PacketHead), 0, (sockaddr*)&addr, &addrLen) <= 0) {
        if (clock() - start >= MAX_TIME) {
            memcpy(buffer, &closeHead, sizeof(PacketHead));
            sendto(socket, buffer, sizeof(PacketHead), 0, (SOCKADDR*)&addr, addrLen);
            start = clock();
        }
    }
    ShowPacket((Packet*)buffer);
    if ((((PacketHead*)buffer)->flag & ACK) && (CheckPacketSum((u_short*)buffer, sizeof(PacketHead) == 0))) {
        cout << "[ACK_RECV]第二次挥手成功，客户端已经断开" << endl;
    }
    else {
        return false;
    }

    u_long imode = 0;
    ioctlsocket(socket, FIONBIO, &imode);//阻塞
    recvfrom(socket, buffer, sizeof(PacketHead), 0, (SOCKADDR*)&addr, &addrLen);
    memcpy(&closeHead, buffer, sizeof(PacketHead));
    ShowPacket((Packet*)buffer);
    if ((((PacketHead*)buffer)->flag & FIN) && (CheckPacketSum((u_short*)buffer, sizeof(PacketHead) == 0))) {
        cout << "[FIN_RECV]第三次挥手成功，服务器断开" << endl;
    }
    else {
        return false;
    }

    imode = 1;
    ioctlsocket(socket, FIONBIO, &imode);

    closeHead.flag = 0;
    closeHead.flag |= ACK;
    closeHead.checkSum = CheckPacketSum((u_short*)&closeHead, sizeof(PacketHead));

    memcpy(buffer, &closeHead, sizeof(PacketHead));
    sendto(socket, buffer, sizeof(PacketHead), 0, (SOCKADDR*)&addr, addrLen);
    ShowPacket((Packet*)&closeHead);
    start = clock();
    while (clock() - start <= 2 * MAX_TIME) {
        if (recvfrom(socket, buffer, sizeof(PacketHead), 0, (SOCKADDR*)&addr, &addrLen) <= 0)
            continue;
        //说明这个ACK丢了
        memcpy(buffer, &closeHead, sizeof(PacketHead));
        sendto(socket, buffer, sizeof(PacketHead), 0, (sockaddr*)&addr, addrLen);
        start = clock();
    }

    cout << "[ACK_SEND]第四次挥手成功，连接已关闭" << endl;
    closesocket(socket);
    return true;
}

Packet makePacket(u_int seq, char* data, int len) {
    Packet pkt;
    pkt.head.seq = seq;
    pkt.head.bufSize = len;
    memcpy(pkt.data, data, len);
    pkt.isAck = false;
    pkt.start = clock();
    pkt.head.checkSum = CheckPacketSum((u_short*)&pkt, sizeof(Packet));
    return pkt;
}

u_int waitingNum(u_int nextSeq) {
    if (nextSeq >= base)
        return nextSeq - base;
    return nextSeq + MAX_SEQ - base;
}

bool inWindows(u_int seq) {
    if (seq >= base && seq < base + windowSize  )
        return true;
    if (seq < base && seq < ((base + windowSize) % MAX_SEQ))
        return true;

    return false;
}

DWORD WINAPI ACKHandler(LPVOID param) {
    SOCKET* clientSock = (SOCKET*)param;
    char recvBuffer[sizeof(Packet)];
    Packet recvPacket;
    while (true) {
        // 通过非阻塞方式接收 ACK 数据包
        //cout<<"ACKHANLER is me??"<<endl;
        if (recvfrom(*clientSock, recvBuffer, sizeof(Packet), 0, (SOCKADDR*)&addrSrv, &addrLen) > 0) {
            // 将接收到的数据包转换为 Packet 结构体
            memcpy(&recvPacket, recvBuffer, sizeof(Packet));
            //cout << "[RECV]收到了ack" << recvPacket.head.ack << endl;
            // 检查 ACK 数据包的校验和以及是否包含 ACK 标志
            if (CheckPacketSum((u_short*)&recvPacket, sizeof(Packet)) == 0 && recvPacket.head.flag & ACK) {
                // 使用互斥锁保护共享资源，即发送窗口的更新操作
                mutexLock.lock();
                //if (recvPacket.head.ack >= base) {
                //    sendPkt[recvPacket.head.ack - base].isAck = true;
                //    sendPkt[recvPacket.head.ack - base].start = clock();
                //    cout << "[RECV] " << base << " is ACKED" << endl;
                //}
  /*              if (repeatAck == recvPacket.head.ack) {
                    repeatAckcount++;
                    if (repeatAckcount == 2) {
                        cout << "{ATTENTION} NEED QUICK RESEND" << endl;
                        needquikresend = true;
                        repeatAckcount = 0;
                    }
                }
                repeatAck = recvPacket.head.ack;
                bool islocked = true;*/
                bool islocked = true;
                bool isTomovewindow = false;
                u_int nextToackSeq = 0;
                // 判断 ACK 中确认号是否在发送窗口内
                // base <= ack 且从base到新的都被确认；该移动窗口了
                if ((sendPkt[0].isAck == false) && (base == recvPacket.head.ack)) {
                    isTomovewindow = true;
                    nextToackSeq = base;//sendPkt[0].head.ack;
                    //for (int i = 1; i < (int)waitingNum(nextSeqNum); i++) {
                    bool windowisfull = false;
                    for (int i = 1; i < windowSize; i++) {
                        windowisfull = true;
                        if (!sendPkt[i].isAck) {
                            nextToackSeq = sendPkt[i-1].head.seq;
                            //isTomovewindow = true;
                            windowisfull = false;
                            break;
                        }
                    }
                    if (windowisfull) {
                        nextToackSeq = base + windowSize - 1;
                    }
                }
                if (recvPacket.head.ack >= base) {
                    if (base == 226) {
                        int a = 1;
                    }
                    sendPkt[recvPacket.head.ack - base].isAck = true;
                    sendPkt[recvPacket.head.ack - base].start = clock();
                    cout << "[RECV] " << recvPacket.head.ack << " is ACKED" << endl;
                }
                if (isTomovewindow) {
                    isTomovewindow = false;
                    // 计算 ACK 中确认号相对于发送窗口基序号的偏移量
                    int d = nextToackSeq + 1 - base;
                    // 将发送窗口中已经确认的数据包移出窗口
                    assert(sendPkt != nullptr);
                    // move all before nextseq after acked to the begin
                    //for (int i = 0; i < (int)waitingNum(nextSeqNum) - d; i++) {
                    for (int i = 0; i < windowSize - d; i++) {
                        sendPkt[i] = sendPkt[i + d];
                        sendPkt[i + d].isAck = false;
                    }
                    ShowPacket(&recvPacket);
                    // 更新 base before base is acked 
                    base = (max((nextToackSeq+1), base)) % MAX_SEQ;
                    cout << "[MOVE WINDOW BASE TO]" << base << endl;
#ifdef OUTPUT_LOG
                    cout << "[window move]base:" << base << " nextSeq:" << nextSeqNum << " endWindow:"
                        << base + windowSize << endl;
#endif
                }
                //mutexLock.unlock();
                // 如果发送窗口为空，停止计时器
                start = clock();
                //if (base == nextSeqNum) {
                //    start = clock();
                //    //stopTimer = true;
                //}
                //else {
                //    // 否则重启计时器
                //    start = clock();
                //}
                // 如果发送窗口基序号等于总包数，表示文件传输完成，退出线程
                if (base == packetNum) {
                    mutexLock.unlock();
                    return 0;
                }

                if (recvPacket.head.windows == 1) {
                    //说明是checkSumc出了问题 重传上一个包
                    islocked = false;
                    mutexLock.unlock();
                }
                if (islocked) {
                    islocked = false;
                    mutexLock.unlock();
                }
            }

        }
    }
}

void sendFSM(u_long len, char* fileBuffer, SOCKET& socket, SOCKADDR_IN& addr) {
    int waitcount = 0;
    packetNum = int(len / MAX_DATA_SIZE) + (len % MAX_DATA_SIZE ? 1 : 0);

    auto nBeginTime = chrono::system_clock::now();
    auto nEndTime = nBeginTime;

    int packetDataLen;
    int addrLen = sizeof(addr);
    char* data_buffer = new char[sizeof(Packet)], * pkt_buffer = new char[sizeof(Packet)];
    //nextSeqNum维护下一个需要发送的数据包
    nextSeqNum = base;
    cout << "[SYS]本次文件数据长度为" << len << "Bytes,需要传输" << packetNum << "个数据包" << endl;
    HANDLE ackhandler = CreateThread(nullptr, 0, ACKHandler, LPVOID(&socket), 0, nullptr);
    while (true) {
        if (base == packetNum) {

            nEndTime = chrono::system_clock::now();
            auto duration = chrono::duration_cast<chrono::microseconds>(nEndTime - nBeginTime);
            printf("System use %lf s, and the throught is %lf KByte/s \n",
                double(duration.count()) * chrono::microseconds::period::num /
                chrono::microseconds::period::den, (len / 1000) / (double(duration.count()) * chrono::microseconds::period::num /
                    chrono::microseconds::period::den));

            CloseHandle(ackhandler);
            PacketHead endPacket;
            endPacket.flag |= END;
            endPacket.checkSum = CheckPacketSum((u_short*)&endPacket, sizeof(PacketHead));
            memcpy(pkt_buffer, &endPacket, sizeof(PacketHead));
            sendto(socket, pkt_buffer, sizeof(PacketHead), 0, (SOCKADDR*)&addr, addrLen);

            while (recvfrom(socket, pkt_buffer, sizeof(PacketHead), 0, (SOCKADDR*)&addr, &addrLen) <= 0) {
                if (clock() - start >= MAX_TIME) {
                    start = clock();
                    goto resend;
                }
            }

            if (((PacketHead*)(pkt_buffer))->flag & ACK &&
                CheckPacketSum((u_short*)pkt_buffer, sizeof(PacketHead)) == 0) {
                cout << "[Finish!]文件传输完成" << endl;
                return;
            }

        resend:
            continue;
        }

        //SendIndex维护当前发送了多少数据包
        packetDataLen = min(MAX_DATA_SIZE, len - sendIndex * MAX_DATA_SIZE);
        mutexLock.lock();
        if (inWindows(nextSeqNum) && sendIndex < packetNum) {
            memcpy(data_buffer, fileBuffer + sendIndex * MAX_DATA_SIZE, packetDataLen);

            assert(sendPkt != nullptr);
            sendPkt[(int)waitingNum(nextSeqNum)] = makePacket(nextSeqNum, data_buffer, packetDataLen);
            memcpy(pkt_buffer, &sendPkt[(int)waitingNum(nextSeqNum)], sizeof(Packet));
            nextSeqNum = (nextSeqNum + 1) % MAX_SEQ;
            ShowPacket(&sendPkt[(int)waitingNum(nextSeqNum)]);
            sendto(socket, pkt_buffer, sizeof(Packet), 0, (SOCKADDR*)&addr, addrLen);
            cout << "[Send]：" << sendIndex << " of " << packetNum << " 被发送" << endl;
            waitcount = 0;
            if (base == nextSeqNum) {
                start = clock();
            }

            sendIndex++;
        }
        mutexLock.unlock();

        if ((needquikresend) || (clock() - start >= MAX_TIME * 2)) {
            mutexLock.lock();
           // cout << "[time out!]resend begin" << endl;
            int resend_num = (int)waitingNum(nextSeqNum);
            for (int i = 0; i < resend_num; i++) {
                if ((!sendPkt[i].isAck) && (clock() - sendPkt[i].start >= MAX_TIME * 2)) {
                    memcpy(pkt_buffer, &sendPkt[i], sizeof(Packet));
                    sendto(socket, pkt_buffer, sizeof(Packet), 0, (SOCKADDR*)&addr, addrLen);
                    sendPkt[i].start = clock();
                    cout << "[RESEND] " << sendIndex - resend_num + i << " of " << packetNum << " 被重传" << endl;
                }

            }
            //int resend_num = (int)waitingNum(nextSeqNum);
            //for (int i = 0; i < resend_num; i++) {
            //    memcpy(pkt_buffer, &sendPkt[i], sizeof(Packet));
            //    sendto(socket, pkt_buffer, sizeof(Packet), 0, (SOCKADDR*)&addr, addrLen);
            //    //base + i
            //    cout << "[RESEND] " << sendIndex - resend_num + i << " of " << packetNum << " 被重传" << endl;
            //    ShowPacket(&sendPkt[i]);
            //}
            ////mutexLock.unlock();
            //start = clock();
            //needquikresend = false;
            mutexLock.unlock();
        }
    }
}

int main() {
    WSAData wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        //加载失败
        cout << "[SYSTEM]加载DLL失败" << endl;
        return -1;
    }
    SOCKET sockClient = socket(AF_INET, SOCK_DGRAM, 0);

    u_long imode = 1;
    ioctlsocket(sockClient, FIONBIO, &imode);//非阻塞

    ADDRSRV = "127.0.0.1";

    addrSrv.sin_family = AF_INET;
    addrSrv.sin_port = htons(PORT);
    addrSrv.sin_addr.S_un.S_addr = inet_addr(ADDRSRV.c_str());

    if (!connectToServer(sockClient, addrSrv)) {
        cout << "[ERROR]连接失败" << endl;
        return 0;
    }
    //cout << "请输入窗口大小" << endl;
    //cin >> windowSize;
    cout << "Current windows size is " << windowSize << endl;
    sendPkt = new Packet[windowSize];
    string filename;
    cout << "[SYSTEM]请输入需要传输的文件名" << endl;
    cin >> filename;

    ifstream infile(filename, ifstream::binary);

    if (!infile.is_open()) {
        cout << "[ERROR]无法打开文件" << endl;
        return 0;
    }

    infile.seekg(0, infile.end);
    u_long fileLen = infile.tellg();
    infile.seekg(0, infile.beg);
    cout << "准备发送文件: " << filename << "文件长度为： " << fileLen << endl;

    char* fileBuffer = new char[fileLen];
    infile.read(fileBuffer, fileLen);
    infile.close();
    //cout.write(fileBuffer,fileLen);
    cout << "[SYSTEM]开始传输" << endl;
    sendFSM(fileLen, fileBuffer, sockClient, addrSrv);

    if (!disConnect(sockClient, addrSrv)) {
        cout << "[ERROR]断开失败" << endl;
        return 0;
    }
    cout << "文件传输完成" << endl;
    return 1;
}
