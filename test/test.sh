for f in udp_server udp_client tcp_server tcp_client;do
    echo compile $f
    g++ $f.cpp -lpthread -o $f
done

ifconfig eth0:0 10.10.10.20
ip rule add fwmark 1 table 100
ip route add local default table 100 dev lo
nft -f nfttest.conf
../build/netsock -c server_test.conf &
../build/netsock -c tproxy.conf &

echo start tcp server test
./tcp_server 10180 &
echo test |./tcp_client 10.10.10.20 10180 > tcpout &

echo start udp server test
./udp_server 10180 &
echo test |./udp_client 10.10.10.20 10180 > udpout &

echo sleep 1 to stop job
sleep 1
jobs
for j in $(jobs |sed -n 's/\[\([0-9]\)\].*/\1/p') ;do
    kill %$j
done

ifconfig eth0:0 down
nft flush ruleset
ip rule del fwmark 1
ip route del local default table 100 dev lo

if [ "$(cat tcpout)" != "Enter message: Reply is: test" ] ;then
    echo "TCP test fail!"
    exit 1
fi
if [ "$(cat udpout)" != "Enter message: Reply is: test" ] ;then
    echo "UDP test fail!"
    exit 1
fi
