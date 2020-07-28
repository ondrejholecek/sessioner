# Sessioner

Creates and maintains specific number of TCP sessions between two hosts. The number of sessions can be dynamically adjusted when it is running.

## Usage 

Sessioner consists of two binaries - `sessioner_initiator` and `sessioner_responder`. 
- Responder show be started first because it listens on configured TCP ports and accepts the connections from initiator.
- Initiator then establishes the configured number of connections to responder and maintains them.

After started, each program shows its own prompt for getting runtime status and changing the number of required connections.

Impatients can jump directly to [Example section](#example).

## sessioner_responder

### Parameters

```
Usage: ./sessioner_responder [-dh] --listen ip:port[-port]
       -h         ... show this help
       -d         ... enable debug outputs
       -u         ... do not try to change resource limits on file descriptors
       --listen   ... listen on IP address and port
                      optionally port range can be specified
                      this option can appear multiple times
       --hashsize ... size of primary hashing table for clients
```

- `-d` Enable debug outputs. This can generate a lot of data for high number of connections.
- `-u` Normally the program tries to read the system limit of maximum open file descriptor per process and sets this limit. This option disables it. Be aware
       that default limit of FD is usually 1024 which is not enough for any serious testing.
-  `--hashsize` Size of primary hashing table (which is used for all clients from all listeners), default is 10,000 which is enough for about 100,000 connected
                clients. When you are testing with more clients you will need to increase this appropriately otherwise accepting new clients will be quite slow.
- `--listen` The most important and the only required parameter. It specified where the responder should wait for new connections. It can be specified many times.
   It can be as simple as `10.0.0.1:5000` or it can be a continous range of ports to listen on like `10.0.0.1:5000-5999`. Reason for using multiple listening
   ports is that for two hosts and one listening port there are usually only about 28,000 possible connections (see [Ports selection](#port-selection) section).
   
### Commands

Reponder's prompt is `responder $ ` and you can type `help` to list available commands. Following are currently recognized:

- `status` Shows some statistics counter. Most important is `Connected clients` which shows the number of connected clients from responder's point of view.
```
responder $ status
Connected clients        : 15
Active listeners         : 1
Clients hash size        : 100000
Clients hash max depth   : 1
Listeners hash max depth : 1
Max descriptors limit    : 1048576/1048576
Debug output             : no
```

- `list` Shows basic IP information about every client.
```
responder $ list
Connected clients according to internal hash structure: 15
127.0.0.1:49496->127.0.0.1:6000 : last action 107 seconds ago
127.0.0.1:49498->127.0.0.1:6000 : last action 93 seconds ago
127.0.0.1:49500->127.0.0.1:6000 : last action 93 seconds ago
127.0.0.1:49502->127.0.0.1:6000 : last action 93 seconds ago
127.0.0.1:49504->127.0.0.1:6000 : last action 93 seconds ago
127.0.0.1:49506->127.0.0.1:6000 : last action 93 seconds ago
127.0.0.1:49508->127.0.0.1:6000 : last action 93 seconds ago
127.0.0.1:49510->127.0.0.1:6000 : last action 93 seconds ago
127.0.0.1:49512->127.0.0.1:6000 : last action 93 seconds ago
127.0.0.1:49514->127.0.0.1:6000 : last action 93 seconds ago
127.0.0.1:49516->127.0.0.1:6000 : last action 60 seconds ago
127.0.0.1:49518->127.0.0.1:6000 : last action 60 seconds ago
127.0.0.1:49520->127.0.0.1:6000 : last action 60 seconds ago
127.0.0.1:49522->127.0.0.1:6000 : last action 60 seconds ago
127.0.0.1:49524->127.0.0.1:6000 : last action 60 seconds ago
```

- `listeners` Shows basic information about every configured listener, including the number of clients it has accepted since start.
```
responder $ listeners
Active listeners according to internal hash structure: 1
127.0.0.1:6000 : 28247 accepted connections
```

- `debug` Enable or disable printing the debug output.
```
responder $ debug
Debug output enabed
responder $ debug
Debug output disabled
```

- `quit` Terminate all the connections and the responder.

Pressing enter without typing any command will just repeat the last command.

## sessioner_initiator

### Parameters

```
Usage: ./sessioner_initiator [-dph] --target ip:port[-port] [--hashsize NUM]
       -h         ... show this help
       -d         ... enable debug outputs
       -p         ... ping through connections periodically
       -u         ... do not try to change resource limits on file descriptors
       --target   ... connect on IP address and port
                      optionally port range can be specified
                      this option can appear multiple times
       --hashsize ... size of primary hashing table for following targets
```

- `-d` Enable debug outputs. This can generate a lot of data for high number of connections.
- `-u` Normally the program tries to read the system limit of maximum open file descriptor per process and sets this limit. This option disables it. Be aware
       that default limit of FD is usually 1024 which is not enough for any serious testing.
- `-p` Enable sending an explicit "PING" packet though the session after some period of time. Currently the ping is sent every 30-60 seconds (randomized for
       every session).
- `--hashsize` Similar to the same parameter in responder, but in initiator the session table is independent for every target (one IP and and port). Every
  `--target` configured after `--hashsize` parameters will have the specific size configured. It is usually not necessary to change the default (10,000).
- `--target` Specifies the remote IP and port where responder is listening. It can be specified many times. It can be as simple as `10.0.0.1:5000` or it 
             can be a continous range of ports to listen on like `10.0.0.1:5000-5999`. Reason for using multiple listening ports is that for two hosts and
             one listening port there are usually only about 28,000 possible connections (see [Ports selection](#port-selection) section).

### Commands

Initiator's prompt is `initiator $ ` and you can type `help` to list available commands. Following are currently recognized:

- `status` Shows some statistics counter. Most important is `Connections requested` which shows the desired total number of sessions and `Connected clients`
  which shown the actual number of established sessions. It can take some time for all the requested sessions to be established, but these two counters
  should eventualy match.
```
initiator $ status
Connections requested    : 40
Connected clients        : 40
Configured targets       : 4
Max descriptors limit    : 1048576/1048576
Ping through connections : no
Debug output             : no
```

- `details` Shows details about every target (single IP and port). Notice that the total number of clients is 40 and it is eqully distributed between
  all the targets.
```
initiator $ details
Target 127.0.0.1:6000:
   Maximum allowed connections       : 10
   Currently established connections : 10
   Session hash max depth            : 1
Target 127.0.0.1:6001:
   Maximum allowed connections       : 10
   Currently established connections : 10
   Session hash max depth            : 1
Target 127.0.0.1:6002:
   Maximum allowed connections       : 10
   Currently established connections : 10
   Session hash max depth            : 1
Target 127.0.0.1:6003:
   Maximum allowed connections       : 10
   Currently established connections : 10
   Session hash max depth            : 1
```

- `set NUM` Sets the total number of sessions to keep established. The sessions are automatically distributed between all targets. If the new limit
  is lower than the previous one, some sessions are automatically closed.

```
initiator $ set 60
```

- `ping` Enable or disable sending explicit "PING" packet though every sessions from time to time. Currently the ping is sent every 30-60 seconds
  (randomized for every session).
```
initiator $ ping
Ping enabed
initiator $ ping
Ping disabled
```

- `debug` Toggle the debug output printing.
```
initiator $ debug
Debug output enabed
initiator $ debug
Debug output disabled
```

- `quit` Terminate all sessions and quit.

Pressing enter without typing any command will just repeat the last command.


## Other notes

### Port selection

When testing is happening between two hosts with one usable IP address on each and you specify ony one listening port, the maximal number of possible
sessions that can be established on generic Linux distribution is about 28,000. It is because of following sysctl setting:

```
# sysctl net.ipv4.ip_local_port_range
net.ipv4.ip_local_port_range = 32768	60999
```

This sysctl specifies the ports that can be used for outgoing connections. In the case there are 28,231 ports available. 

You can change this sysctl, but it is better to specify more ports and/or IP addresses. Then every combination of IP and port can establish about 28,000
sessions. For 1 million sessions you need about 40 combinations. 

### Maximum number of file descriptors

Both initiator and responder will try to configure the highest possible number of open file descriptors, unless `-u` parameter was specified.

This limit is specified in `/proc/sys/fs/nr_open` and is usually slighly above 1 million. 
```
# cat /proc/sys/fs/nr_open
1048576
```

That is why it is not possible to open more than roughly 1 million sessions between one initiator and responder.

## Example 

- Start responder on host testB. Listen on single IP address and one hundred ports from 3000 to 3099. In the status output we can see there are
  indeed 100 configured listeners and the are no connected clients yet.
```
root@testB:~# sessioner_responder --hashsize 100000 --listen 10.109.80.40:3000-3099
responder $ status
Connected clients        : 0
Active listeners         : 100
Clients hash size        : 100000
Clients hash max depth   : 0
Listeners hash max depth : 1
Max descriptors limit    : 1048576/1048576
Debug output             : no
```

- Start initiator on hostA. Configure 100 targets - all on IP 10.109.80.40 and ports from 3000 to 3099. After start the initiator establishes
  one session per target, therefore there is now 100 sessions established.
```
root@testA:~# sessioner_initiator --target 10.109.80.40:3000-3099
initiator $ status
Connections requested    : 100
Connected clients        : 100
Configured targets       : 100
Max descriptors limit    : 1048576/1048576
Ping through connections : no
Debug output             : no
```

- We can verify the same numbers of established sessions are seen on responder.
```
responder $ status
Connected clients        : 100
Active listeners         : 100
Clients hash size        : 100000
Clients hash max depth   : 1
Listeners hash max depth : 1
Max descriptors limit    : 1048576/1048576
Debug output             : no
```

- Now configure initiator to have 100,000 sessions established.
```
initiator $ set 100000
```

- It will take some time. We can monitor the progress by executing `status` command. Empty command will repeat the previous command.
```
initiator $ status
Connections requested    : 100000
Connected clients        : 2652
Configured targets       : 100
Max descriptors limit    : 1048576/1048576
Ping through connections : no
Debug output             : no
initiator $
Connections requested    : 100000
Connected clients        : 33900
Configured targets       : 100
Max descriptors limit    : 1048576/1048576
Ping through connections : no
Debug output             : no
initiator $
Connections requested    : 100000
Connected clients        : 63036
Configured targets       : 100
Max descriptors limit    : 1048576/1048576
Ping through connections : no
Debug output             : no
initiator $
Connections requested    : 100000
Connected clients        : 100000
Configured targets       : 100
Max descriptors limit    : 1048576/1048576
Ping through connections : no
Debug output             : no
```

- We can do the same monitoring on responder.
```
responder $ status
Connected clients        : 8650
Active listeners         : 100
Clients hash size        : 100000
Clients hash max depth   : 1
Listeners hash max depth : 1
Max descriptors limit    : 1048576/1048576
Debug output             : no
responder $
Connected clients        : 23300
Active listeners         : 100
Clients hash size        : 100000
Clients hash max depth   : 1
Listeners hash max depth : 1
Max descriptors limit    : 1048576/1048576
Debug output             : no
responder $
Connected clients        : 34836
Active listeners         : 100
Clients hash size        : 100000
Clients hash max depth   : 1
Listeners hash max depth : 1
Max descriptors limit    : 1048576/1048576
Debug output             : no
responder $
Connected clients        : 64073
Active listeners         : 100
Clients hash size        : 100000
Clients hash max depth   : 1
Listeners hash max depth : 1
Max descriptors limit    : 1048576/1048576
Debug output             : no
responder $
Connected clients        : 100000
Active listeners         : 100
Clients hash size        : 100000
Clients hash max depth   : 1
Listeners hash max depth : 1
Max descriptors limit    : 1048576/1048576
Debug output             : no
```

- Now we have 100,000 connected sessions.
