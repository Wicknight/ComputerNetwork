import select
import socket

from main import Host


class SR:

    def __init__(self, local_address=Host.host_address_1, remote_address=Host.host_address_2):
        self.server_window_size = 4  # 窗口尺寸
        self.send_base = 0  # 窗口的起始位置
        self.next_seq = 0  # 下一个分组序号
        self.time_out = 5  # 设置超时时间
        self.local_address = local_address  # 设置本地socket地址
        self.remote_address = remote_address  # 设置远程socket地址
        self.socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.socket.bind(self.local_address)  # 绑定套接字的本地IP地址和端口号
        self.data = []  # 缓存发送数据
        self.read_path = 'file/send.txt'  # 需要发送的源文件数据
        self.ack_buf_size = 1200 # 服务器接收返回信息时数组的大小，由于数据是1024大小，所以预留出一些空间
        self.readfile()

        self.client_window_size = 4  # 接受窗口尺寸
        self.data_buf_size = 1200  # 作为客户端接收数据缓存，由于数据是1024大小，所以预留出一些空间
        self.save_path = 'file/receive.txt'  # 接收数据时，保存数据的地址
        self.writefile('', mode='w')

        self.time_counts = {}  # 字典类型，存储窗口中每个发出序号的时间,value类型为整数，计时
        self.ack_seqs = {}  # 字典类型，储存窗口中每个序号的ack情况，value类型为boolean,标识是否收到对应ack

        self.rcv_base = 0  # 最小的需要接收的数据的分组序号
        self.rcv_data = {}  # 字典类型，缓存失序的数据分组

    # 若仍剩余窗口空间，则构造数据报发送；否则拒绝发送数据
    def send_data(self):
        if self.next_seq == len(self.data):  # 判断是否还有缓存数据可以发送
            print('服务器:发送完毕，等待确认'+'\n')
            return
        if self.next_seq < self.send_base + self.server_window_size:  # 窗口中仍有可用空间
            #每5次模拟一次分组丢失
            if (not self.next_seq%5==0):
                self.socket.sendto(Host.make_packet(self.next_seq, self.data[self.next_seq]),self.remote_address)
                print('服务器:成功发送数据' + str(self.next_seq)+'\n')
            else:
                print('服务器:模拟数据丢失' + str(self.next_seq)+'\n')
            self.time_counts[self.next_seq] = 0  # 设置计时器,开始计时
            self.ack_seqs[self.next_seq] = False  # 设置为未接受确认包
            self.next_seq += 1
        else:  # 窗口中无可用空间
            print('服务器:窗口已满，暂不发送数据'+'\n')

    # 超时处理函数：计时器置0，设为未接受ACK，同时发送该序列号数据
    def Time_out(self, time_out_seq):
        print('超时,服务器进行重传:分组' + str(time_out_seq)+'\n')
        self.time_counts[time_out_seq] = 0  # 重新计时
        self.socket.sendto(Host.make_packet(time_out_seq, self.data[time_out_seq]),self.remote_address)

    # 从文件中读取数据，并存储到data属性里
    def readfile(self):
        f = open(self.read_path, 'r', encoding='utf-8')
        while True:
            send_data = f.read(1024)  # 一次读取1024个字节（如果有这么多）
            if len(send_data) <= 0:
                break
            self.data.append(send_data)  # 将读取到的数据保存到data数据结构中

    # 服务器滑动窗口，用于接收到最小的ack后调用
    def slide_server_window(self):
        #从当前窗口起始位置开始滑动，跳过所有为ack为true(正确接收数据)的窗口位置，滑动到第一个未接受分组的位置
        while self.ack_seqs.get(self.send_base):
            del self.ack_seqs[self.send_base]  # 从记录ack的字典中删除已接受的分组部分
            del self.time_counts[self.send_base]  # 从计时器的字典中删除已接受的分组部分
            self.send_base = self.send_base + 1  # 不断向后滑动
            print('服务器:窗口滑动到' + str(self.send_base)+'\n')

    def server_run(self):
        while True:
            self.send_data()  # 发送数据
            #发送数据以后等待回复
            #阻塞模式通信，但可利用该函数模拟非阻塞模式进行计时
            #参数1就表示如果未监听到变化，阻塞1秒，返回值空，此时完成计时器+1
            readable = select.select([self.socket], [], [], 1)[0]
            if len(readable) > 0:  # 接收ACK数据
                #提取ACK信息
                rcv_ack = self.socket.recvfrom(self.ack_buf_size)[0].decode().split()[0]
                #收到的ACK在当前窗口内
                if self.send_base <= int(rcv_ack) < self.next_seq:  # 收到窗口内分组的ack，则标记为已确认且超时计数为0
                    print('服务器:收到窗口内ACK' + rcv_ack+'\n')
                    self.ack_seqs[int(rcv_ack)] = True  # 确认接收分组
                    #收到当前窗口起始位置序号的ACK，则可以进行窗口滑动;反之不能滑动
                    if self.send_base == int(rcv_ack):  # 收到的ack为最小的窗口序号
                        self.slide_server_window()  
                else:
                    print('服务器:收到窗口外ACK' + rcv_ack+'\n')
            #无论是未接受到分组select阻塞，还是接收到一个分组，因为是单独计时，都要对未收到的分组计时+1
            for seq in self.time_counts.keys():  # 每个未接收的分组的时长都加1,进行计时
                if not self.ack_seqs[seq]:  # 若未收到ACK
                    self.time_counts[seq] += 1  # 计时+1
                    if self.time_counts[seq] > self.time_out:  # 触发超时操作
                        self.Time_out(seq)  # 超时处理
            if self.send_base == len(self.data):  # 数据传输结束
                self.socket.sendto(Host.make_packet(0, 0), self.remote_address)  # 发送传输结束包
                print('服务器:数据传输结束'+'\n')
                break

    # 保存来自服务器的数据
    def writefile(self, data, mode='a'):
        with open(self.save_path, mode, encoding='utf-8') as f:
            f.write(data)

    # 主要执行函数，不断接收服务器发送的数据，若失序则保存到缓存；若按序则滑动窗口；否则丢弃
    def client_run(self):
        #用一个list数据结构来记录丢失过的ACK
        miss_ack=[]
        while True:
            readable = select.select([self.socket], [], [], 1)[0]  
            if len(readable) > 0:
                rcv_data = self.socket.recvfrom(self.data_buf_size)[0].decode()
                rcv_seq = rcv_data.split()[0]  # 提取数据包序号
                rcv_data = rcv_data.replace(rcv_seq + ' ', '')  # 提取数据包数据
                if rcv_seq == '0' and rcv_data == '0':  # 收到传输数据结束的标志
                    print('客户端:传输数据结束\n')
                    break
                print('客户端:收到数据' + rcv_seq+'\n')
                #处理ack丢失以及按序、乱序数据到达
                if self.rcv_base - self.client_window_size <= int(rcv_seq) < self.rcv_base + self.client_window_size:
                    if self.rcv_base < int(rcv_seq) < self.rcv_base + self.client_window_size:  #窗口内但不是expect，乱序到达的数据
                        self.rcv_data[int(rcv_seq)] = rcv_data  # 失序的数据到来:缓存+发送ack
                    elif int(rcv_seq) == self.rcv_base:  # 按序数据的到来:滑动窗口并交付数据(清除对应的缓冲区)
                        self.rcv_data[int(rcv_seq)] = rcv_data
                        self.slide_client_window()
                    
                    #无论如何都是发送ack(n)
                    #窗口内分组，则直接发送ack(n)
                    #若是小于窗口base的分组n,也是发送ack(n)因为再次收到之前分组的重传说明是之前ack丢失，重新发送ack
                    #每8次模拟一个丢失ACK
                    if(not int(rcv_seq)%8==0):
                        self.socket.sendto(Host.make_packet(int(rcv_seq), 0), self.remote_address)
                        print('客户端:发送ACK:' + rcv_seq+'\n')
                    
                    #miss_count对应的值不为空，说明之前已经模拟丢失过一次，因此第二次重传，避免不断模拟丢失死循环
                    elif(rcv_seq in miss_ack):
                        self.socket.sendto(Host.make_packet(int(rcv_seq), 0), self.remote_address)
                        miss_ack.remove(rcv_seq)
                        print('客户端:重传ACK:' + rcv_seq+'\n')
                        
                    #模拟ACK丢失
                    else:
                        miss_ack.append(rcv_seq)
                        print('客户端:模拟ACK丢失:' + rcv_seq+'\n')
                        
                    

    # 客户端滑动接收窗口:滑动rcv_base，向上层交付数据，并清除已交付数据的缓存
    def slide_client_window(self):
        while self.rcv_data.get(self.rcv_base) is not None:  # 循环直到找到第一个未接受的数据包
            self.writefile(self.rcv_data.get(self.rcv_base))  # 写入文件
            del self.rcv_data[self.rcv_base]  # 清除该缓存
            self.rcv_base = self.rcv_base + 1  # 滑动窗口
            print('客户端:窗口滑动到' + str(self.rcv_base)+'\n')
