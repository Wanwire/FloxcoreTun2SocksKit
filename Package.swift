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
	url: "https://github.com/Wanwire/FloxcoreTun2SocksKit/releases/download/2.6.2/HevSocks5Tunnel.xcframework.zip",
	checksum: "0132d4ae6f5580a682ec1dbf11501feb279651feca8e7c4c3e5339e0f6991fc1"
    )
  ]
)
