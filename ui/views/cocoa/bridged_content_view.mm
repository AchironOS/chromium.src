// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "ui/views/cocoa/bridged_content_view.h"

#include "base/logging.h"
#import "base/mac/scoped_nsobject.h"
#include "base/strings/sys_string_conversions.h"
#include "ui/base/ime/text_input_client.h"
#import "ui/events/cocoa/cocoa_event_utils.h"
#include "ui/events/keycodes/dom3/dom_code.h"
#import "ui/events/keycodes/keyboard_code_conversion_mac.h"
#include "ui/gfx/canvas_paint_mac.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/strings/grit/ui_strings.h"
#include "ui/views/controls/menu/menu_controller.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget.h"

using views::MenuController;

namespace {

// Convert a |point| in |source_window|'s AppKit coordinate system (origin at
// the bottom left of the window) to |target_window|'s content rect, with the
// origin at the top left of the content area.
// If |source_window| is nil, |point| will be treated as screen coordinates.
gfx::Point MovePointToWindow(const NSPoint& point,
                             NSWindow* source_window,
                             NSWindow* target_window) {
  NSPoint point_in_screen = source_window
      ? [source_window convertBaseToScreen:point]
      : point;

  NSPoint point_in_window = [target_window convertScreenToBase:point_in_screen];
  NSRect content_rect =
      [target_window contentRectForFrameRect:[target_window frame]];
  return gfx::Point(point_in_window.x,
                    NSHeight(content_rect) - point_in_window.y);
}

}

@interface BridgedContentView ()

// Translates the location of |theEvent| to toolkit-views coordinates and passes
// the event to NativeWidgetMac for handling.
- (void)handleMouseEvent:(NSEvent*)theEvent;

// Handles an NSResponder Action Message by mapping it to a corresponding text
// editing command from ui_strings.grd and, when not being sent to a
// TextInputClient, the keyCode that toolkit-views expects internally.
// For example, moveToLeftEndOfLine: would pass ui::VKEY_HOME in non-RTL locales
// even though the Home key on Mac defaults to moveToBeginningOfDocument:.
// This approach also allows action messages a user
// may have remapped in ~/Library/KeyBindings/DefaultKeyBinding.dict to be
// catered for.
// Note: default key bindings in Mac can be read from StandardKeyBinding.dict
// which lives in /System/Library/Frameworks/AppKit.framework/Resources. Do
// `plutil -convert xml1 -o StandardKeyBinding.xml StandardKeyBinding.dict` to
// get something readable.
- (void)handleAction:(int)commandId
             keyCode:(ui::KeyboardCode)keyCode
             domCode:(ui::DomCode)domCode
          eventFlags:(int)eventFlags;

@end

@implementation BridgedContentView

@synthesize hostedView = hostedView_;
@synthesize textInputClient = textInputClient_;

- (id)initWithView:(views::View*)viewToHost {
  DCHECK(viewToHost);
  gfx::Rect bounds = viewToHost->bounds();
  // To keep things simple, assume the origin is (0, 0) until there exists a use
  // case for something other than that.
  DCHECK(bounds.origin().IsOrigin());
  NSRect initialFrame = NSMakeRect(0, 0, bounds.width(), bounds.height());
  if ((self = [super initWithFrame:initialFrame])) {
    hostedView_ = viewToHost;

    // Apple's documentation says that NSTrackingActiveAlways is incompatible
    // with NSTrackingCursorUpdate, so use NSTrackingActiveInActiveApp.
    trackingArea_.reset([[CrTrackingArea alloc]
        initWithRect:NSZeroRect
             options:NSTrackingMouseMoved | NSTrackingCursorUpdate |
                     NSTrackingActiveInActiveApp | NSTrackingInVisibleRect
               owner:self
            userInfo:nil]);
    [self addTrackingArea:trackingArea_.get()];
  }
  return self;
}

- (void)clearView {
  hostedView_ = NULL;
  [trackingArea_.get() clearOwner];
  [self removeTrackingArea:trackingArea_.get()];
}

- (void)processCapturedMouseEvent:(NSEvent*)theEvent {
  if (!hostedView_)
    return;

  NSWindow* source = [theEvent window];
  NSWindow* target = [self window];
  DCHECK(target);

  // If it's the view's window, process normally.
  if ([target isEqual:source]) {
    [self handleMouseEvent:theEvent];
    return;
  }

  ui::MouseEvent event(theEvent);
  event.set_location(
      MovePointToWindow([theEvent locationInWindow], source, target));
  hostedView_->GetWidget()->OnMouseEvent(&event);
}

// BridgedContentView private implementation.

- (void)handleMouseEvent:(NSEvent*)theEvent {
  if (!hostedView_)
    return;

  ui::MouseEvent event(theEvent);
  hostedView_->GetWidget()->OnMouseEvent(&event);
}

- (void)handleAction:(int)commandId
             keyCode:(ui::KeyboardCode)keyCode
             domCode:(ui::DomCode)domCode
          eventFlags:(int)eventFlags {
  if (!hostedView_)
    return;

  // If there's an active MenuController it gets preference, and it will likely
  // swallow the event.
  MenuController* menuController = MenuController::GetActiveInstance();
  if (menuController && menuController->owner() == hostedView_->GetWidget()) {
    if (menuController->OnWillDispatchKeyEvent(0, keyCode) ==
        ui::POST_DISPATCH_NONE)
      return;
  }

  // If there's an active TextInputClient, it ignores the key and processes the
  // logical editing action.
  if (commandId && textInputClient_ &&
      textInputClient_->IsEditingCommandEnabled(commandId)) {
    textInputClient_->ExecuteEditingCommand(commandId);
    return;
  }

  // Otherwise, process the action as a regular key event.
  ui::KeyEvent event(ui::ET_KEY_PRESSED, keyCode, domCode, eventFlags);
  hostedView_->GetWidget()->OnKeyEvent(&event);
}

// NSView implementation.

- (BOOL)acceptsFirstResponder {
  return YES;
}

- (void)setFrameSize:(NSSize)newSize {
  [super setFrameSize:newSize];
  if (!hostedView_)
    return;

  hostedView_->SetSize(gfx::Size(newSize.width, newSize.height));
}

- (void)drawRect:(NSRect)dirtyRect {
  // Note that BridgedNativeWidget uses -[NSWindow setAutodisplay:NO] to
  // suppress calls to this when the window is known to be hidden.
  if (!hostedView_)
    return;

  // If there's a layer, painting occurs in BridgedNativeWidget::OnPaintLayer().
  if (hostedView_->GetWidget()->GetLayer())
    return;

  gfx::CanvasSkiaPaint canvas(dirtyRect, false /* opaque */);
  hostedView_->GetWidget()->OnNativeWidgetPaint(&canvas);
}

- (NSTextInputContext*)inputContext {
  if (!hostedView_)
    return [super inputContext];

  // If a menu is active, and -[NSView interpretKeyEvents:] asks for the
  // input context, return nil. This ensures the action message is sent to
  // the view, rather than any NSTextInputClient a subview has installed.
  MenuController* menuController = MenuController::GetActiveInstance();
  if (menuController && menuController->owner() == hostedView_->GetWidget())
    return nil;

  return [super inputContext];
}

// NSResponder implementation.

- (void)keyDown:(NSEvent*)theEvent {
  // Convert the event into an action message, according to OSX key mappings.
  [self interpretKeyEvents:@[ theEvent ]];
}

- (void)mouseDown:(NSEvent*)theEvent {
  [self handleMouseEvent:theEvent];
}

- (void)rightMouseDown:(NSEvent*)theEvent {
  [self handleMouseEvent:theEvent];
}

- (void)otherMouseDown:(NSEvent*)theEvent {
  [self handleMouseEvent:theEvent];
}

- (void)mouseUp:(NSEvent*)theEvent {
  [self handleMouseEvent:theEvent];
}

- (void)rightMouseUp:(NSEvent*)theEvent {
  [self handleMouseEvent:theEvent];
}

- (void)otherMouseUp:(NSEvent*)theEvent {
  [self handleMouseEvent:theEvent];
}

- (void)mouseDragged:(NSEvent*)theEvent {
  [self handleMouseEvent:theEvent];
}

- (void)rightMouseDragged:(NSEvent*)theEvent {
  [self handleMouseEvent:theEvent];
}

- (void)otherMouseDragged:(NSEvent*)theEvent {
  [self handleMouseEvent:theEvent];
}

- (void)mouseMoved:(NSEvent*)theEvent {
  // Note: mouseEntered: and mouseExited: are not handled separately.
  // |hostedView_| is responsible for converting the move events into entered
  // and exited events for the view heirarchy.
  [self handleMouseEvent:theEvent];
}

- (void)scrollWheel:(NSEvent*)theEvent {
  if (!hostedView_)
    return;

  ui::MouseWheelEvent event(theEvent);
  hostedView_->GetWidget()->OnMouseEvent(&event);
}

////////////////////////////////////////////////////////////////////////////////
// NSResponder Action Messages. Keep sorted according NSResponder.h (from the
// 10.9 SDK). The list should eventually be complete. Anything not defined will
// beep when interpretKeyEvents: would otherwise call it.
// TODO(tapted): Make this list complete.

// The insertText action message forwards to the TextInputClient unless a menu
// is active.
- (void)insertText:(id)text {
  [self insertText:text replacementRange:NSMakeRange(NSNotFound, 0)];
}

// Selection movement and scrolling.

- (void)moveRight:(id)sender {
  [self handleAction:IDS_MOVE_RIGHT
             keyCode:ui::VKEY_RIGHT
             domCode:ui::DomCode::ARROW_RIGHT
          eventFlags:0];
}

- (void)moveLeft:(id)sender {
  [self handleAction:IDS_MOVE_LEFT
             keyCode:ui::VKEY_LEFT
             domCode:ui::DomCode::ARROW_LEFT
          eventFlags:0];
}

- (void)moveUp:(id)sender {
  [self handleAction:0
             keyCode:ui::VKEY_UP
             domCode:ui::DomCode::ARROW_UP
          eventFlags:0];
}

- (void)moveDown:(id)sender {
  [self handleAction:0
             keyCode:ui::VKEY_DOWN
             domCode:ui::DomCode::ARROW_DOWN
          eventFlags:0];
}

- (void)moveWordRight:(id)sender {
  [self handleAction:IDS_MOVE_WORD_RIGHT
             keyCode:ui::VKEY_RIGHT
             domCode:ui::DomCode::ARROW_RIGHT
          eventFlags:ui::EF_CONTROL_DOWN];
}

- (void)moveWordLeft:(id)sender {
  [self handleAction:IDS_MOVE_WORD_LEFT
             keyCode:ui::VKEY_LEFT
             domCode:ui::DomCode::ARROW_LEFT
          eventFlags:ui::EF_CONTROL_DOWN];
}

- (void)moveLeftAndModifySelection:(id)sender {
  [self handleAction:IDS_MOVE_LEFT_AND_MODIFY_SELECTION
             keyCode:ui::VKEY_LEFT
             domCode:ui::DomCode::ARROW_LEFT
          eventFlags:ui::EF_SHIFT_DOWN];
}

- (void)moveRightAndModifySelection:(id)sender {
  [self handleAction:IDS_MOVE_RIGHT_AND_MODIFY_SELECTION
             keyCode:ui::VKEY_RIGHT
             domCode:ui::DomCode::ARROW_RIGHT
          eventFlags:ui::EF_SHIFT_DOWN];
}

- (void)moveWordRightAndModifySelection:(id)sender {
  [self handleAction:IDS_MOVE_WORD_RIGHT_AND_MODIFY_SELECTION
             keyCode:ui::VKEY_RIGHT
             domCode:ui::DomCode::ARROW_RIGHT
          eventFlags:ui::EF_CONTROL_DOWN | ui::EF_SHIFT_DOWN];
}

- (void)moveWordLeftAndModifySelection:(id)sender {
  [self handleAction:IDS_MOVE_WORD_LEFT_AND_MODIFY_SELECTION
             keyCode:ui::VKEY_LEFT
             domCode:ui::DomCode::ARROW_LEFT
          eventFlags:ui::EF_CONTROL_DOWN | ui::EF_SHIFT_DOWN];
}

- (void)moveToLeftEndOfLine:(id)sender {
  [self handleAction:IDS_MOVE_TO_BEGINNING_OF_LINE
             keyCode:ui::VKEY_HOME
             domCode:ui::DomCode::HOME
          eventFlags:0];
}

- (void)moveToRightEndOfLine:(id)sender {
  [self handleAction:IDS_MOVE_TO_END_OF_LINE
             keyCode:ui::VKEY_END
             domCode:ui::DomCode::END
          eventFlags:0];
}

- (void)moveToLeftEndOfLineAndModifySelection:(id)sender {
  [self handleAction:IDS_MOVE_TO_BEGINNING_OF_LINE_AND_MODIFY_SELECTION
             keyCode:ui::VKEY_HOME
             domCode:ui::DomCode::HOME
          eventFlags:ui::EF_SHIFT_DOWN];
}

- (void)moveToRightEndOfLineAndModifySelection:(id)sender {
  [self handleAction:IDS_MOVE_TO_END_OF_LINE_AND_MODIFY_SELECTION
             keyCode:ui::VKEY_END
             domCode:ui::DomCode::END
          eventFlags:ui::EF_SHIFT_DOWN];
}

// Insertions and Indentations.

- (void)insertNewline:(id)sender {
  [self handleAction:0
             keyCode:ui::VKEY_RETURN
             domCode:ui::DomCode::ENTER
          eventFlags:0];
}

// Deletions.

- (void)deleteForward:(id)sender {
  [self handleAction:IDS_DELETE_FORWARD
             keyCode:ui::VKEY_DELETE
             domCode:ui::DomCode::DEL
          eventFlags:0];
}

- (void)deleteBackward:(id)sender {
  [self handleAction:IDS_DELETE_BACKWARD
             keyCode:ui::VKEY_BACK
             domCode:ui::DomCode::BACKSPACE
          eventFlags:0];
}

- (void)deleteWordForward:(id)sender {
  [self handleAction:IDS_DELETE_WORD_FORWARD
             keyCode:ui::VKEY_DELETE
             domCode:ui::DomCode::DEL
          eventFlags:ui::EF_CONTROL_DOWN];
}

- (void)deleteWordBackward:(id)sender {
  [self handleAction:IDS_DELETE_WORD_BACKWARD
             keyCode:ui::VKEY_BACK
             domCode:ui::DomCode::BACKSPACE
          eventFlags:ui::EF_CONTROL_DOWN];
}

// Cancellation.

- (void)cancelOperation:(id)sender {
  [self handleAction:0
             keyCode:ui::VKEY_ESCAPE
             domCode:ui::DomCode::ESCAPE
          eventFlags:0];
}

// Support for Services in context menus.
// Currently we only support reading and writing plain strings.
- (id)validRequestorForSendType:(NSString*)sendType
                     returnType:(NSString*)returnType {
  BOOL canWrite = [sendType isEqualToString:NSStringPboardType] &&
                  [self selectedRange].length > 0;
  BOOL canRead = [returnType isEqualToString:NSStringPboardType];
  // Valid if (sendType, returnType) is either (string, nil), (nil, string),
  // or (string, string).
  BOOL valid = textInputClient_ && ((canWrite && (canRead || !returnType)) ||
                                    (canRead && (canWrite || !sendType)));
  return valid ? self : [super validRequestorForSendType:sendType
                                              returnType:returnType];
}

// NSServicesRequests informal protocol.

- (BOOL)writeSelectionToPasteboard:(NSPasteboard*)pboard types:(NSArray*)types {
  DCHECK([types containsObject:NSStringPboardType]);
  if (!textInputClient_)
    return NO;

  gfx::Range selectionRange;
  if (!textInputClient_->GetSelectionRange(&selectionRange))
    return NO;

  base::string16 text;
  textInputClient_->GetTextFromRange(selectionRange, &text);
  return [pboard writeObjects:@[ base::SysUTF16ToNSString(text) ]];
}

- (BOOL)readSelectionFromPasteboard:(NSPasteboard*)pboard {
  NSArray* objects =
      [pboard readObjectsForClasses:@[ [NSString class] ] options:0];
  DCHECK([objects count] == 1);
  [self insertText:[objects lastObject]];
  return YES;
}

// NSTextInputClient protocol implementation.

- (NSAttributedString*)
    attributedSubstringForProposedRange:(NSRange)range
                            actualRange:(NSRangePointer)actualRange {
  base::string16 substring;
  if (textInputClient_) {
    gfx::Range textRange;
    textInputClient_->GetTextRange(&textRange);
    gfx::Range subrange = textRange.Intersect(gfx::Range(range));
    textInputClient_->GetTextFromRange(subrange, &substring);
    if (actualRange)
      *actualRange = subrange.ToNSRange();
  }
  return [[[NSAttributedString alloc]
      initWithString:base::SysUTF16ToNSString(substring)] autorelease];
}

- (NSUInteger)characterIndexForPoint:(NSPoint)aPoint {
  NOTIMPLEMENTED();
  return 0;
}

- (void)doCommandBySelector:(SEL)selector {
  if ([self respondsToSelector:selector])
    [self performSelector:selector withObject:nil];
  else
    [[self nextResponder] doCommandBySelector:selector];
}

- (NSRect)firstRectForCharacterRange:(NSRange)range
                         actualRange:(NSRangePointer)actualRange {
  NOTIMPLEMENTED();
  return NSZeroRect;
}

- (BOOL)hasMarkedText {
  return textInputClient_ && textInputClient_->HasCompositionText();
}

- (void)insertText:(id)text replacementRange:(NSRange)replacementRange {
  if (!hostedView_)
    return;

  if ([text isKindOfClass:[NSAttributedString class]])
    text = [text string];

  MenuController* menuController = MenuController::GetActiveInstance();
  if (menuController && menuController->owner() == hostedView_->GetWidget()) {
    // Handle menu mnemonics (e.g. "sav" jumps to "Save"). Handles both single-
    // characters and input from IME. For IME, swallow the entire string unless
    // the very first character gives ui::POST_DISPATCH_PERFORM_DEFAULT.
    bool swallowedAny = false;
    for (NSUInteger i = 0; i < [text length]; ++i) {
      if (!menuController ||
          menuController->OnWillDispatchKeyEvent([text characterAtIndex:i],
                                                 ui::VKEY_UNKNOWN) ==
              ui::POST_DISPATCH_PERFORM_DEFAULT) {
        if (swallowedAny)
          return;  // Swallow remainder.
        break;
      }
      swallowedAny = true;
      // Ensure the menu remains active.
      menuController = MenuController::GetActiveInstance();
    }
  }

  if (!textInputClient_)
    return;

  textInputClient_->DeleteRange(gfx::Range(replacementRange));
  textInputClient_->InsertText(base::SysNSStringToUTF16(text));
}

- (NSRange)markedRange {
  if (!textInputClient_)
    return NSMakeRange(NSNotFound, 0);

  gfx::Range range;
  textInputClient_->GetCompositionTextRange(&range);
  return range.ToNSRange();
}

- (NSRange)selectedRange {
  if (!textInputClient_)
    return NSMakeRange(NSNotFound, 0);

  gfx::Range range;
  textInputClient_->GetSelectionRange(&range);
  return range.ToNSRange();
}

- (void)setMarkedText:(id)text
        selectedRange:(NSRange)selectedRange
     replacementRange:(NSRange)replacementRange {
  if (!textInputClient_)
    return;

  if ([text isKindOfClass:[NSAttributedString class]])
    text = [text string];
  ui::CompositionText composition;
  composition.text = base::SysNSStringToUTF16(text);
  composition.selection = gfx::Range(selectedRange);
  textInputClient_->SetCompositionText(composition);
}

- (void)unmarkText {
  if (textInputClient_)
    textInputClient_->ConfirmCompositionText();
}

- (NSArray*)validAttributesForMarkedText {
  return @[];
}

// NSAccessibility informal protocol implementation.

- (id)accessibilityAttributeValue:(NSString*)attribute {
  if ([attribute isEqualToString:NSAccessibilityChildrenAttribute]) {
    return @[ hostedView_->GetNativeViewAccessible() ];
  }

  return [super accessibilityAttributeValue:attribute];
}

- (id)accessibilityHitTest:(NSPoint)point {
  return [hostedView_->GetNativeViewAccessible() accessibilityHitTest:point];
}

@end
