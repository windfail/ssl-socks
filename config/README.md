* master router
  dnsmasq setup:
  1. init ipset
     ```
     ipset creat gfw hash:ip
     ```
  1. gfw list upstream work.lan, add to ipset
     ```
     server=/google.com/192.168.1.20
     ipset=/google.com/gfw
     ```
  2. ipset mark 1 prerouting
     ```
     iptables -t mangle -A PREROUTING -m set --match-set gfw -j MARK --set-mark 1
     ```
  3. route rule mark 1 route to work.lan
     `
     ip rule add fwmark 1 table 100
     ip route add default table 100 via work.lan
     `

* work.lan proxy
  dnsmasq setup:
  1. init nft set,add 8.8.8.8
     ```
     set gfw {
        typeof ip daddr
        element = { 8.8.8.8}
     }
     ```
  1. dnsmasq gfw list upstream 8.8.8.8, add to ipset
     ```
     server=/google.com/8.8.8.8
     nftset=/google.com/gfw
     ```
  2. ipset tproxy to port 1080 mark 1 prerouting
     ```
     chain input {
         type filter hook prerouting priority mangle ;
         ip daddr @gfw tproxy to :1080 meta mark set 1 accept
     }
     ```
  3. ipset-out mark 1 re-route
     ```
     chain output {
         type route hook output priority mangle;
         ip daddr @gfw meta mark set 1
     }
     ```
  3. route rule mark 1 route to local
     `
     ip rule add fwmark 1 table 100
     ip route add local default table 100 dev lo
     `
