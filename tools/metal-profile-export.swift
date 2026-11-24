// Called by tools/metal-profile. Drive the English Xcode Metal debugger.
import AppKit
import ApplicationServices
import Carbon

func fail(_ message: String) -> Never {
  fputs("metal-profile: \(message)\n", stderr)
  exit(1)
}

func attribute(_ element: AXUIElement, _ name: String) -> CFTypeRef? {
  var value: CFTypeRef?
  AXUIElementCopyAttributeValue(element, name as CFString, &value)
  return value
}

func find(_ root: AXUIElement, _ key: String, _ value: String) -> AXUIElement? {
  if attribute(root, key) as? String == value { return root }
  // These collections can contain thousands of GPU commands or files.
  let role = attribute(root, "AXRole") as? String ?? ""
  if ["AXOutline", "AXTable", "AXBrowser", "AXList"].contains(role) {
    return nil
  }
  for child in attribute(root, "AXChildren") as? [AXUIElement] ?? [] {
    if let match = find(child, key, value) { return match }
  }
  return nil
}

func wait<T>(_ what: String, seconds: Double = 15, _ poll: () -> T?) -> T {
  let deadline = ProcessInfo.processInfo.systemUptime + seconds
  repeat {
    if let result = poll() { return result }
    Thread.sleep(forTimeInterval: 0.35)
  } while ProcessInfo.processInfo.systemUptime < deadline
  fail("timed out waiting for \(what). Inspect the trace window in Xcode.")
}

func press(_ control: AXUIElement) {
  guard AXUIElementPerformAction(control, kAXPressAction as CFString)
          == .success else { fail("could not press \(control)") }
}

func check(_ box: AXUIElement) {
  if attribute(box, "AXValue") as? Int == 0 { press(box) }
  let _: Bool = wait("checked profiling option") {
    attribute(box, "AXValue") as? Int == 1 ? true : nil
  }
}

func setText(_ field: AXUIElement, _ text: String) {
  guard AXUIElementSetAttributeValue(field, "AXValue" as CFString,
                                     text as CFString) == .success,
        attribute(field, "AXValue") as? String == text else {
    fail("could not set text field to \(text)")
  }
}

// Resolve Command-Shift-G for the current keyboard layout (yawl uses Dvorak).
func goToFolderKey() -> CGKeyCode {
  let source = TISCopyCurrentKeyboardLayoutInputSource().takeRetainedValue()
  guard let data = TISGetInputSourceProperty(source,
                                              kTISPropertyUnicodeKeyLayoutData)
    else { fail("keyboard layout has no Unicode key map") }
  let bytes = CFDataGetBytePtr(Unmanaged<CFData>.fromOpaque(data)
    .takeUnretainedValue())!
  return bytes.withMemoryRebound(to: UCKeyboardLayout.self, capacity: 1) {
    layout in
    for code: UInt16 in 0..<128 {
      var state: UInt32 = 0
      var char: UniChar = 0
      var length = 0
      if UCKeyTranslate(layout, code, UInt16(kUCKeyActionDisplay), 0,
                        UInt32(LMGetKbdType()), 0, &state, 1, &length,
                        &char) == noErr, length == 1, char == 103 {
        return code
      }
    }
    fail("cannot find the G key for Go to Folder")
  }
}

guard CommandLine.arguments.count == 3 else {
  fail("run tools/metal-profile INPUT.gputrace OUTPUT.gputrace")
}
guard AXIsProcessTrusted() else {
  fail("allow the launching terminal or agent in System Settings → " +
       "Privacy & Security → Accessibility, then retry")
}
let input = URL(fileURLWithPath: CommandLine.arguments[1])
  .resolvingSymlinksInPath()
let output = URL(fileURLWithPath: CommandLine.arguments[2]).standardizedFileURL
let folder = output.deletingLastPathComponent().resolvingSymlinksInPath()
let open = Process()
open.executableURL = URL(fileURLWithPath: "/usr/bin/open")
open.arguments = ["-a", "Xcode", input.path]
try open.run()
open.waitUntilExit()
guard open.terminationStatus == 0 else { fail("Xcode could not open the trace") }
let app = wait("Xcode") {
  NSRunningApplication.runningApplications(withBundleIdentifier:
    "com.apple.dt.Xcode").first
}
let xcode = AXUIElementCreateApplication(app.processIdentifier)
let window = wait("input trace document", seconds: 60) {
  (attribute(xcode, "AXWindows") as? [AXUIElement])?.first { window in
    guard let path = attribute(window, "AXDocument") as? String,
          let url = URL(string: path) else { return false }
    return url.resolvingSymlinksInPath().path == input.path
  }
}
func control(_ root: AXUIElement, _ key: String, _ label: String,
             seconds: Double = 15) -> AXUIElement {
  wait(label, seconds: seconds) { find(root, key, label) }
}

check(control(window, "AXIdentifier", "profileCheckbox"))
press(control(window, "AXTitle", "Replay"))
fputs("Profiling the frame in Xcode…\n", stderr)
// The Summary replaces "Collecting/Analyzing performance data" with this
// button when the profile is ready. Its Export opens the trace save panel.
_ = control(window, "AXDescription", "Show Performance", seconds: 300)
press(control(window, "AXTitle", "Export"))
let sheet = control(window, "AXIdentifier", "save-panel")
if let expand = find(sheet, "AXIdentifier", "NS_OPEN_SAVE_DISCLOSURE_TRIANGLE"),
   attribute(expand, "AXValue") as? Int == 0 {
  press(expand)
}
let embed = control(sheet, "AXTitle", "Embed performance data")
check(embed)
let name = control(sheet, "AXIdentifier", "saveAsNameTextField")
let basename = output.deletingPathExtension().lastPathComponent
setText(name, basename)

// Only the two standard save-panel shortcuts need keyboard events.
// Check the focused Xcode control immediately before sending each one.
func key(_ code: CGKeyCode, _ flags: CGEventFlags, in field: AXUIElement) {
  guard app.isActive,
        let focus = attribute(xcode, "AXFocusedUIElement"),
        CFEqual(focus, field) else {
    fail("Xcode save panel lost focus. Finish the export there.")
  }
  for down in [true, false] {
    guard let event = CGEvent(keyboardEventSource: nil, virtualKey: code,
                              keyDown: down) else { fail("no keyboard event") }
    event.flags = flags
    event.post(tap: .cghidEventTap)
  }
}
AXUIElementSetAttributeValue(name, "AXFocused" as CFString, kCFBooleanTrue)
key(goToFolderKey(), [.maskCommand, .maskShift], in: name)
let path = control(sheet, "AXIdentifier", "PathTextField")
setText(path, folder.path)
key(CGKeyCode(kVK_Return), [], in: path)
let _: Bool = wait("destination folder") {
  guard find(sheet, "AXIdentifier", "GoToWindow") == nil,
        let location = find(sheet, "AXIdentifier", "where popup"),
        attribute(location, "AXValue") as? String == folder.lastPathComponent
    else { return nil }
  return true
}
guard attribute(embed, "AXValue") as? Int == 1,
      attribute(name, "AXValue") as? String == basename,
      !FileManager.default.fileExists(atPath: output.path) else {
  fail("export settings changed or destination already exists")
}
press(control(sheet, "AXTitle", "Save"))
fputs("Writing \(output.path)…\n", stderr)
let _: Bool = wait("finished GPU trace archive", seconds: 300) {
  guard FileManager.default.fileExists(atPath: output.path),
        find(window, "AXRole", "AXSheet") == nil else { return nil }
  return true
}
if let stop = find(window, "AXTitle", "Stop GPU workload") { press(stop) }
