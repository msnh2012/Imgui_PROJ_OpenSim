#pragma once

#include "oscar/Platform/Log.hpp"
#include "oscar/Utils/CStringView.hpp"

#include <ctime>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// os: where all the icky OS/distro/filesystem-specific stuff is hidden
namespace osc
{
    // returns a `std::tm` populated 'as-if' by calling `std::gmtime(&t)`, but in
    // an OS-defined threadsafe way
    //
    // note: C++20 may make this obsolete (timezone and calendar support)
    std::tm GMTimeThreadsafe(std::time_t);

    // returns a `std::string` describing the given error number, but in an OS-defined
    // threadsafe way (unlike `strerror()`, which _may_ be undefined)
    std::string StrerrorThreadsafe(int errnum);

    // returns the full path to the currently-executing application
    std::filesystem::path const& CurrentExeDir();

    // returns the full path to the user's data directory
    std::filesystem::path const& GetUserDataDir();

    // writes a backtrace for the calling thread's stack to the log at the specified level
    void WriteTracebackToLog(log::level::LevelEnum);

    // installs a signal handler that prints a backtrace
    //
    // note: this is a noop on some OSes
    void InstallBacktraceHandler();

    // tries to open the specified filepath in the OSes default application for that
    // path. This function returns immediately: the application is opened in a separate
    // window.
    //
    // how, or what, the OS does is implementation-defined. E.g. Windows opens
    // filesystem paths by searching the file's extension against a list of default
    // applications. It opens URLs in the default browser, etc.
    void OpenPathInOSDefaultApplication(std::filesystem::path const&);

    // tries to open the specified URL in the OSes default browser
    //
    // how, or what, the OS does is implementation defined.
    void OpenURLInDefaultBrowser(std::string_view);

    // try to copy a string onto the user's clipboard
    bool SetClipboardText(CStringView);

    // set an environment variable's value (process-wide)
    void SetEnv(CStringView name, CStringView value, bool overwrite);

    // synchronously prompt a user to select a single file
    //
    // - `maybeCommaDelimitedExtensions` can be:
    //   - std::nullopt, meaning "don't filter by extension"
    //   - a single extension (e.g. "blend")
    //   - or a comma-delimited list of multiple extensions (e.g. "txt,csv,tsv")
    //
    // - `maybeInitialDirectoryToOpen` can be:
    //   - std::nullopt, meaning "use a system-defined default"
    //   - a directory to initially show to the user when the prompt opens
    //
    // returns std::nullopt if the user doesn't select a file
    std::optional<std::filesystem::path> PromptUserForFile(
        std::optional<CStringView> maybeCommaDelimitedExtensions = std::nullopt,
        std::optional<CStringView> maybeInitialDirectoryToOpen = std::nullopt
    );

    // synchronously prompt a user to select files ending with the supplied extensions (e.g. "txt,csv,tsv")
    //
    // - `maybeCommaDelimitedExtensions` can be:
    //   - std::nullopt, meaning "don't filter by extension"
    //   - a single extension (e.g. "blend")
    //   - or a comma-delimited list of multiple extensions (e.g. "txt,csv,tsv")
    //
    // - `maybeInitialDirectoryToOpen` can be:
    //   - std::nullopt, meaning "use a system-defined default"
    //   - a directory to initially show to the user when the prompt opens
    //
    // returns an empty vector if the user doesn't select any files
    std::vector<std::filesystem::path> PromptUserForFiles(
        std::optional<CStringView> maybeCommaDelimitedExtensions = std::nullopt,
        std::optional<CStringView> maybeInitialDirectoryToOpen = std::nullopt
    );

    // synchronously prompt a user to select a file location for where to save a file
    //
    // - `maybeOneExtension` can be:
    //   - std::nullopt, meaning "don't filter by extension"
    //   - or a single extension (e.g. "blend")
    //   - (you can't use multiple extensions with this method)
    //
    // - `maybeInitialDirectoryToOpen` can be:
    //   - std::nullopt, meaning "use a system-defined default"
    //   - a directory to initially show to the user when the prompt opens
    //
    // - if the user manually types a filename without an extension (e.g. "model"), the implementation will add `extension`
    //   (if not std::nullopt) to the end of the user's string. It detects a lack of extension by searching the end of the user
    //   -supplied string for the given extension (if supplied)
    //
    // returns std::nullopt if the user doesn't select a file; otherwise, returns the user-selected save location--including the extension--if
    // the user selects a location
    std::optional<std::filesystem::path> PromptUserForFileSaveLocationAndAddExtensionIfNecessary(
        std::optional<CStringView> maybeExtension = std::nullopt,
        std::optional<CStringView> maybeInitialDirectoryToOpen = std::nullopt
    );
}
