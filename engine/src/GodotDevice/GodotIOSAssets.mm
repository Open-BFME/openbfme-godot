// OpenBFME: iOS external-drive folder selection and security-scoped bookmarks.
#include "GodotDevice/GodotRelease.h"
#import <UIKit/UIKit.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>
#include <godot_cpp/classes/project_settings.hpp>

static NSURL *assetURL;
static NSString *assetState = @"idle";
static NSString *assetError = @"";
static NSString *bookmarkPath() {
    auto path = godot::ProjectSettings::get_singleton()->globalize_path("user://external-assets.bookmark").utf8();
    return [NSString stringWithUTF8String:path.get_data()];
}
static void acceptURL(NSURL *url) {
    if (assetURL) [assetURL stopAccessingSecurityScopedResource];
    assetURL = url;
    if (![url startAccessingSecurityScopedResource]) {
        assetState = @"error"; assetError = @"Could not access this folder. Choose it again in Files."; return;
    }
    NSError *error = nil;
    NSData *data = [url bookmarkDataWithOptions:NSURLBookmarkCreationMinimalBookmark includingResourceValuesForKeys:nil relativeToURL:nil error:&error];
    if (!data || ![data writeToFile:bookmarkPath() options:NSDataWritingAtomic error:&error]) {
        assetState = @"error"; assetError = error.localizedDescription ?: @"Could not remember folder access."; return;
    }
    assetState = @"selected"; assetError = @"";
}
@interface OpenBFMEAssetPicker : NSObject <UIDocumentPickerDelegate>
@end
@implementation OpenBFMEAssetPicker
- (void)documentPicker:(UIDocumentPickerViewController *)controller didPickDocumentsAtURLs:(NSArray<NSURL *> *)urls {
    if (urls.count) acceptURL(urls.firstObject); else assetState = @"cancelled";
}
- (void)documentPickerWasCancelled:(UIDocumentPickerViewController *)controller { assetState = @"cancelled"; }
@end
static OpenBFMEAssetPicker *assetDelegate;
namespace godot {
void InstallSetup::pick_external_assets() {
    if (![NSThread isMainThread]) {
        // UIKit must be presented on the main queue even when Godot is threaded.
        dispatch_async(dispatch_get_main_queue(), ^{ this->pick_external_assets(); });
        return;
    }
    if ([assetState isEqualToString:@"waiting"]) return;
    UIViewController *root = nil;
    for (UIScene *scene in UIApplication.sharedApplication.connectedScenes) {
        if (scene.activationState == UISceneActivationStateForegroundActive && [scene isKindOfClass:UIWindowScene.class])
            for (UIWindow *window in ((UIWindowScene *)scene).windows) if (window.isKeyWindow) root = window.rootViewController;
    }
    if (!root) {
        for (UIScene *scene in UIApplication.sharedApplication.connectedScenes)
            if ([scene isKindOfClass:UIWindowScene.class])
                for (UIWindow *window in ((UIWindowScene *)scene).windows)
                    if (window.rootViewController && !window.hidden) root = window.rootViewController;
    }
    while (root.presentedViewController) root = root.presentedViewController;
    if (!root) { assetState = @"error"; assetError = @"No active window for folder picker."; return; }
    assetDelegate = [OpenBFMEAssetPicker new];
    UIDocumentPickerViewController *picker = [[UIDocumentPickerViewController alloc] initForOpeningContentTypes:@[UTTypeFolder] asCopy:NO];
    picker.delegate = assetDelegate;
    picker.allowsMultipleSelection = NO;
    picker.shouldShowFileExtensions = YES;
    picker.modalPresentationStyle = UIModalPresentationFormSheet;
    assetState = @"waiting";
    [root presentViewController:picker animated:YES completion:nil];
}
Dictionary InstallSetup::external_assets() const {
    if ([assetState isEqualToString:@"idle"]) {
        NSData *data = [NSData dataWithContentsOfFile:bookmarkPath()];
        if (data) {
            BOOL stale = NO;
            NSError *error = nil;
            NSURL *url = [NSURL URLByResolvingBookmarkData:data options:0 relativeToURL:nil bookmarkDataIsStale:&stale error:&error];
            if (url) acceptURL(url);
            else { assetState = @"error"; assetError = error.localizedDescription ?: @"Reconnect the drive and choose its folder."; }
        }
    }
    Dictionary result;
    result["state"] = String([assetState UTF8String]);
    result["path"] = assetURL ? String([assetURL.path UTF8String]) : String();
    result["error"] = String([assetError UTF8String]);
    return result;
}
Dictionary InstallSetup::exclude_from_backup(const String &path) const {
    NSURL *url = [NSURL fileURLWithPath:[NSString stringWithUTF8String:path.utf8().get_data()] isDirectory:YES];
    NSError *error = nil;
    Dictionary result;
    result["ok"] = (bool)[url setResourceValue:@YES forKey:NSURLIsExcludedFromBackupKey error:&error];
    result["error"] = error ? String([error.localizedDescription UTF8String]) : String();
    return result;
}
}
