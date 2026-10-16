# iOS build

Requires a Mac with Xcode's iPhoneOS SDK, CMake, Ninja, Python 3, and the Godot 4.7 editor. The device target is ARM64, iOS 26 or newer. Build the macOS extension first (the editor uses it while importing scripts), then:

```sh
tools/release/build_ios.sh
python3 tools/release/package_ios.py
```

The extension is built as `godot/bin/openbfme.ios.template_debug.arm64.framework`. The exporter downloads the matching official iOS template into `engine/build-ios/ios.zip`, then creates `engine/build-ios/export/OpenBFME.xcodeproj`. Open that project in Xcode and select your development team and device for signing. A signed device build requires an Apple development identity and provisioning profile. Without a team ID, the export leaves the team fields empty. Set `--team-id` (or `APPLE_TEAM_ID`) if one is available.

This is a device build; it does not target the simulator. The desktop game's mouse/keyboard interface has not been adapted for touch. To include both installed games in the iOS app:

```sh
python3 tools/release/package_ios.py \
  --output-dir engine/build-ios/export-with-assets \
  --rotwk /path/to/RotWK --bfme2 /path/to/BFME2
```

Use this export's Xcode project for the build. Xcode copies its `retail` folder into the app as resources. Startup mounts `retail/rotwk` and `retail/bfme2` inside the app and refreshes loose movie/audio lookup paths, without showing the desktop path picker. Allow roughly 8 GiB per copy of the game data plus build products. Existing export projects are never overwritten; choose a new output directory to export again.

To produce an unsigned device app after exporting:

```sh
xcodebuild -project engine/build-ios/export/OpenBFME.xcodeproj \
  -scheme OpenBFME -configuration Debug -sdk iphoneos \
  -destination 'generic/platform=iOS' \
  -derivedDataPath engine/build-ios/DerivedData CODE_SIGNING_ALLOWED=NO build
```

Output: `engine/build-ios/DerivedData/Build/Products/Debug-iphoneos/OpenBFME.app`. An unsigned app cannot be installed directly on a device. The initial device ARM64 build and unsigned Xcode build passed; runtime behavior has not been tested on an iPhone or iPad.

### Smaller local package

Add `--compact-assets` alongside `--rotwk` and `--bfme2` to omit campaign cinematics, publisher videos, Windows executables and installer files. Required retail archives stay intact and menu movies remain. Campaign video playback is unavailable in this package. Current English installs produce about 5.83 GB of data plus the app. This still exceeds 4.36 GB available on an iPad.

If trimming an existing export, use **Product → Clean Build Folder** in Xcode before rebuilding so removed files do not remain in an old app bundle. Original installed game folders are never modified.

### External USB drive

Export without `--rotwk` / `--bfme2` to make a small app. On iPad, connect the drive and choose **Choose folder on drive**. In Apple's Files picker select `OpenBFME` (containing `retail/rotwk` and `retail/bfme2`) or `retail` itself. The app keeps the data on the drive and saves a security-scoped bookmark to restore access on later launches. Keep the drive connected while playing. If iPadOS cannot restore access, reconnect and choose the folder again.

The USB export for this setup is `engine/build-ios/export-usb-picker/OpenBFME.xcodeproj`. It uses a native directory picker rather than Godot's desktop path picker. External access and gameplay still require validation on the physical iPad.

USB exports enable `LSSupportsOpeningDocumentsInPlace` and `UIFileSharingEnabled`. On first launch (or when a bookmark cannot be restored), Apple’s native folder picker opens automatically. In **Browse → Locations → Dsik**, select `OpenBFME` or `OpenBFME/retail`. No copying into iPad storage is required. If cancelled, **Choose folder on drive** reopens the picker.
