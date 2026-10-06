#ifndef MOPPE_ENVIRONMENT_HH
#define MOPPE_ENVIRONMENT_HH

// The development switches (MOPPE_DEMO, MOPPE_HUD, ...) are named variables.
// Desktop and browser hosts keep them in the process environment. A UWP app
// has none -- the CRT does not even declare getenv there -- so on such hosts
// they live in a process-local table that the platform layer may fill (from
// a file the developer places in the app's storage, say).
//
// Set variables before starting threads that read them; the returned
// pointer stays valid until the variable is set again.

#ifdef _WIN32
#include <winapifamily.h>
#endif

#include <cstdlib>
#include <functional>
#include <map>
#include <string>
#include <string_view>

#if defined(_WIN32)
#if WINAPI_FAMILY_PARTITION(WINAPI_PARTITION_DESKTOP)
#define MOPPE_PROCESS_ENVIRONMENT 1
#else
#define MOPPE_PROCESS_ENVIRONMENT 0
#endif
#else
#define MOPPE_PROCESS_ENVIRONMENT 1
#endif

namespace moppe {
#if !MOPPE_PROCESS_ENVIRONMENT
  namespace detail {
    inline std::map<std::string, std::string, std::less<>>&
    environment_table () {
      static std::map<std::string, std::string, std::less<>> table;
      return table;
    }
  }
#endif

  // The variable's value, or null when it is unset.
  inline const char* environment (const char* name) {
#if MOPPE_PROCESS_ENVIRONMENT
    return std::getenv (name);
#else
    const auto& table = detail::environment_table ();
    const auto found = table.find (std::string_view (name));
    return found == table.end () ? nullptr : found->second.c_str ();
#endif
  }

  // Sets the variable, or unsets it when value is null.
  inline void set_environment (const char* name, const char* value) {
#if MOPPE_PROCESS_ENVIRONMENT && defined(_WIN32)
    ::_putenv_s (name, value ? value : "");
#elif MOPPE_PROCESS_ENVIRONMENT
    if (value)
      ::setenv (name, value, 1);
    else
      ::unsetenv (name);
#else
    auto& table = detail::environment_table ();
    if (value)
      table.insert_or_assign (std::string (name), std::string (value));
    else if (const auto found = table.find (std::string_view (name));
             found != table.end ())
      table.erase (found);
#endif
  }
}

#endif
