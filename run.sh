 mkdir -p /mnt/huge
 mount -t hugetlbfs nodev /mnt/huge
 echo 64 > /sys/devices/system/node/node0/hugepages/hugepages-2048kB/nr_hugepages
 
 ip link set enp0s8 up
 ./simple-web -c7 -n1 --vdev=net_pcap0,iface=enp0s8 -- -i --nb-cores=1 --nb-ports=1 --total-num-mbufs=2048
