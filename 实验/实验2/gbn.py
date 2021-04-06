
import select
import socket

from main import Host


class GBN:

    def __init__(self, local_address=Host.host_address_1, remote_address=Host.host_address_2):
        self.window_size = 4  # 窗口尺寸
        self.send_base = 0  # 窗口的起始位置
        self.next_seq = 0  # 待发送的下一个分组的序号
        self.time_count = 0  # 当前传输时间
        self.time_out = 5  # 设置超时时间
        self.local_address = local_address  # 设置本地socket地址
        self.remote_address = remote_address  # 设置远程socket地址
        self.socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.socket.bind(self.local_address)  # 绑定套接字的本地IP地址和端口号
        self.data = []  # 缓存发送数据
        self.read_path = 'file/send.txt'  # 需要发送的源文件数据
        self.ack_buf_size = 1200 # 服务器接收返回信息时数组的大小，由于数据是1024大小，所以预留出一些空间
        self.readfile()

        self.data_buf_size = 1200  # 作为客户端接收数据时数组的大小，由于数据是1024大小，所以预留出一些空间
        self.exp_seq = 0  # 期望收到的分组序号
        self.save_path = 'file/receive.txt'  # 接收数据时，保存数据在文件中
        self.writefile('', mode='w')


    # 若仍剩余窗口空间，则构造数据报发送；否则拒绝发送数据
    def send_data(self):
        if self.next_seq == len(self.data):  # data数据已全部被发送过
            print('服务器:发送完毕，等待确认'+'\n')
            return
        if self.next_seq - self.send_base < self.window_size:  # 窗口中仍有可用空间
            if (not self.next_seq%5==0):  #每5次模拟丢失一个分组
                self.socket.sendto(Host.make_packet(self.next_seq, self.data[self.next_seq]),self.remote_address)
                print('服务器:成功发送数据' + str(self.next_seq)+'\n')
            else:
                print('服务器:模拟丢失数据' + str(self.next_seq)+'\n')
            self.next_seq = self.next_seq + 1
        else:  # 窗口中无可用空间
            print('服务器：窗口已满，暂不发送数据')

    # 超时处理函数：计时器置0
    def Time_out(self):
        print('超时，服务器进行重传'+'\n')
        self.time_count = 0  # 超时计次重启
        for i in range(self.send_base, self.next_seq):  #重新发送窗口中所有分组
            self.socket.sendto(Host.make_packet(i, self.data[i]), self.remote_address)
            print('服务器数据已重发:' + str(i)+'\n')

    # 从文本中读取数据用于模拟上层协议数据的到来
    def readfile(self):
        f = open(self.read_path, 'r', encoding='utf-8')
        while True:
            send_data = f.read(1024)  # 一次读取1024个字节（如果有这么多）
            if len(send_data) <= 0:
                break
            self.data.append(send_data)  # 将读取到的数据保存到data数据结构中

    # 服务器执行函数，不断发送数据并接收ACK报文做相应的处理(计时与窗口滑动)
    def server_run(self):
        while True:
            self.send_data()  #分组发送数据
            #阻塞模式通信，但可利用该函数模拟非阻塞模式进行计时
            #参数1就表示如果未监听到变化，阻塞1秒，返回值空，此时完成计时器+1
            readable = select.select([self.socket], [], [], 1)[0]
            if len(readable) > 0:  # 接收到数据
                #提取ACK信息
                rcv_ack = self.socket.recvfrom(self.ack_buf_size)[0].decode().split()[0]
                print('服务器收到客户端ACK:' + rcv_ack+'\n')
                
                #ack对之前所有分组进行确认，设置base为ack+1，窗口滑动
                self.send_base = int(rcv_ack) + 1  # 滑动窗口的起始序号
                
                self.time_count = 0  #重新开始计时
                
            else:  #进入该分支说明readable为空，select函数未监听到变化，此时select阻塞1秒，对应于计时器+1
                self.time_count += 1  #阻塞一次
                if self.time_count > self.time_out:  # 超时重传
                    self.Time_out()
            if self.send_base == len(self.data):  # 数据传输结束
                self.socket.sendto(Host.make_packet(0, 0), self.remote_address)  # 发送结束报文
                print('服务器:发送完毕'+'\n')
                break

    # 保存来自服务器的合适的数据
    def writefile(self, data, mode='a'):
        with open(self.save_path, mode, encoding='utf-8') as f:
            f.write(data)  # 模拟将数据交付到上层

    # 客户端执行函数，不断接收服务器数据，与期待序号相符，则保存，否则直接丢弃；返回相应的ACK报文
    def client_run(self):
        miss_ack=[]
        while True:
            
            readable = select.select([self.socket], [], [], 1)[0] 
            if len(readable) > 0:  # 接收到数据
                rcv_data = self.socket.recvfrom(self.data_buf_size)[0].decode()
                rcv_seq = rcv_data.split()[0]  # 获取分组序列号
                rcv_data = rcv_data.replace(rcv_seq + ' ', '')  # 获取数据
                if rcv_seq == '0' and rcv_data == '0':  # 接收到结束包
                    print('客户端:接受全部数据\n')
                    break
                if int(rcv_seq) == self.exp_seq:  # 接收到的分组是按序的
                    print('客户端:收到期望序号数据:' + str(rcv_seq)+'\n')
                    self.writefile(rcv_data)  # 保存服务器端发送的数据到本地文件中
                    self.exp_seq = self.exp_seq + 1  # 期望数据的序号更新
                else:
                    print('客户端:收到乱序数据，期望:' + str(self.exp_seq) + '实际:' + str(rcv_seq)+'\n')
                if (not self.exp_seq%8==0):  # 随机丢包发送数据
                    self.socket.sendto(Host.make_packet(self.exp_seq - 1, 0), self.remote_address)
                    print('客户端发送ACK:'+str(self.exp_seq - 1)+'\n')
                elif(self.exp_seq in miss_ack):
                    miss_ack.remove(self.exp_seq)
                    self.socket.sendto(Host.make_packet(self.exp_seq - 1, 0), self.remote_address)
                    print('客户端重传ACK:'+str(self.exp_seq - 1)+'\n')
                else:
                    miss_ack.append(self.exp_seq)
                    print('客户端模拟丢失ACK:'+str(self.exp_seq - 1)+'\n')
