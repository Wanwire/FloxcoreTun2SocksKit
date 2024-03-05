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
	url: "https://github.com/Wanwire/FloxcoreTun2SocksKit/releases/download/2.6.7/HevSocks5Tunnel.xcframework.zip",
	checksum: "d3850a0d764c594c262e3296f0a3e583b54a4f03d53be0d523a37f7244b74ead"
    )
  ]
)
