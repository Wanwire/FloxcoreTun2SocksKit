// swift-tools-version: 5.9

import PackageDescription

let package = Package(
  name: "Tun2SocksKit",
  platforms: [.iOS(.v16), .macOS(.v14), .macCatalyst(.v16), .tvOS(.v17)],
  products: [
    .library(
        name: "Tun2SocksKit",
        targets: ["Tun2SocksKit"]
    )
  ],
  targets: [
    .target(
        name: "Tun2SocksKit",
        dependencies: ["Tun2SocksKitC", "HevSocks5Tunnel"]
    ),
    .target(
        name: "Tun2SocksKitC",
        publicHeadersPath: "."
    ),
    .binaryTarget(
        name: "HevSocks5Tunnel",
	url: "https://github.com/Wanwire/FloxcoreTun2SocksKit/releases/download/2.7.5/HevSocks5Tunnel.xcframework.zip",
	checksum: "0711f385999737f7614ae72d856e8b3f0e505194957126e36e4d96dc9ceceaf9"
    )
  ]
)
