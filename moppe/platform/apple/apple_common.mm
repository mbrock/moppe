// Platform services shared by the macOS and iOS layers: asset
// resolution, monotonic time, speech, background work, and CoreText
// glyph rasterization for the font atlas.

#import <TargetConditionals.h>
#import <Foundation/Foundation.h>
#import <CoreText/CoreText.h>
#import <CoreGraphics/CoreGraphics.h>
#import <AVFoundation/AVFoundation.h>

#include <moppe/platform/platform.hh>

#include <chrono>
#include <sys/stat.h>

namespace moppe {
namespace platform {
  static bool file_exists (const std::string& p) {
    struct stat st;
    return ::stat (p.c_str (), &st) == 0;
  }

  std::string
  asset_path (const std::string& relative) {
    // 1. Explicit override for development.
    if (const char* base = ::getenv ("MOPPE_ASSETS")) {
      std::string p = std::string (base) + "/" + relative;
      if (file_exists (p))
	return p;
    }

    // 2. App bundle resources.
    NSString* res = [[NSBundle mainBundle] resourcePath];
    if (res) {
      std::string p = std::string (res.UTF8String) + "/" + relative;
      if (file_exists (p))
	return p;
    }

    // 3. Working directory (running from the repo root).
    if (file_exists (relative))
      return relative;

    // Fall through with the bundle path for a useful error message.
    if (res)
      return std::string (res.UTF8String) + "/" + relative;
    return relative;
  }

  double
  now () {
    using namespace std::chrono;
    return duration_cast<duration<double> >
      (steady_clock::now ().time_since_epoch ()).count ();
  }

  void
  say (const std::string& phrase) {
    // One long-lived synthesizer: a local would deallocate before
    // it finishes speaking.
    static AVSpeechSynthesizer* synth = [[AVSpeechSynthesizer alloc] init];
    AVSpeechUtterance* u = [AVSpeechUtterance
      speechUtteranceWithString:
	[NSString stringWithUTF8String: phrase.c_str ()]];
    [synth speakUtterance: u];
  }

  void
  async (void (*work) (void*), void (*done) (void*), void* ctx) {
    dispatch_queue_t q = dispatch_get_global_queue
      (QOS_CLASS_USER_INITIATED, 0);
    dispatch_async (q, ^{
      work (ctx);
      dispatch_async (dispatch_get_main_queue (), ^{
	done (ctx);
      });
    });
  }

  bool
  rasterize_glyph (const char* font_family, float point_size,
		   float scale, unsigned int codepoint,
		   GlyphBitmap& out) {
    CFStringRef name = CFStringCreateWithCString
      (NULL, font_family, kCFStringEncodingUTF8);
    CTFontRef font = CTFontCreateWithName (name, point_size * scale,
					   NULL);
    CFRelease (name);
    if (!font)
      return false;

    UniChar chars[2];
    CGGlyph glyphs[2];
    int nchars = 0;
    if (codepoint <= 0xFFFF) {
      chars[0] = (UniChar) codepoint;
      nchars = 1;
    } else {
      const unsigned int v = codepoint - 0x10000;
      chars[0] = (UniChar) (0xD800 + (v >> 10));
      chars[1] = (UniChar) (0xDC00 + (v & 0x3FF));
      nchars = 2;
    }
    if (!CTFontGetGlyphsForCharacters (font, chars, glyphs, nchars)) {
      CFRelease (font);
      return false;
    }

    CGGlyph glyph = glyphs[0];
    CGSize advance;
    CTFontGetAdvancesForGlyphs (font, kCTFontOrientationHorizontal,
				&glyph, &advance, 1);
    CGRect box;
    CTFontGetBoundingRectsForGlyphs (font,
				     kCTFontOrientationHorizontal,
				     &glyph, &box, 1);

    const int pad = 1;
    const int w = (int) ceil (box.size.width) + pad * 2;
    const int h = (int) ceil (box.size.height) + pad * 2;
    out.advance = (float) advance.width / scale;
    out.bearing_x = (int) floor (box.origin.x) - pad;
    // Distance from baseline up to the bitmap's top edge.
    out.bearing_y = (int) ceil (box.origin.y + box.size.height) + pad;
    out.width = w > pad * 2 ? w : 0;
    out.height = h > pad * 2 ? h : 0;
    if (out.width == 0 || out.height == 0) {
      // Space or other blank glyph.
      out.pixels.clear ();
      CFRelease (font);
      return true;
    }

    std::basic_string<unsigned char> pixels;
    pixels.resize ((size_t) w * h, 0);

    CGColorSpaceRef gray = CGColorSpaceCreateDeviceGray ();
    CGContextRef ctx = CGBitmapContextCreate
      (&pixels[0], w, h, 8, w, gray, kCGImageAlphaNone);
    CGColorSpaceRelease (gray);
    if (!ctx) {
      CFRelease (font);
      return false;
    }
    CGContextSetGrayFillColor (ctx, 1.0, 1.0);
    // Place the glyph so its bounding box lands inside the bitmap;
    // CG user space has a bottom-left origin, but the buffer's
    // row 0 is already the TOP scanline -- no flip needed.
    CGPoint pos = CGPointMake (-box.origin.x + pad,
			       -box.origin.y + pad);
    CTFontDrawGlyphs (font, &glyph, &pos, 1, ctx);
    CGContextRelease (ctx);
    CFRelease (font);

    out.pixels = pixels;
    return true;
  }
}
}
