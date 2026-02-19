// swift-tools-version: 5.9

import PackageDescription

let package = Package(
  name: "Tun2SocksKit",
  platforms: [.iOS(.v16), .macOS(.v14), .macCatalyst(.v16), .tvOS(.v17), .visionOS(.v1)],
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
	url: "https://github.com/Wanwire/FloxcoreTun2SocksKit/releases/download/2026.02.19/HevSocks5Tunnel.xcframework.zip",
	checksum: "0fb4b0f3f9f34539f7f9f1ac0ee8f6508820d67eb3749432d2b7e3a9ffb61b92"
    )
  ]
)
