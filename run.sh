 mkdir -p /mnt/huge
 mount -t hugetlbfs nodev /mnt/huge
 echo 64 > /sys/devices/system/node/node0/hugepages/hugepages-2048kB/nr_hugepages
 
 ip link set eth1 up
 ./simple-web -c1 -n1 --vdev=net_pcap0,iface=eth1 -- 222.195.81.233 80
