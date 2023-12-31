#include <stdio.h>
#include <iostream>
#include <winsock2.h>
#include <Windows.h>
#include <WS2tcpip.h>
#include <fstream>
#include <thread>

using namespace std;

#pragma comment(lib, "ws2_32.lib")

#define PACKETSIZE 1500
#define HEADERSIZE 14
#define DATASIZE (PACKETSIZE-HEADERSIZE)
#define FILE_NAME_MAX_LENGTH 64

// 一些header中的标志位
#define SEQ_BITS_START 0
#define ACK_BITS_START 4
#define FLAG_BIT_POSITION 8
#define DATA_LENGTH_BITS_START 10
#define CHECKSUM_BITS_START 12


SOCKET serverSocket = INVALID_SOCKET;
SOCKADDR_IN serverAddr = { 0 }; // 接收端地址
SOCKADDR_IN clientAddr = { 0 }; // 发送端地址
int len = sizeof(clientAddr);
char header[HEADERSIZE] = { 0 };

u_short checkSum(const char* input, int length) {
	int count = (length + 1) / 2; // 有多少组16 bits
	u_short* buf = new u_short[count]{ 0 };
	for (int i = 0; i < count; i++) {
		buf[i] = (u_char)input[2 * i] + ((2 * i + 1 < length) ? (u_char)input[2 * i + 1] << 8 : 0);
		// 最后这个三元表达式是为了避免在计算buf最后一位时，出现input[length]的越界情况
	}

	register u_long sum = 0;
	while (count--) {
		sum += *buf++;
		// 如果sum有进位，则进位回滚
		if (sum & 0xFFFF0000) {
			sum &= 0xFFFF;
			sum++;
		}
	}
	return ~(sum & 0xFFFF);
}

bool handshake() {
	u_short checksum = 0;
	char recvBuf[HEADERSIZE] = { 0 };
	int recvResult = 0;
	int seq, ack;

	// 接受第一次握手请求报文
	while (true) {
		recvResult = recvfrom(serverSocket, recvBuf, HEADERSIZE, 0, (SOCKADDR*)&clientAddr, &len);
		// 检查checksum
		checksum = checkSum(recvBuf, HEADERSIZE);
		// 提取seq of message 1
		seq = (u_char)recvBuf[SEQ_BITS_START] + ((u_char)recvBuf[SEQ_BITS_START + 1] << 8)
			+ ((u_char)recvBuf[SEQ_BITS_START + 2] << 16) + ((u_char)recvBuf[SEQ_BITS_START + 3] << 24);

		if (checksum == 0 && recvBuf[FLAG_BIT_POSITION] == 0b010) {
			// 发送第一次握手应答报文
			memset(header, 0, HEADERSIZE);
			// 设置ack位，ack = seq of message 1 + 1
			ack = 1111;
			header[ACK_BITS_START] = (u_char)(ack & 0xFF);
			header[ACK_BITS_START + 1] = (u_char)(ack >> 8);
			header[ACK_BITS_START + 2] = (u_char)(ack >> 16);
			header[ACK_BITS_START + 3] = (u_char)(ack >> 24);
			sendto(serverSocket, header, HEADERSIZE, 0, (SOCKADDR*)&clientAddr, sizeof(SOCKADDR));
			cout << "第一次握手成功" << endl;
			break;
		}
		else {
			cout << "第一次握手失败" << endl;
			return false;
		}
	}
	

	// 发送第二次握手应答报文
	memset(header, 0, HEADERSIZE);
	// 设置ack位，ack = seq of message 1 + 1
	ack = seq + 1;
	header[ACK_BITS_START] = (u_char)(ack & 0xFF);
	header[ACK_BITS_START + 1] = (u_char)(ack >> 8);
	header[ACK_BITS_START + 2] = (u_char)(ack >> 16);
	header[ACK_BITS_START + 3] = (u_char)(ack >> 24);
	// 设置seq位
	seq = rand() % 65535;
	header[SEQ_BITS_START] = (u_char)(seq & 0xFF);
	header[SEQ_BITS_START + 1] = (u_char)(seq >> 8);
	header[SEQ_BITS_START + 2] = (u_char)(seq >> 16);
	header[SEQ_BITS_START + 3] = (u_char)(seq >> 24);
	// 设置ACK SYN位
	header[FLAG_BIT_POSITION] = 0b110;
	// 设置checksum位
	checksum = checkSum(header, HEADERSIZE);
	header[CHECKSUM_BITS_START] = (u_char)(checksum & 0xFF);
	header[CHECKSUM_BITS_START + 1] = (u_char)(checksum >> 8);
	sendto(serverSocket, header, HEADERSIZE, 0, (SOCKADDR*)&clientAddr, sizeof(SOCKADDR));
	cout << "服务器端发送第二次握手" << endl;

	// 接受第三次握手请求报文
	while (true) {
		recvResult = recvfrom(serverSocket, recvBuf, HEADERSIZE, 0, (SOCKADDR*)&clientAddr, &len);
		// 检查checksum
		checksum = checkSum(recvBuf, HEADERSIZE);
		// 提取ack of message 3
		ack = (u_char)recvBuf[ACK_BITS_START] + ((u_char)recvBuf[ACK_BITS_START + 1] << 8)
			+ ((u_char)recvBuf[ACK_BITS_START + 2] << 16) + ((u_char)recvBuf[ACK_BITS_START + 3] << 24);

		if (checksum == 0 && ack == seq + 1 && recvBuf[FLAG_BIT_POSITION] == 0b100) {
			// 发送第一次握手应答报文
			memset(header, 0, HEADERSIZE);
			// 设置ack位，ack = seq of message 1 + 1
			ack = 3333;
			header[ACK_BITS_START] = (u_char)(ack & 0xFF);
			header[ACK_BITS_START + 1] = (u_char)(ack >> 8);
			header[ACK_BITS_START + 2] = (u_char)(ack >> 16);
			header[ACK_BITS_START + 3] = (u_char)(ack >> 24);
			sendto(serverSocket, header, HEADERSIZE, 0, (SOCKADDR*)&clientAddr, sizeof(SOCKADDR));
			cout << "第三次握手成功" << endl;
			break;
		}
		else {
			cout << "第三次握手失败" << endl;
			return false;
		}
	}
	cout << "握手成功结束！！！" << endl;
	cout << "=============================================握手结束================================================================" << endl;
	return true;
}

void wavehand() {
	u_short checksum = 0;
	char recvBuf[HEADERSIZE] = { 0 };
	int recvResult = 0;
	int seq, ack;
	// 接收第一次挥手请求报文，在recvfile()中已经接收了

	// 发送第二次挥手应答报文          
	// 设置seq位
	seq = rand();
	header[SEQ_BITS_START] = (u_char)(seq & 0xFF);
	header[SEQ_BITS_START + 1] = (u_char)(seq >> 8);
	header[SEQ_BITS_START + 2] = (u_char)(seq >> 16);
	header[SEQ_BITS_START + 3] = (u_char)(seq >> 24);
	// 设置ACK位
	header[FLAG_BIT_POSITION] = 0b100;
	sendto(serverSocket, header, HEADERSIZE, 0, (SOCKADDR*)&clientAddr, sizeof(SOCKADDR));
	cout << "发送第二次挥手消息" << endl;

	// 发送第三次挥手请求报文
	// 设置seq位
	seq = rand();
	header[SEQ_BITS_START] = (u_char)(seq & 0xFF);
	header[SEQ_BITS_START + 1] = (u_char)(seq >> 8);
	header[SEQ_BITS_START + 2] = (u_char)(seq >> 16);
	header[SEQ_BITS_START + 3] = (u_char)(seq >> 24);
	// ack和上一个报文一样
	// 设置ACK FIN位
	header[FLAG_BIT_POSITION] = 0b101;
	sendto(serverSocket, header, HEADERSIZE, 0, (SOCKADDR*)&clientAddr, sizeof(SOCKADDR));
	cout << "发送第三条挥手消息" << endl;

	// 接收第四次挥手应答报文
	while (true) {
		recvResult = recvfrom(serverSocket, recvBuf, HEADERSIZE, 0, (SOCKADDR*)&clientAddr, &len);
		// 检查checksum
		checksum = checkSum(recvBuf, HEADERSIZE);
		if (checksum == 0) {
			memset(header, 0, HEADERSIZE);
			int ack = 4444;
			header[ACK_BITS_START] = (u_char)(ack & 0xFF);
			header[ACK_BITS_START + 1] = (u_char)(ack >> 8);
			header[ACK_BITS_START + 2] = (u_char)(ack >> 16);
			header[ACK_BITS_START + 3] = (u_char)(ack >> 24);
			sendto(serverSocket, header, HEADERSIZE, 0, (SOCKADDR*)&clientAddr, sizeof(SOCKADDR));
			cout << "成功接收第四次挥手消息" << endl;
			break;
		}
		else {
			cout << "接收第四次挥手消息失败" << endl;
			return;
		}
	}

	cout << "结束 连接！！！" << endl;
	return;
}

void recvfile() {
	char recvBuf[PACKETSIZE] = { 0 }; // header + data
	char dataSegment[DATASIZE] = { 0 };
	char filename[FILE_NAME_MAX_LENGTH] = { 0 };
	int filesize = 0;
	int recvResult = 0; // 接受的packet总长度

	while (true) {
		// 接收文件名
		recvResult = recvfrom(serverSocket, recvBuf, PACKETSIZE, 0, (SOCKADDR*)&clientAddr, &len);

		// 检查是否是挥手报文
		if (recvBuf[FLAG_BIT_POSITION] == 0b101) {
			cout << "=============================================准备断连================================================================" << endl;
			// 记录一下seq
			for (int i = 0; i < 4; i++) {
				header[SEQ_BITS_START + i] = recvBuf[SEQ_BITS_START + i];
			}
			// 发送第一次握手应答报文
			memset(header, 0, HEADERSIZE);
			// 设置ack位，ack = seq of message 1 + 1
			int ack = 1111;
			header[ACK_BITS_START] = (u_char)(ack & 0xFF);
			header[ACK_BITS_START + 1] = (u_char)(ack >> 8);
			header[ACK_BITS_START + 2] = (u_char)(ack >> 16);
			header[ACK_BITS_START + 3] = (u_char)(ack >> 24);
			sendto(serverSocket, header, HEADERSIZE, 0, (SOCKADDR*)&clientAddr, sizeof(SOCKADDR));
			cout << "收到了第一次挥手的消息" << endl;
			wavehand();
			return;
		}

		// 提取header
		memcpy(header, recvBuf, HEADERSIZE);

		// 提取文件名
		if (header[FLAG_BIT_POSITION] == 0b1000) {
			memcpy(filename, recvBuf + HEADERSIZE, FILE_NAME_MAX_LENGTH);
		}

		// 接收文件大小
		recvResult = recvfrom(serverSocket, recvBuf, PACKETSIZE, 0, (SOCKADDR*)&clientAddr, &len);

		// 提取header
		memcpy(header, recvBuf, HEADERSIZE);

		// 提取文件大小
		if (header[FLAG_BIT_POSITION] == 0b10000) {
			filesize = atoi(recvBuf + HEADERSIZE);
		}
		cout << "开始接收文件, 文件名: " << filename << ", 文件大小 : " << filesize << " bytes." << endl;

		// 接收文件内容
		int hasReceived = 0; // 已接收字节数
		int seq_opp = 0, ack_opp = 0; // 对方发送报文中的seq, ack
		int seq = 0, ack = 0; // 要发送的响应报文中的seq, ack
		int dataLength = 0; // 接收到的数据段长度(= recvResult - HEADERSIZE)
		u_short checksum = 0; // 校验和（为0时正确）

		ofstream out;
		out.open(filename, ios::out | ios::binary | ios::app);
		while (true) {
			memset(recvBuf, 0, PACKETSIZE);
			memset(header, 0, HEADERSIZE);
			recvResult = recvfrom(serverSocket, recvBuf, PACKETSIZE, 0, (SOCKADDR*)&clientAddr, &len);
			if (recvResult == SOCKET_ERROR) {
				cout << "receive error! sleep!" << endl;
				std::this_thread::sleep_for(std::chrono::milliseconds(2000));
				continue;
			}

			// 检查校验和 and ACK位
			checksum = checkSum(recvBuf, recvResult);
			if (checksum == 0 && recvBuf[FLAG_BIT_POSITION] == 0b100) {
				seq_opp = (u_char)recvBuf[SEQ_BITS_START] + ((u_char)recvBuf[SEQ_BITS_START + 1] << 8)
					+ ((u_char)recvBuf[SEQ_BITS_START + 2] << 16) + ((u_char)recvBuf[SEQ_BITS_START + 3] << 24);
				ack_opp = (u_char)recvBuf[ACK_BITS_START] + ((u_char)recvBuf[ACK_BITS_START + 1] << 8)
					+ ((u_char)recvBuf[ACK_BITS_START + 2] << 16) + ((u_char)recvBuf[ACK_BITS_START + 3] << 24);
				if (seq_opp == ack) { // 检查收到的包的seq(即seq_opp)是否等于上一个包发过去的ack
					// 如果收到了正确的包，那就提取内容 + 回复
					dataLength = recvResult - HEADERSIZE;
					// 提取数据
					memcpy(dataSegment, recvBuf + HEADERSIZE, dataLength);
					out.write(dataSegment, dataLength);

					// 设置seq位，本协议中为了确认方便，就把响应报文的seq置为收到报文的seq
					seq = seq_opp;
					header[SEQ_BITS_START] = (u_char)(seq & 0xFF);
					header[SEQ_BITS_START + 1] = (u_char)(seq >> 8);
					header[SEQ_BITS_START + 2] = (u_char)(seq >> 16);
					header[SEQ_BITS_START + 3] = (u_char)(seq >> 24);
					// 设置ack位, = seq_opp + dataLength，表示确认接收到了这之前的全部内容，并期待收到这之后的内容
					ack = seq_opp + dataLength;
					header[ACK_BITS_START] = (u_char)(ack & 0xFF);
					header[ACK_BITS_START + 1] = (u_char)(ack >> 8);
					header[ACK_BITS_START + 2] = (u_char)(ack >> 16);
					header[ACK_BITS_START + 3] = (u_char)(ack >> 24);
					// 设置ACK位
					header[FLAG_BIT_POSITION] = 0b100;
					// 响应报文中的data length为0，就不用设置了

					hasReceived += recvResult - HEADERSIZE;
					cout << "has received " << hasReceived << " bytes, ack = " << ack << endl;
					sendto(serverSocket, header, HEADERSIZE, 0, (SOCKADDR*)&clientAddr, sizeof(SOCKADDR));
				}
				else {
					// 说明网络异常，丢了包，所以不用更改，直接重发上一个包的ack即可
					sendto(serverSocket, header, HEADERSIZE, 0, (SOCKADDR*)&clientAddr, sizeof(SOCKADDR));
					cout << "seq_opp != ack." << endl;
				}
			}
			else {
				// 校验和或ACK位异常，重发上一个包的ack
				sendto(serverSocket, header, HEADERSIZE, 0, (SOCKADDR*)&clientAddr, sizeof(SOCKADDR));
				cout << "checksum ERROR or ACK ERROR!" << endl;
				continue;
			}

			if (hasReceived == filesize) {
				cout << "receive file " << filename << " successfully! total " << hasReceived << " bytes." << endl;
				break;
			}
		}
		out.close();
	}
}

int main() {
	WSAData wsd;
	if (WSAStartup(MAKEWORD(2, 2), &wsd) != 0) {
		cout << "初始化失败" << endl;;
		exit(1);
	}
	else {
		cout << "初始化成功" << endl;
	}

	serverSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (serverSocket == SOCKET_ERROR) {
		cout << "serverSocket创建失败" << endl;
		closesocket(serverSocket);
		WSACleanup();
		exit(1);
	}
	else {
		cout << "serverSocket创建成功" << endl;
	}

	serverAddr.sin_family = AF_INET; // 协议版本 ipv4
	serverAddr.sin_addr.S_un.S_addr = inet_addr("127.0.0.1"); // ip地址，inet_addr把数点格式转换为整数
	serverAddr.sin_port = htons(5200); // 端口.5200

	clientAddr.sin_family = AF_INET;
	clientAddr.sin_port = htons(5300);
	clientAddr.sin_addr.s_addr = inet_addr("127.0.0.1");

	if (bind(serverSocket, (SOCKADDR*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
		cout << "绑定失败 " << endl;
		closesocket(serverSocket);
		WSACleanup();
		exit(1);
	}
	else {
		cout << "绑定成功" << endl;
	}

	if (handshake()) {
		thread recvfile_thread(recvfile);
		recvfile_thread.join();
	}

	closesocket(serverSocket);
	WSACleanup();
	return 0;
}
