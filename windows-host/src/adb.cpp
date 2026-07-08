#include "adb.hpp"

#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>

namespace fif::host {

namespace {

std::string quote_for_cmd(const std::string& value) {
  if (value.find_first_of(" \t") == std::string::npos) {
    return value;
  }
  return "\"" + value + "\"";
}

std::string adb_base_command() {
  if (const char* from_env = std::getenv("FIF_ADB")) {
    if (from_env[0] != '\0') {
      return quote_for_cmd(from_env);
    }
  }
  return "adb";
}

}  // namespace

std::string AdbReverseManager::adb_command() const {
  std::string command = adb_base_command();
  if (const char* serial = std::getenv("FIF_ADB_SERIAL")) {
    if (serial[0] != '\0') {
      command += " -s ";
      command += quote_for_cmd(serial);
    }
  }
  return command;
}

bool AdbReverseManager::is_adb_available() const {
  if (const char* from_env = std::getenv("FIF_ADB")) {
    if (from_env[0] != '\0') {
      const std::string command = quote_for_cmd(from_env) + " version >nul 2>nul";
      return std::system(command.c_str()) == 0;
    }
  }
  return std::system("where adb >nul 2>nul") == 0;
}

bool AdbReverseManager::setup(const AdbReverseConfig& config) const {
  if (!is_adb_available()) {
    std::cerr << "adb not found on PATH\n";
    return false;
  }

  const std::string adb = adb_command();
  std::ostringstream control;
  control << adb << " reverse tcp:" << config.control_port << " tcp:" << config.control_port;
  if (std::system(control.str().c_str()) != 0) {
    std::cerr << "failed to configure adb reverse for control port\n";
    return false;
  }

  std::ostringstream video;
  video << adb << " reverse tcp:" << config.video_port << " tcp:" << config.video_port;
  if (std::system(video.str().c_str()) != 0) {
    std::cerr << "failed to configure adb reverse for video port\n";
    return false;
  }

  return true;
}

void AdbReverseManager::remove(const AdbReverseConfig& config) const {
  if (!is_adb_available()) {
    return;
  }

  const std::string adb = adb_command();
  std::ostringstream control;
  control << adb << " reverse --remove tcp:" << config.control_port;
  (void)std::system(control.str().c_str());

  std::ostringstream video;
  video << adb << " reverse --remove tcp:" << config.video_port;
  (void)std::system(video.str().c_str());
}

}  // namespace fif::host
