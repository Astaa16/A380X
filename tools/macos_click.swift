import CoreGraphics
import Foundation

guard CommandLine.arguments.count == 3,
      let x = Double(CommandLine.arguments[1]),
      let y = Double(CommandLine.arguments[2]) else {
    fputs("usage: macos_click x y\n", stderr)
    exit(2)
}

let point = CGPoint(x: x, y: y)
CGEvent(mouseEventSource: nil, mouseType: .mouseMoved, mouseCursorPosition: point, mouseButton: .left)?.post(tap: .cghidEventTap)
usleep(50_000)
CGEvent(mouseEventSource: nil, mouseType: .leftMouseDown, mouseCursorPosition: point, mouseButton: .left)?.post(tap: .cghidEventTap)
CGEvent(mouseEventSource: nil, mouseType: .leftMouseUp, mouseCursorPosition: point, mouseButton: .left)?.post(tap: .cghidEventTap)
