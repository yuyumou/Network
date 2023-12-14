#include <iostream>
#include <WINSOCK2.h>
#include <ctime>
#include <fstream>
#include <cstdio>
#include <windows.h>
#include <iostream>
#include <thread>
#include <vector>
#define SYN 0x1
#define ACK 0x2
#define FIN 0x4
#define END 0x8
#define PORT 8004
#define ADDRSRV "192.168.174.1"
#define MAX_FILE_SIZE 100000000
#define LOSS 100
#define MAX_DATA_SIZE 8192
#define MAX_SEQ 0xffff
using namespace std;
#define OUTPUT_LOG
#undef OUTPUT_LOG
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
    bool isAck;
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


double MAX_TIME = CLOCKS_PER_SEC / 4;
double MAX_WAIT_TIME = MAX_TIME / 4;
static u_int base_stage = 0;
static int windowSize = 8;//设定对应窗口大小
static Packet* sendArr = nullptr;
static int base = 0;
static int sendCount = 0;

static int losscount = 0;
char fileBuffer[MAX_FILE_SIZE];


bool inWindows(u_int seq) {
    if (seq >= base && seq < base + windowSize)
        return true;

    return false;
}

bool acceptClient(SOCKET& socket, SOCKADDR_IN& addr) {

    char* buffer = new char[sizeof(PacketHead)];
    int len = sizeof(addr);
    recvfrom(socket, buffer, sizeof(PacketHead), 0, (SOCKADDR*)&addr, &len);


    if ((((PacketHead*)buffer)->flag & SYN) && (CheckPacketSum((u_short*)buffer, sizeof(PacketHead)) == 0))
        cout << "——————[SYN_RECV]第一次握手成功——————" << endl;
    else
        return false;
    base_stage = ((PacketHead*)buffer)->seq;

    PacketHead head;
    head.flag |= ACK;
    head.flag |= SYN;
    head.checkSum = CheckPacketSum((u_short*)&head, sizeof(PacketHead));
    memcpy(buffer, &head, sizeof(PacketHead));
    if (sendto(socket, buffer, sizeof(PacketHead), 0, (sockaddr*)&addr, len) == -1) {
        return false;
    }

    cout << "——————[SYN_ACK_SEND]第二次握手成功——————" << endl;
    u_long imode = 1;
    ioctlsocket(socket, FIONBIO, &imode);//非阻塞

    clock_t start = clock(); //开始计时
    while (recvfrom(socket, buffer, sizeof(head), 0, (sockaddr*)&addr, &len) <= 0) {
        if (clock() - start >= MAX_TIME) {
            sendto(socket, buffer, sizeof(buffer), 0, (sockaddr*)&addr, len);
            cout << "——————[again_SYN_ACK_SEND]第二次握手超时重传——————" << endl;

            start = clock();
        }
    }


    if ((((PacketHead*)buffer)->flag & ACK) && (CheckPacketSum((u_short*)buffer, sizeof(PacketHead)) == 0)) {
        cout << "——————[ACK_RECV]第三次握手成功——————" << endl;
    }
    else {
        return false;
    }
    imode = 0;
    ioctlsocket(socket, FIONBIO, &imode);//阻塞

    cout << "——————[CONNECTED]与用户端成功建立连接，准备接收文件——————" << endl;
    return true;
}

bool disConnect(SOCKET& socket, SOCKADDR_IN& addr) {
    int addrLen = sizeof(addr);
    char* buffer = new char[sizeof(PacketHead)];

    recvfrom(socket, buffer, sizeof(PacketHead), 0, (SOCKADDR*)&addr, &addrLen);


    if ((((PacketHead*)buffer)->flag & FIN) && (CheckPacketSum((u_short*)buffer, sizeof(PacketHead) == 0))) {
        cout << "——————[FIN ACK]第一次挥手完成，用户端断开——————" << endl;
    }
    else {
        return false;
    }

    PacketHead closeHead;
    closeHead.flag = 0;
    closeHead.flag |= ACK;
    closeHead.checkSum = CheckPacketSum((u_short*)&closeHead, sizeof(PacketHead));
    memcpy(buffer, &closeHead, sizeof(PacketHead));
    sendto(socket, buffer, sizeof(PacketHead), 0, (SOCKADDR*)&addr, addrLen);

    cout << "——————[ACK]第二次挥手完成——————" << endl;

    closeHead.flag = 0;
    closeHead.flag |= FIN;
    closeHead.checkSum = CheckPacketSum((u_short*)&closeHead, sizeof(PacketHead));
    memcpy(buffer, &closeHead, sizeof(PacketHead));
    sendto(socket, buffer, sizeof(PacketHead), 0, (SOCKADDR*)&addr, addrLen);

    cout << "——————[FIN ACK]第三次挥手完成——————" << endl;

    u_long imode = 1;
    ioctlsocket(socket, FIONBIO, &imode);
    clock_t start = clock();
    while (recvfrom(socket, buffer, sizeof(PacketHead), 0, (sockaddr*)&addr, &addrLen) <= 0) {
        if (clock() - start >= MAX_TIME) {
            memcpy(buffer, &closeHead, sizeof(PacketHead));
            sendto(socket, buffer, sizeof(PacketHead), 0, (SOCKADDR*)&addr, addrLen);
            cout << "——————[again_FIN ACK]第三次挥手超时重传——————" << endl;
            start = clock();
        }
    }

    if ((((PacketHead*)buffer)->flag & ACK) && (CheckPacketSum((u_short*)buffer, sizeof(PacketHead) == 0))) {
        cout << "——————[ACK]第四次挥手完成，连接关闭——————" << endl;
    }
    else {
        return false;
    }
    closesocket(socket);
    return true;
}

Packet makePacket(u_int ack) {
    Packet pkt;
    pkt.head.ack = ack;
    pkt.head.flag |= ACK;
    pkt.head.checkSum = CheckPacketSum((u_short*)&pkt, sizeof(Packet));
    pkt.isAck = false;
    pkt.start = clock();
    return pkt;
}

u_long recvFSM(char* fileBuffer, SOCKET& socket, SOCKADDR_IN& addr) {
    u_long fileLen = 0;
    int addrLen = sizeof(addr);
    u_int expectedSeq = base_stage;
    int dataLen;

    char* pkt_buffer = new char[sizeof(Packet)];
    Packet recvPkt, sendPkt = makePacket(base_stage - 1), lossPkt;
    clock_t start;
    bool clockStart = false;
    while (true) {       
        memset(pkt_buffer, 0, sizeof(Packet));
        recvfrom(socket, pkt_buffer, sizeof(Packet), 0, (SOCKADDR*)&addr, &addrLen);
        memcpy(&recvPkt, pkt_buffer, sizeof(Packet));

        if (recvPkt.head.flag & END && CheckPacketSum((u_short*)&recvPkt, sizeof(PacketHead)) == 0) {
            cout << "——————传输完毕——————" << endl;
            PacketHead endPacket;
            endPacket.flag |= ACK;
            endPacket.checkSum = CheckPacketSum((u_short*)&endPacket, sizeof(PacketHead));
            memcpy(pkt_buffer, &endPacket, sizeof(PacketHead));
            sendto(socket, pkt_buffer, sizeof(PacketHead), 0, (SOCKADDR*)&addr, addrLen);
            return fileLen;
        }

        if (inWindows(recvPkt.head.seq) && CheckPacketSum((u_short*)&recvPkt, sizeof(Packet)) == 0) {
            
            sendArr[recvPkt.head.seq - base] = makePacket(recvPkt.head.seq);
            
            memcpy(&sendArr[recvPkt.head.seq - base].data, recvPkt.data, recvPkt.head.bufSize);
            sendArr[recvPkt.head.seq - base].isAck = true;
            memcpy(pkt_buffer, &sendArr[recvPkt.head.seq - base], sizeof(Packet));
            sendto(socket, pkt_buffer, sizeof(Packet), 0, (SOCKADDR*)&addr, addrLen);
            cout << "向客户端发送收到" << recvPkt.head.seq << "号数据包"<<endl;

            if ((sendArr[0].head.ack == base)) {//该移动窗口了
                bool flag = true;
                int movecount = 0;
                base += 1;
                movecount++;
                cout << "——————[move]将窗口移动到：" << base << "号位置——————" << endl;
                dataLen = recvPkt.head.bufSize;
                memcpy(fileBuffer + fileLen, sendArr[0].data, dataLen);
                fileLen += dataLen;
                for (int i = 1; i < windowSize; i++) {
                    if (sendArr[i].head.ack == base) {
                        flag = false;
                        base += 1;
                        movecount++;
                        dataLen = recvPkt.head.bufSize;
                        memcpy(fileBuffer + fileLen, sendArr[i].data, dataLen);
                        fileLen += dataLen;
                    }
                    else {
                        for (int j = 0; j < windowSize - movecount; j++) {
                            sendArr[j] = sendArr[j + movecount];
                            sendArr[j + movecount].head.ack = -1;
                        }
                        if (!flag)
                            cout << "——————[move]将窗口移动到：" << base << "号位置——————" << endl;
                        break;
                    }
                }
            }

            continue;
        }
        if (CheckPacketSum((u_short*)&recvPkt, sizeof(Packet)) != 0) {
            //cout << sizeof(Packet) << endl;
            cout << "CheckSun Wrong  " << CheckPacketSum((u_short*)&recvPkt, sizeof(Packet)) << endl;
            Packet wrongSumpkt;
            wrongSumpkt.head.ack = -1;
            wrongSumpkt.head.flag |= ACK;
            wrongSumpkt.head.checkSum = CheckPacketSum((u_short*)&wrongSumpkt, sizeof(Packet));
            memcpy(pkt_buffer, &wrongSumpkt, sizeof(Packet));
            sendto(socket, pkt_buffer, sizeof(Packet), 0, (SOCKADDR*)&addr, addrLen);
            continue;
        }

        cout << "当前窗口区间为：[" << base << ", " << base + windowSize << "]接收到：" << recvPkt.head.seq << endl;
        sendPkt = makePacket(recvPkt.head.seq);
        memcpy(pkt_buffer, &sendPkt, sizeof(Packet));
        sendto(socket, pkt_buffer, sizeof(Packet), 0, (SOCKADDR*)&addr, addrLen);
        cout << "向客户端发送收到" << recvPkt.head.seq << endl;
    }
}

int main() {
    WSAData wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        //加载失败
        cout << "——————[ERROR]加载DLL失败——————" << endl;
        return -1;
    }
    SOCKET sockSrv = socket(AF_INET, SOCK_DGRAM, 0);

    SOCKADDR_IN addrSrv;
    addrSrv.sin_family = AF_INET;
    addrSrv.sin_port = htons(PORT);
    addrSrv.sin_addr.S_un.S_addr = inet_addr(ADDRSRV);
    bind(sockSrv, (SOCKADDR*)&addrSrv, sizeof(SOCKADDR));

    SOCKADDR_IN addrClient;

    //三次握手建立连接
    if (!acceptClient(sockSrv, addrClient)) {
        cout << "——————[ERROR]连接失败——————" << endl;
        return 0;
    }
    sendArr = new Packet[windowSize];
    //char fileBuffer[MAX_FILE_SIZE];
    //可靠数据传输过程
    string recvfile_name;
    u_long fileLen = recvFSM(fileBuffer, sockSrv, addrClient);
    //四次挥手断开连接
    if (!disConnect(sockSrv, addrClient)) {
        cout << "——————[ERROR]断开失败——————" << endl;
        return 0;
    }

    //写入复制文件
    string filename = R"(C:\Users\DELL\Desktop\计算机网络\test\test.jpg)";
    ofstream outfile(filename, ios::binary);
    if (!outfile.is_open()) {
        cout << "——————[ERROR]打开文件出错——————" << endl;
        return 0;
    }
    cout << "——————接收到的数据包总长度为" << fileLen << "bytes——————" << endl;
    outfile.write(fileBuffer, fileLen);
    outfile.close();

    cout << "——————[FINISHED]文件复制完毕——————" << endl;
    return 1;
}
