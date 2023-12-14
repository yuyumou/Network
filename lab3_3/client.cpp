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
#define SYN 0x1
#define ACK 0x2
#define FIN 0x4
#define END 0x8

#define MAX_DATA_SIZE 8192
#define MAX_SEQ 0xffff
using namespace std;
#define OUTPUT_LOG
#undef OUTPUT_LOG
time_t now;
char* curr_time = ctime(&now);
struct PacketHead {
    u_int seq;
    u_int ack;
    u_short checkSum;
    u_short bufSize;
    char flag;
    u_char windows;

    PacketHead() {
        seq = ack = 0;
        checkSum = bufSize = 0;
        flag = 0;
        windows = 0;
    }
};

struct Packet {
    PacketHead head;
    char data[MAX_DATA_SIZE];
    bool beACKed;
    clock_t start;
};

u_short CheckPacketSum(u_short* packet, int packetLen) {
    if (packetLen > 100) {
        packetLen -= sizeof(clock_t);
        return 0;
    }
    u_long sum = 0;
    int count = (packetLen + 1) / 2;
    u_short* temp = new u_short[count];
    //u_short temp1[MAX_DATA_SIZE];
    //u_short* temp = temp1;
    memset(temp, 0, 2 * count);
    memcpy(temp, packet, packetLen);

    while (count--) {
        sum += *temp++;
        if (sum & 0xFFFF0000) {
            sum &= 0xFFFF;
            sum++;
        }
    }
    return ~(sum & 0xFFFF);
}

#pragma comment(lib, "ws2_32.lib")  //加载 ws2_32.dll
#pragma comment(lib, "winmm.lib")
using namespace std;
#define min(a, b) a>b?b:a
#define max(a, b) a>b?a:b
static SOCKADDR_IN addrSrv;
static int addrLen = sizeof(addrSrv);
#define PORT 8000
double MAX_TIME = CLOCKS_PER_SEC / 4;

string ADDRSRV;
static int windowSize = 8;//设定窗口
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
    //sendto(socket, buffer, sizeof(head), 0, (sockaddr *) &addr, len);

    //cout << "[SYN_SEND]第一次握手成功" << endl;

    int flag = sendto(socket, buffer, sizeof(head), 0, (sockaddr*)&addr, len);
    //第一次握手发送时间
    clock_t pack1starttime = clock();
    if (flag == -1)
    {
        now = time(nullptr);
        curr_time = ctime(&now);
        cout << curr_time << "[System]:" << endl;
        cout << "——————[ERROR]第一次握手发送失败————" << endl;
    }
    else
    {
        now = time(nullptr);
        curr_time = ctime(&now);
        cout << curr_time << "[System]:" << endl;
    }

    start = clock(); //开始计时
    while (recvfrom(socket, buffer, sizeof(head), 0, (sockaddr*)&addr, &len) <= 0) {
        if (clock() - start >= MAX_TIME) {
            memcpy(buffer, &head, sizeof(head));
            sendto(socket, buffer, sizeof(buffer), 0, (sockaddr*)&addr, len);
            cout << "——————[again_SYN_SEND]第一次握手超时重传————" << endl;
            start = clock();
        }
    }
    cout << "——————[SYN_SEND]第一次握手发送成功————" << endl;
    memcpy(&head, buffer, sizeof(head));
    if ((head.flag & ACK) && (CheckPacketSum((u_short*)&head, sizeof(head)) == 0) && (head.flag & SYN)) {
        cout << "——————[ACK_RECV]第二次握手成功——————" << endl;
    }
    else {
        cout << "——————[ERROR]第二次握手失败——————" << endl;
        return false;
    }

    //服务器建立连接
    head.flag = 0;
    head.flag |= ACK;
    head.checkSum = 0;
    head.checkSum = (CheckPacketSum((u_short*)&head, sizeof(head)));
    memcpy(buffer, &head, sizeof(head));
    sendto(socket, buffer, sizeof(head), 0, (sockaddr*)&addr, len);

    //等待两个MAX_TIME，如果没有收到消息说明ACK没有丢包
    start = clock();
    while (clock() - start <= 2 * MAX_TIME) {
        if (recvfrom(socket, buffer, sizeof(PacketHead), 0, (SOCKADDR*)&addr, &len) <= 0)
            continue;
        //说明这个ACK丢了
        memcpy(buffer, &head, sizeof(head));
        sendto(socket, buffer, sizeof(head), 0, (sockaddr*)&addr, len);
        cout << "——————[again_ACK_SEND]第三次握手超时重传————" << endl;

        start = clock();
    }
    cout << "——————[ACK_SEND]三次握手成功——————" << endl;
    cout << "——————[CONNECTED]成功与服务器建立连接，准备发送数据——————" << endl;
    return true;
}


bool disConnect(SOCKET& socket, SOCKADDR_IN& addr) {

    int addrLen = sizeof(addr);
    char* buffer = new char[sizeof(PacketHead)];
    PacketHead closeHead;
    closeHead.flag |= FIN;
    closeHead.checkSum = CheckPacketSum((u_short*)&closeHead, sizeof(PacketHead));
    memcpy(buffer, &closeHead, sizeof(PacketHead));

    if (sendto(socket, buffer, sizeof(PacketHead), 0, (SOCKADDR*)&addr, addrLen) != SOCKET_ERROR)
        cout << "——————[FIN_SEND]第一次挥手成功——————" << endl;
    else {
        cout << "——————[ERROR]第一次挥手失败——————" << endl;
        return false;
    }

    start = clock();
    while (recvfrom(socket, buffer, sizeof(PacketHead), 0, (sockaddr*)&addr, &addrLen) <= 0) {
        if (clock() - start >= MAX_TIME) {
            memcpy(buffer, &closeHead, sizeof(PacketHead));
            sendto(socket, buffer, sizeof(PacketHead), 0, (SOCKADDR*)&addr, addrLen);
            cout << "——————[again_FIN_SEND]第一次挥手超时重传——————" << endl;
            start = clock();
        }
    }
    if ((((PacketHead*)buffer)->flag & ACK) && (CheckPacketSum((u_short*)buffer, sizeof(PacketHead) == 0))) {
        cout << "——————[ACK_RECV]第二次挥手成功，客户端已经断开——————" << endl;
    }
    else {

        return false;
    }

    u_long imode = 0;
    ioctlsocket(socket, FIONBIO, &imode);//阻塞
    recvfrom(socket, buffer, sizeof(PacketHead), 0, (SOCKADDR*)&addr, &addrLen);
    memcpy(&closeHead, buffer, sizeof(PacketHead));
    if ((((PacketHead*)buffer)->flag & FIN) && (CheckPacketSum((u_short*)buffer, sizeof(PacketHead) == 0))) {
        cout << "——————[FIN_RECV]第三次挥手成功，服务器断开——————" << endl;
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
    start = clock();
    while (clock() - start <= 2 * MAX_TIME) {
        if (recvfrom(socket, buffer, sizeof(PacketHead), 0, (SOCKADDR*)&addr, &addrLen) <= 0)
            continue;
        //说明这个ACK丢了
        memcpy(buffer, &closeHead, sizeof(PacketHead));
        sendto(socket, buffer, sizeof(PacketHead), 0, (sockaddr*)&addr, addrLen);
        cout << "——————[again_ACK_SEND]第四次挥手超时重传——————" << endl;

        start = clock();
    }

    cout << "——————[ACK_SEND]第四次挥手成功，连接已关闭——————" << endl;
    closesocket(socket);
    return true;
}

Packet makePacket(u_int seq, char* data, int len) {
    Packet pkt;
    pkt.head.seq = seq;
    pkt.head.bufSize = len;
    memcpy(pkt.data, data, len);
    pkt.beACKed = false;
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
    if (seq >= base && seq < base + windowSize)
        return true;
    if (seq < base && seq < ((base + windowSize) % MAX_SEQ))
        return true;

    return false;
}

// ACKHandler函数用于处理接收到的ACK数据包
DWORD WINAPI ACKHandler(LPVOID param) {
    SOCKET* clientSock = (SOCKET*)param;  // 将传入的参数转换为SOCKET类型指针，表示客户端套接字
    char recvBuffer[sizeof(Packet)];  // 接收缓冲区，用于接收数据包
    Packet recvPacket;  // 接收到的数据包
    while (true) {
        // 通过非阻塞方式接收ACK数据包
        if (recvfrom(*clientSock, recvBuffer, sizeof(Packet), 0, (SOCKADDR*)&addrSrv, &addrLen) > 0) {
            // 将接收到的数据包转换为Packet结构体
            memcpy(&recvPacket, recvBuffer, sizeof(Packet));
            // 检查ACK数据包的校验和以及是否包含ACK标志
            if (CheckPacketSum((u_short*)&recvPacket, sizeof(Packet)) == 0 && recvPacket.head.flag & ACK) {
                // 使用互斥锁保护共享资源，即发送窗口的更新操作
                mutexLock.lock();
                bool islocked = true;
                bool mw = false;
                u_int nextbeACKed = 0;
                // 判断ACK中确认号是否在发送窗口内
                // base <= ack 且从base到新的都被确认；该移动窗口了
                if ((sendPkt[0].beACKed == false) && (base == recvPacket.head.ack)) {
                    mw = true;
                    nextbeACKed = base;
                    bool windowisfull = false;
                    for (int i = 1; i < windowSize; i++) {
                        windowisfull = true;
                        if (!sendPkt[i].beACKed) {//移动窗口辅助标志位直到未确认的数据包之前
                            if (sendPkt[i - 1].head.seq > nextbeACKed)
                                nextbeACKed = sendPkt[i - 1].head.seq;
                            windowisfull = false;
                            break;
                        }
                    }
                    if (windowisfull) {
                        nextbeACKed = base + windowSize - 1;
                    }
                }
                if (recvPacket.head.ack >= base) {
                    sendPkt[recvPacket.head.ack - base].beACKed = true;//设置标志表示正确接收到
                    sendPkt[recvPacket.head.ack - base].start = clock();//刷新计时器
                    cout << "——————[RECV] " << recvPacket.head.ack << " 号数据包被确认收到——————" << endl;
                    //break;
                }
                if (mw) {
                    mw = false;
                    // 计算ACK中确认号相对于发送窗口基序号的偏移量
                    int d = nextbeACKed + 1 - base;
                    
                    // 将发送窗口中已经确认的数据包移出窗口
                    assert(sendPkt != nullptr);
                    for (int i = 0; i < windowSize - d; i++) {
                        sendPkt[i] = sendPkt[i + d];
                        sendPkt[i + d].beACKed = false;
                    }
                    
                    // 更新base
                    base = (max((nextbeACKed + 1), base)) % MAX_SEQ;
                    cout << "[move]将窗口移动到：" << base << endl;
                }
                // 如果发送窗口为空，停止计时器
                start = clock();
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
            printf("整次传输用时 %lf s\n",
                double(duration.count()) * chrono::microseconds::period::num /
                chrono::microseconds::period::den); // 输出传输时间

            int totalDataSize = len; // 传输的数据字节数
            double totalTime = double(duration.count()) * chrono::microseconds::period::num /
                chrono::microseconds::period::den; // 传输的时间（秒）

            // 计算平均吞吐率（单位：字节每秒）总请求数/响应时间
            double averageThroughput = (totalDataSize / static_cast<double>(totalTime)) * 1000;

            std::cout << "整次传输的平均吞吐率: " << averageThroughput << " bytes 每秒" << std::endl;

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
                cout << "——————[END]文件传输完成——————" << endl;
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

            assert(sendPkt != nullptr);  // 断言，确保sendPkt指针不为空
            sendPkt[(int)waitingNum(nextSeqNum)] = makePacket(nextSeqNum, data_buffer, packetDataLen);  // 创建数据包并存储在sendPkt数组中的对应位置
            memcpy(pkt_buffer, &sendPkt[(int)waitingNum(nextSeqNum)], sizeof(Packet));  // 将数据包复制到pkt_buffer中
            nextSeqNum = (nextSeqNum + 1) % MAX_SEQ;  // 更新下一个序列号，取模MAX_SEQ
            sendto(socket, pkt_buffer, sizeof(Packet), 0, (SOCKADDR*)&addr, addrLen);  // 通过socket发送数据包到指定地址
            cout << "发送第" << sendIndex << "号数据包" << endl;  // 输出发送的数据包编号
            waitcount = 0;  // 重置等待计数为0
            if (base == nextSeqNum) {  // 如果base等于nextSeqNum
                start = clock();  // 记录当前时钟时间
            }

            sendIndex++;  // 增加发送索引，表示已发送一个数据包
        }
        mutexLock.unlock();  // 释放互斥锁

        if ((clock() - start >= MAX_TIME * 2)) {  // 检查是否需要进行重传
            mutexLock.lock();  // 获取互斥锁
            int resend_num = (int)waitingNum(nextSeqNum);  // 获取需要重传的数据包数量
            for (int i = 0; i < resend_num; i++) {  // 遍历需要重传的数据包
                if ((!sendPkt[i].beACKed) && (clock() - sendPkt[i].start >= MAX_TIME * 2)) {  // 检查数据包是否未被确认，并且时间超过了最大时间的两倍
                    memcpy(pkt_buffer, &sendPkt[i], sizeof(Packet));  // 将数据包复制到pkt_buffer中
                    sendto(socket, pkt_buffer, sizeof(Packet), 0, (SOCKADDR*)&addr, addrLen);  // 通过socket重新发送数据包到指定地址
                    sendPkt[i].start = clock();  // 更新数据包的开始时间
                    cout << "[！超时重传！]第 " << sendIndex - resend_num + i << " 号数据包 " << endl;  // 输出已重传的数据包编号
                }
            }
            mutexLock.unlock();  // 释放互斥锁
        }
    }
}

int main() {
    WSAData wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        //加载失败
        cout << "——————[SYSTEM]加载DLL失败——————" << endl;
        return -1;
    }
    SOCKET sockClient = socket(AF_INET, SOCK_DGRAM, 0);

    u_long imode = 1;
    ioctlsocket(sockClient, FIONBIO, &imode);//非阻塞

    ADDRSRV = "192.168.174.1";

    addrSrv.sin_family = AF_INET;
    addrSrv.sin_port = htons(PORT);
    addrSrv.sin_addr.S_un.S_addr = inet_addr(ADDRSRV.c_str());

    if (!connectToServer(sockClient, addrSrv)) {
        cout << "——————[ERROR]连接失败——————" << endl;
        return 0;
    }
    sendPkt = new Packet[windowSize];

    cout << "当前窗口大小为：" << windowSize << endl;

    string filename;
    cout << "——————[SYSTEM]请输入需要传输的文件名(1.jpg,2.jpg,3.jpg,helloworld.txt)——————" << endl;
    cin >> filename;

    ifstream infile(filename, ifstream::binary);

    if (!infile.is_open()) {
        cout << "——————[ERROR]无法打开文件——————" << endl;
        return 0;
    }

    infile.seekg(0, infile.end);
    u_long fileLen = infile.tellg();
    infile.seekg(0, infile.beg);
    cout << fileLen << endl;

    char* fileBuffer = new char[fileLen];
    infile.read(fileBuffer, fileLen);
    infile.close();
    //cout.write(fileBuffer,fileLen);
    cout << "——————[SYSTEM]开始传输——————" << endl;

    sendFSM(fileLen, fileBuffer, sockClient, addrSrv);

    cout << "——————总传输的数据包总长度为" << fileLen << "bytes——————" << endl;
    if (!disConnect(sockClient, addrSrv)) {
        cout << "——————[ERROR]断开失败——————" << endl;
        return 0;
    }
    cout << "——————文件传输完成——————" << endl;
    return 1;
}
