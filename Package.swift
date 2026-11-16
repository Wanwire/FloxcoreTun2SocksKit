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
	url: "https://github.com/Wanwire/FloxcoreTun2SocksKit/releases/download/2026.11.15/HevSocks5Tunnel.xcframework.zip",
	checksum: "e5eda6fa6cb390c339271076fad41968742bab6b301c29177112dcdbf61c462d"
    )
  ]
)
