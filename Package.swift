// swift-tools-version:5.7
// ZPlanKit v1.0.0 — clean-room reimplementation of ZPlan v1.03 (W.M. Smithers, 1997-98)
// Portable C core + Swift API. Builds for macOS, iOS, and as a command-line tool.
import PackageDescription

let package = Package(
    name: "ZPlanKit",
    platforms: [
        .macOS(.v12),
        .iOS(.v15),
    ],
    products: [
        .library(name: "ZPlanKit", targets: ["ZPlanKit"]),
        .library(name: "ZPlannerUI", targets: ["ZPlannerUI"]),
        .executable(name: "zplan", targets: ["zplan-cli"]),
    ],
    targets: [
        // Portable C99 decompression engine (ZH-L16C, VVAL-79, VPM-B)
        .target(
            name: "CZPlan",
            path: "Sources/CZPlan"
        ),
        // Swift API layer for macOS / iOS apps
        .target(
            name: "ZPlanKit",
            dependencies: ["CZPlan"],
            path: "Sources/ZPlanKit"
        ),
        // SwiftUI front end modelled on the original ZPlanner (Delphi) form
        .target(
            name: "ZPlannerUI",
            dependencies: ["ZPlanKit"],
            path: "Sources/ZPlannerUI"
        ),
        // Command-line front end (drop-in-ish for zplan.exe)
        .executableTarget(
            name: "zplan-cli",
            dependencies: ["CZPlan"],
            path: "Sources/zplan-cli"
        ),
    ]
)
