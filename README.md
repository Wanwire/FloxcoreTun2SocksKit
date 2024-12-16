# Tun2SocksKit

[![Release](https://github.com/Wanwire/FloxcoreTun2SocksKit/actions/workflows/release.yml/badge.svg)](https://github.com/Wanwire/FloxcoreTun2SocksKit/actions/workflows/release.yml)

[Download](https://github.com/Wanwire/FloxcoreTun2SocksKit/releases/latest "download latest release")

### Example configuration
```yml
tunnel:
  mtu: 9000

socks5:
  port: 7890
  address: ::1
  udp: 'udp'

misc:
  task-stack-size: 24576 # 20480 + 4096
  tcp-buffer-size: 4096
  connect-timeout: 5000
  read-write-timeout: 60000
  log-file: stderr
  log-level: debug
  limit-nofile: 65535
```

### Usage
```swift
import Tun2SocksKit

let config = "..."

// start
Socks5Tunnel.run(config: config)


// stop
Socks5Tunnel.quit()
```

