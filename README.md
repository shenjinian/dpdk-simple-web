## 使用DPDK的简单web server

一个极高性能的简单web server，使用DPDK框架，可以用于极高并发但处理逻辑比较简单的环境，比如高考查分/物联网应用。

由于不处理/保存客户端TCP连接的状态信息，因此本服务器支持的并发数是无限的，仅仅受限于带宽。

以高考查分为例，假定一个省有100万(1M)个考生，每个考生的信息有200字节，全部数据是200MB字节。
这些信息读入内存用hash查找，可以在O(1)时间查到数据(用数据库太慢了)。每个考生的查询过程，可以精简到大约
15个数据包交互，全部考生查1次成绩大约需要15M个数据包，本程序的目标是每秒钟处理超过10M个数据包，也就是
说如果带宽足够，可以在1.5秒钟内让一个省考生查询1次成绩。实际受限于带宽，不能这么快。这些数据包预计有
2GB左右，如果是1Gbps的网络，大约20秒可以完成。

本服务器只能处理极其简单的请求: 仅仅处理用户发来的第一个TCP包中的请求（最好使用GET请求），
HTTP应答也最好不超过TCP MSS长度（也许将来可以实现IP包分片功能）。

已实现功能：

* 可以收到包

待实现功能：

* 响应ARP
* 响应ICMP echo
* 响应TCP SYN
* 响应HTTP GET

我的环境：(Ubuntu 17.10)

```
apt-get install libnuma-dev libcap-dev
cd /usr/src
wget https://fast.dpdk.org/rel/dpdk-17.11.tar.xz
xzcat dpdk-17.11.tar.xz | tar xvf -
cd dpdk-17.11
make config T=x86_64-native-linuxapp-gcc
sed -ri 's,(PMD_PCAP=).*,\1y,' build/.config
make
make -C examples RTE_SDK=$(pwd) RTE_TARGET=build O=$(pwd)/build/examples

mkdir -p /mnt/huge
mount -t hugetlbfs nodev /mnt/huge
echo 64 > /sys/devices/system/node/node0/hugepages/hugepages-2048kB/nr_hugepages

ip link set enp0s8 up    #enp0s8是一个独立的虚拟网卡，专用于DPDK

cd /usr/src/
git clone https://github.com/bg6cq/dpdk-simple-web.git
cd dpdk-simple-web
make
sh run.sh
```
