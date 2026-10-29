// Prints how many on-screen windows belong to the process with the given PID.
// Used by smoke_test.sh. Window owners are visible without the Screen
// Recording permission, which window titles would need.

import CoreGraphics
import Foundation

guard CommandLine.arguments.count == 2, let pid = Int(CommandLine.arguments[1]) else {
    FileHandle.standardError.write("usage: count_windows.swift PID\n".data(using: .utf8)!)
    exit(2)
}

let windows = CGWindowListCopyWindowInfo([.optionOnScreenOnly], kCGNullWindowID) as? [[String: Any]] ?? []
let count = windows.filter { ($0[kCGWindowOwnerPID as String] as? Int) == pid }.count
print(count)
