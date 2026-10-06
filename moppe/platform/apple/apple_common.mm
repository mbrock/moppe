// Platform services shared by the Apple UIKit hosts and the Mac's core
// probe: asset resolution, monotonic time, speech, and background work.

#import <AVFoundation/AVFoundation.h>
#import <Foundation/Foundation.h>
#import <TargetConditionals.h>

#include <moppe/platform/platform.hh>

#include <chrono>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <sys/stat.h>

namespace moppe {
  namespace platform {
    static bool file_exists (const std::string& p) {
      struct stat st;
      return ::stat (p.c_str (), &st) == 0;
    }

    std::string asset_path (const std::string& relative) {
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

    std::string executable_build_id () {
      static std::string cached;
      if (!cached.empty ())
        return cached;
      NSString* executable = [[NSBundle mainBundle] executablePath];
      if (!executable)
        return "unknown";
      std::ifstream input (executable.UTF8String, std::ios::binary);
      if (!input)
        return "unknown";

      // FNV-1a is used as a cache identity, not as a security boundary. Hashing
      // the linked executable makes dirty local rebuilds invalidate naturally.
      std::uint64_t hash = 14695981039346656037ull;
      char bytes[64 * 1024];
      while (input) {
        input.read (bytes, sizeof (bytes));
        for (std::streamsize i = 0; i < input.gcount (); ++i) {
          hash ^= static_cast<unsigned char> (bytes[i]);
          hash *= 1099511628211ull;
        }
      }
      std::ostringstream text;
      text << std::hex << std::setfill ('0') << std::setw (16) << hash;
      cached = text.str ();
      return cached;
    }

    std::string cache_path (const std::string& relative) {
      NSArray<NSURL*>* roots =
        [[NSFileManager defaultManager] URLsForDirectory:NSCachesDirectory
                                               inDomains:NSUserDomainMask];
      NSURL* base = roots.firstObject;
      if (!base)
        return relative;
      NSURL* directory = [base URLByAppendingPathComponent:@"Moppe"];
      [[NSFileManager defaultManager] createDirectoryAtURL:directory
                               withIntermediateDirectories:YES
                                                attributes:nil
                                                     error:nil];
      return std::string (directory.path.UTF8String) + "/" + relative;
    }

    double now () {
      using namespace std::chrono;
      return duration_cast<duration<double>> (
               steady_clock::now ().time_since_epoch ())
        .count ();
    }

    void say (const std::string& phrase) {
      // One long-lived synthesizer: a local would deallocate before
      // it finishes speaking.
      static AVSpeechSynthesizer* synth = [[AVSpeechSynthesizer alloc] init];
      AVSpeechUtterance* u = [AVSpeechUtterance
        speechUtteranceWithString:[NSString
                                    stringWithUTF8String:phrase.c_str ()]];
      [synth speakUtterance:u];
    }

    void async (void (*work) (void*),
                void (*done) (void*),
                std::shared_ptr<void> context) {
      dispatch_queue_t q =
        dispatch_get_global_queue (QOS_CLASS_USER_INITIATED, 0);
      // Blocks capture this plain pointer, while the shared owner keeps the
      // context alive from work dispatch through done's return.
      auto* retained = new std::shared_ptr<void> (std::move (context));
      dispatch_async (q, ^{
        work (retained->get ());
        dispatch_async (dispatch_get_main_queue (), ^{
          done (retained->get ());
          delete retained;
        });
      });
    }
  }
}
