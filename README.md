# Tun2SocksKit

[![Release](https://github.com/Wanwire/FloxcoreTun2SocksKit/actions/workflows/release.yml/badge.svg)](https://github.com/Wanwire/FloxcoreTun2SocksKit/actions/workflows/release.yml)

[Download](https://github.com/Wanwire/FloxcoreTun2SocksKit/releases/latest "download latest release")

### Usage
```swift
import Tun2SocksKit

Socks5Tunnel.run(withFileDescriptor: 4, configFilePath: localConfigFileURL.path(percentEncoded: false))
```

### Example configuration
```yml
tunnel:
  mtu: 9000

socks5:
  port: 7890
  address: ::1
  udp: 'udp'

misc:
  task-stack-size: 81920
  connect-timeout: 5000
  read-write-timeout: 60000
  log-file: stderr
  log-level: debug
  limit-nofile: 65535
```

