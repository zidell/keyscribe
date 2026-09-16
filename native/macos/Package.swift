// swift-tools-version: 5.9
import PackageDescription

let package = Package(
    name: "KeyScribeNative",
    platforms: [.macOS(.v13)],
    products: [.executable(name: "KeyScribe", targets: ["KeyScribe"])],
    targets: [
        .executableTarget(
            name: "KeyScribe",
            linkerSettings: [
                .linkedFramework("AppKit"),
                .linkedFramework("AVFoundation"),
                .linkedFramework("ApplicationServices"),
                .linkedFramework("CoreGraphics"),
                .linkedFramework("QuartzCore"),
            ]
        ),
    ]
)
