// swift-tools-version: 5.9
import PackageDescription

let package = Package(
    name: "KeyScribeNative",
    platforms: [.macOS(.v13)],
    products: [.executable(name: "KeyScribe", targets: ["KeyScribe"])],
    dependencies: [
        .package(url: "https://github.com/sparkle-project/Sparkle", exact: "2.10.0"),
    ],
    targets: [
        .executableTarget(
            name: "KeyScribe",
            dependencies: [.product(name: "Sparkle", package: "Sparkle")],
            linkerSettings: [
                // 앱 번들의 Contents/Frameworks에 넣은 Sparkle.framework를 찾는다.
                .unsafeFlags(["-Xlinker", "-rpath", "-Xlinker", "@executable_path/../Frameworks"]),
                .linkedFramework("AppKit"),
                .linkedFramework("AVFoundation"),
                .linkedFramework("ApplicationServices"),
                .linkedFramework("CoreGraphics"),
                .linkedFramework("QuartzCore"),
            ]
        ),
    ]
)
