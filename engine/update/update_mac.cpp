#include "update_internal.h"

#if !defined(_WIN32)
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace engine::update {

namespace fs = std::filesystem;

namespace {

constexpr const char* kStateName = ".Pokemon XD updates";

std::string Quote(const std::string& text) {
  std::string out = "'";
  for (char c : text) {
    if (c == '\'') {
      out += "'\\''";
    } else {
      out += c;
    }
  }
  return out + "'";
}

bool AppUsable(const fs::path& app, const std::vector<std::string>& required) {
  return FolderUsable(app / "Contents" / "MacOS", required);
}

}

fs::path MacStateFolder(const Layout& l) { return l.root / kStateName; }

std::string MacHelperScript(const fs::path& app, const fs::path& pending, const fs::path& previous, long waitForPid,
                            bool openAfter) {
  const std::string a = Quote(detail::Utf8(app)), n = Quote(detail::Utf8(pending)), p = Quote(detail::Utf8(previous));
  std::string s = "#!/bin/sh\n"
                  "# Written by the launcher's updater (docs/UPDATER.md): puts a new Pokemon XD.app in place once the\n"
                  "# launcher has ended, and opens it. Any failure puts the old app back.\n";
  s += "PID=" + std::to_string(waitForPid) + "\nAPP=" + a + "\nPENDING=" + n + "\nPREV=" + p + "\n";
  s += "i=0\n"
       "while kill -0 \"$PID\" 2>/dev/null && [ \"$i\" -lt 120 ]; do sleep 1; i=$((i+1)); done\n"
       "if kill -0 \"$PID\" 2>/dev/null; then exit 1; fi\n"
       "[ -d \"$PENDING\" ] || exit 1\n"
       "mkdir -p \"$(dirname \"$PREV\")\" || exit 1\n"
       "rm -rf \"$PREV\"\n"
       "if mv \"$APP\" \"$PREV\"; then\n"
       "  if ! mv \"$PENDING\" \"$APP\"; then mv \"$PREV\" \"$APP\"; fi\n"
       "fi\n"
       "rmdir \"$(dirname \"$PENDING\")\" 2>/dev/null\n"
       "rm -rf \"$(dirname \"$PENDING\")\"\n"
       "rm -f \"$0\"\n";
  if (openAfter) {
    s += "[ -d \"$APP\" ] && open \"$APP\"\n";
  }
  return s;
}

namespace detail {

bool LooksLikeMacApp(const fs::path& programFolder) {
  const fs::path macos = programFolder.filename() == "" ? programFolder.parent_path() : programFolder;
  const fs::path contents = macos.parent_path();
  const fs::path app = contents.parent_path();
  return macos.filename() == "MacOS" && contents.filename() == "Contents" && app.extension() == ".app";
}

Layout FindMacLayout(const fs::path& programFolder) {
  Layout l;
  std::error_code ec;
  const fs::path macos = fs::weakly_canonical(programFolder, ec);
  l.macApp = true;
  l.appPath = macos.parent_path().parent_path();
  l.root = l.appPath.parent_path();
  l.running = "app";
  const std::string where = Utf8(l.appPath);
  if (where.find("/AppTranslocation/") != std::string::npos || where.rfind("/Volumes/", 0) == 0) {
    l.why = Fmt(Tr("This copy runs from a disk image or from a temporary place the Mac made for it (%s). Drag Pokemon XD to your "
                   "Applications folder (or any folder of yours), open it from there, and updates can be installed."),
                where.c_str());
    l.root.clear();
    return l;
  }
#if !defined(_WIN32)
  if (::access(l.root.c_str(), W_OK | X_OK) != 0) {
    l.why = Fmt(Tr("Pokemon XD is in a folder this account can't write to (%s), so an update can't be put in place. Move the app "
                   "into a folder of yours (or install the new version by hand)."),
                Utf8(l.root).c_str());
    l.root.clear();
    return l;
  }
#endif
  l.valid = true;
  const fs::path state = l.root / kStateName;
  const fs::path name = l.appPath.filename();
  const bool pending = fs::is_directory(state / "pending" / name, ec);
  const bool previous = fs::is_directory(state / "previous" / name, ec);
  l.current = pending ? "pending" : "app";
  l.previous = pending ? "app" : (previous ? "previous" : "");
  return l;
}

fs::path MacProgramFolder(const Layout& l, const std::string& name) {
  const fs::path state = l.root / kStateName;
  const fs::path app = l.appPath.filename();
  if (name == "pending") {
    return state / "pending" / app / "Contents" / "MacOS";
  }
  if (name == "previous") {
    return state / "previous" / app / "Contents" / "MacOS";
  }
  return l.appPath / "Contents" / "MacOS";
}

bool InstallMacApp(const InstallRequest& r, std::string& installed, std::string& error,
                   const std::function<bool()>& stop) {
  installed.clear();
  const Layout& l = r.layout;
  std::error_code ec;
  const fs::path state = l.root / kStateName;
  const fs::path partial = state / "pending.partial";
  const fs::path pending = state / "pending";
  const fs::path name = l.appPath.filename();
  const auto fail = [&](const std::string& why) {
    error = why;
    RemoveFolder(partial);
    return false;
  };
  if (!RemoveFolder(partial)) {
    error = Fmt(Tr("an earlier unfinished update couldn't be removed: %s"), Utf8(partial).c_str());
    return false;
  }
  fs::create_directories(state, ec);
  std::string why;
  if (!ExtractZip(r.package, partial, why, stop)) {
    return fail(why);
  }
  fs::path base = partial;
  bool wholeDownload = false;
  {
    int topApps = 0;
    fs::path onlyDir;
    int dirs = 0;
    for (const auto& e : fs::directory_iterator(partial, ec)) {
      if (e.is_directory(ec) && e.path().extension() == ".app") {
        topApps++;
      } else if (e.is_directory(ec) && !e.is_symlink(ec)) {
        onlyDir = e.path();
        dirs++;
      }
    }
    if (topApps == 0 && dirs == 1) {
      base = onlyDir;
      wholeDownload = true;
    }
  }
  fs::path found;
  int apps = 0;
  for (const auto& e : fs::directory_iterator(base, ec)) {
    if (e.is_directory(ec) && e.path().extension() == ".app") {
      found = e.path();
      apps++;
    } else if (!wholeDownload) {
      return fail(Fmt(Tr("the package holds something besides the app: %s"), Utf8(e.path().filename()).c_str()));
    }
  }
  if (apps != 1) {
    return fail("the package should hold exactly one app");
  }
  if (!AppUsable(found, r.requiredFiles)) {
    return fail("the package is incomplete (the app should hold the launcher and the game)");
  }
  RestoreExecutableBits(found);
  const std::string version = FolderVersion(found / "Contents" / "MacOS");
  if (version != r.manifest.version) {
    return fail(Fmt(Tr("the package holds version %s, not %s"), version.c_str(), r.manifest.version.c_str()));
  }
  if (!RemoveFolder(pending)) {
    return fail("an earlier waiting update couldn't be removed");
  }
  fs::create_directories(pending, ec);
  fs::rename(found, pending / name, ec);
  if (ec) {
    RemoveFolder(pending);
    return fail(Fmt(Tr("the new app couldn't be put in its waiting place: %s"), ec.message().c_str()));
  }
  RemoveFolder(partial);
  installed = "pending";
  return true;
}

bool GoBackMac(const Layout& l, const std::vector<std::string>& requiredFiles, std::string& error) {
  std::error_code ec;
  const fs::path state = l.root / kStateName;
  if (l.current == "pending") {
    if (!RemoveFolder(state / "pending")) {
      error = Tr("the waiting update couldn't be removed");
      return false;
    }
    return true;
  }
  if (l.previous != "previous" || !FolderUsable(MacProgramFolder(l, "previous"), requiredFiles)) {
    error = Tr("there is no previous version to go back to");
    return false;
  }
  if (!RemoveFolder(state / "pending")) {
    error = Tr("the waiting update couldn't be removed");
    return false;
  }
  fs::rename(state / "previous", state / "pending", ec);
  if (ec) {
    error = Fmt(Tr("the previous version couldn't be made ready: %s"), ec.message().c_str());
    return false;
  }
  return true;
}

}

bool MacApplyPending(const Layout& l, std::string& error) {
  error.clear();
  if (!l.macApp || !l.valid) {
    return false;
  }
  std::error_code ec;
  const fs::path state = MacStateFolder(l);
  const fs::path name = l.appPath.filename();
  const fs::path pending = state / "pending" / name;
  if (!fs::is_directory(pending, ec)) {
    return false;
  }
#if defined(_WIN32)
  return false;
#else
  const fs::path script = state / "swap.sh";
  const std::string text = MacHelperScript(l.appPath, pending, state / "previous" / name, static_cast<long>(::getpid()),
                                         std::getenv("XD_UPDATE_NO_OPEN") == nullptr);
  if (std::FILE* f = std::fopen(script.c_str(), "wb")) {
    const bool ok = std::fwrite(text.data(), 1, text.size(), f) == text.size();
    if (std::fclose(f) != 0 || !ok) {
      error = Tr("the update helper couldn't be written");
      return false;
    }
  } else {
    error = Tr("the update helper couldn't be written");
    return false;
  }
  const pid_t child = ::fork();
  if (child < 0) {
    error = Tr("the update helper couldn't be started");
    return false;
  }
  if (child == 0) {
    ::setsid();
    const int null = ::open("/dev/null", O_RDWR);
    if (null >= 0) {
      ::dup2(null, 0);
      ::dup2(null, 1);
      ::dup2(null, 2);
    }
    ::execl("/bin/sh", "sh", script.c_str(), static_cast<char*>(nullptr));
    ::_exit(127);
  }
  return true;
#endif
}

}
