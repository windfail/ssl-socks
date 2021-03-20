

##server  
###raw_relay  
connect between client and local server, or between remote server and remote site

###ssl_relay  
tls connect between local server and remote server
`_relays`  each client's raw_relay has a session-value map in _relays



##flow
###local raw_relay
1. client start connect to local server, make new raw_relay
2. read from raw_relay, get connect cmd , add raw_relay to _relays, add start data(sock5 connect cmd) to ssl_relay buffer, if ssl_relay not start, start ssl_relay.
3. wait ssl_relay to start real relay:
   1. read from client, add data relay to ssl_relay buffer.
	  read error: close raw, tell ssl_relay to stop
   2. wait ssl_relay to send data to client.

###local ssl_relay
1. wait to start ssl_relay: connect to remote server and make tls handshake.
2. send ssl_relay buffer to remote server.
3. read from remote server
   1. if receive start_raw cmd, tell raw_relay to start real relay.
   2. if receive stop cmd, tell raw_relay to stop.
   3. if receive data, send data to raw_relay client.
	  no session, send stop to remote.
4. repeate 2-3

###remote ssl_relay
1. wait local ssl_relay to connect
2. read from local:
   1. start cmd, add new raw_relay to _relays, new_relay connect to remote site:
	  1. success, send start_raw to local.
	  2. fail, send stop to local.
   2. stop , tell raw_relay to stop.
   3. data, send data to raw_relay.
3. send ssl_relay buffer to local.
4. repeat 2-3

###remote raw_relay
1. read from raw, add data to ssl_relay buffer.
	  read error: close raw, tell ssl_relay to stop
2. wait ssl_relay to send data to remote site.
