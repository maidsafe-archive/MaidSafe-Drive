/*
* ============================================================================
*
* Copyright [2011] Sigmoid Solutions limited
*
* Description:  Sigmoid Core
* Version:      1.0
* Created:      25/04/2011 12:00:01
* Company:      Sigmoid Solutions Ltd.
*
* The following source code is property of Sigmoid limited and is not
* meant for external use.  The use of this code is governed by the licence
* file licence.txt found in the root of this directory and also on
* www.sigmoidsolutions.co.uk.
*
* You are not free to copy, amend or otherwise use this source code without
* the explicit written permission of the board of directors of Sigmoid
* Solutions.
*
* ============================================================================
*/


#include <ShlObj.h>
#include <windows.h>
#include <cstdio>
#include <string>
#include "boost/filesystem/convenience.hpp"
#include "boost/lexical_cast.hpp"
#include "sigmoid/core/callbacks_win.h"
#include "sigmoid/core/log.h"

#ifdef __MSVC__
#  pragma warning(push, 1)
#endif

#include "boost/program_options.hpp"

#ifdef __MSVC__
#  pragma warning(pop)
#endif

namespace fs = boost::filesystem;
namespace po = boost::program_options;

namespace {
  std::string WstringToString(const std::wstring &input) {
    std::locale locale("");
    std::string string_buffer(input.size() * 4 + 1, 0);
    std::use_facet< std::ctype<wchar_t> >(locale).narrow(&input[0],
        &input[0] + input.size(), '?', &string_buffer[0]);
    return std::string(&string_buffer[0]);
  }
}

enum {
  SIGMOID_SERVICE_EMPTY_PATH = 0x0,
  SIGMOID_SERVICE_DRIVE_UNMOUNTING,
  SIGMOID_SERVICE_STD_EXCEPTION,
  SIGMOID_SERVICE_UNKNOWN_EXCEPTION,
  SIGMOID_SERVICE_INITIALISATION,
  SIGMOID_SERVICE_MOUNTING_DRIVE,
  SIGMOID_SERVICE_STOP_REQUESTED
};

fs::path GetPathFromProgramOption(const std::string &option_name,
                                  po::variables_map *variables_map,
                                  bool must_exist) {
  if (variables_map->count(option_name)) {
    boost::system::error_code error_code;
    fs::path option_path(variables_map->at(option_name).as<std::string>());
    if (must_exist) {
      if (!fs::exists(option_path, error_code) || error_code) {
        LOG(ERROR) << "Invalid " << option_name << " option.  " << option_path
                   << " doesn't exist or can't be accessed (error message: "
                   << error_code.message() << ")";
        return fs::path();
      }
      if (!fs::is_directory(option_path, error_code) || error_code) {
        LOG(ERROR) << "Invalid " << option_name << " option.  " << option_path
                   << " is not a directory (error message: "
                   << error_code.message() << ")";
        return fs::path();
      }
    } else {
      if (fs::exists(option_path, error_code)) {
        LOG(ERROR) << "Invalid " << option_name << " option.  " << option_path
                   << " already exists (error message: "
                   << error_code.message() << ")";
        return fs::path();
      }
    }
    LOG(INFO) << option_name << " set to " << option_path;
    return option_path;
  } else {
    LOG(WARNING) << "You must set the " << option_name << " option to a"
                 << (must_exist ? "n" : " non-") << "existing directory.";
    return fs::path();
  }
}

fs::path ApplicationDataConfigFilePath() {
  TCHAR application_data_directory[MAX_PATH];
  fs::path application_data_path, application_config_file_path;
  boost::system::error_code error_code;
  bool found_config_file = false;

  if (SUCCEEDED(SHGetFolderPath(NULL,
                                CSIDL_COMMON_APPDATA,
                                NULL,
                                0,
                                application_data_directory))) {
    application_data_path = application_data_directory;
    application_config_file_path = application_data_path
                                   / "Sigmoid\\Core\\sigmoid_core.conf";
    if (fs::exists(application_config_file_path, error_code)) {
      found_config_file = true;
    } else {
      application_data_directory[0] = '\0';
      if (SUCCEEDED(SHGetFolderPath(NULL,
                                    CSIDL_APPDATA,
                                    NULL,
                                    0,
                                    application_data_directory))) {
        application_data_path = application_data_directory;
        application_config_file_path = application_data_path
                                       / "Sigmoid\\Core\\sigmoid_core.conf";
        if (fs::exists(application_config_file_path, error_code)) {
          found_config_file = true;
        } else {
          application_data_directory[0] = '\0';
          if (SUCCEEDED(SHGetFolderPath(NULL,
                                        CSIDL_LOCAL_APPDATA,
                                        NULL,
                                        0,
                                        application_data_directory))) {
            application_data_path = application_data_directory;
            application_config_file_path = application_data_path
                                          / "Sigmoid\\Core\\sigmoid_core.conf";
            if (fs::exists(application_config_file_path, error_code)) {
              found_config_file = true;
            }
          }
        }
      }
    }
  }
  if (found_config_file) {
    fs::path logging_dir(application_config_file_path.parent_path() / "logs");
    if (!fs::exists(logging_dir, error_code))
      fs::create_directory(logging_dir, error_code);
    fs::path log_path(logging_dir / "sigmoid_core.");
    for (google::LogSeverity s = google::WARNING; s < google::NUM_SEVERITIES;
         ++s)
      google::SetLogDestination(s, "");
    google::SetLogDestination(google::INFO, log_path.string().c_str());
    LOG(INFO) << "Sigmoid log files will be written to "
              << WstringToString(logging_dir.wstring());
  }
  return application_config_file_path;
}

struct NullLogger : public google::base::Logger {
  virtual void Write(bool should_flush,
                     time_t timestamp,
                     const char* message,
                     int length) {}
  virtual void Flush() {}
  virtual std::uint32_t LogSize() { return 0; }
};

void SigmoidLogger(int severity, google::base::Logger* logger) {
  google::base::Logger* old_logger = google::base::GetLogger(severity);
  google::base::SetLogger(severity, logger);
  google::FlushLogFiles(severity);
}

void FailureWriter(const char *data, int size) {
  fs::path logging_dir(fs::temp_directory_path() / "SigmoidCoreLogs");
  boost::system::error_code error_code;
  if (!fs::exists(logging_dir, error_code))
    fs::create_directory(logging_dir, error_code);
  std::string data_dump(data, size);
  std::ofstream dump_file;
  dump_file.open(logging_dir.string() + "data_dump.log",
           std::ios_base::out | std::ios_base::binary);
  if (dump_file.bad()) {
    LOG(INFO) << "Unable to open " << logging_dir.string()
              << "\\data_dump.log!\n";
  } else {
    dump_file << data_dump;
    dump_file.close();
  }
}

namespace {
SERVICE_STATUS g_service_status;
SERVICE_STATUS_HANDLE g_service_status_handle;
wchar_t g_service_name[12] = L"SigmoidCore";
}  // unnamed namespace

void StopService(DWORD exit_code, DWORD error_code) {
  g_service_status.dwCurrentState = SERVICE_STOPPED;
  g_service_status.dwWin32ExitCode = exit_code;
  g_service_status.dwServiceSpecificExitCode = error_code;
  SetServiceStatus(g_service_status_handle, &g_service_status);
}

void ServiceMain();
void ControlHandler(DWORD request);

int main(int /*argc*/, char *argv[]) {
  google::InitGoogleLogging(argv[0]);
  // google::InstallFailureSignalHandler();
  // google::InstallFailureWriter(&FailureWriter);
  FLAGS_logtostderr = false;
  FLAGS_stderrthreshold = 2;
  FLAGS_minloglevel = 0;
  // FLAGS_log_dir = logging_dir.string().c_str();

  /*NullLogger null_logger;
  SigmoidLogger(google::INFO, &null_logger);
  SigmoidLogger(google::WARNING, &null_logger);
  SigmoidLogger(google::ERROR, &null_logger);
  SigmoidLogger(google::FATAL, &null_logger);*/

  SERVICE_TABLE_ENTRY service_table[2];
  service_table[0].lpServiceName = g_service_name;
  service_table[0].lpServiceProc =
      reinterpret_cast<LPSERVICE_MAIN_FUNCTION>(ServiceMain);
  service_table[1].lpServiceName = NULL;
  service_table[1].lpServiceProc = NULL;
  // Start the control dispatcher thread for our service
  StartServiceCtrlDispatcher(service_table);
  return 0;
}


void ServiceMain() {
  g_service_status.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
  g_service_status.dwCurrentState = SERVICE_START_PENDING;
  g_service_status.dwControlsAccepted = SERVICE_ACCEPT_STOP |
                                        SERVICE_ACCEPT_SHUTDOWN;
  g_service_status.dwWin32ExitCode = 0;
  g_service_status.dwServiceSpecificExitCode = 0;
  g_service_status.dwCheckPoint = 0;
  g_service_status.dwWaitHint = 0;

  g_service_status_handle = RegisterServiceCtrlHandler(g_service_name,
      reinterpret_cast<LPHANDLER_FUNCTION>(ControlHandler));
  if (g_service_status_handle == SERVICE_STATUS_HANDLE(0))
    return;

  fs::path chunkstore_path, metadata_path, mount_path;

  try {
    po::options_description options_description("Allowed options");
    options_description.add_options()
        ("help,H", "print this help message")
        ("version,V", "Display version")
        ("chunkdir,C", po::value<std::string>(),
         "set directory to store chunks")
        ("metadatadir,M", po::value<std::string>(),
         "set directory to store metadata")
        ("mountdir,D", po::value<std::string>(), "set virtual drive name")
        ("checkdata", "check all data (metadata and chunks)")
        ("start", "start Sigmoid Core (mount drive) [default]")  // daemonise
        ("stop", "stop Sigmoid Core (unmount drive) [not implemented]");

    po::variables_map variables_map;
    // set up options for config file
    po::options_description config_file_options;
    config_file_options.add(options_description);

    fs::path config_file_path = ApplicationDataConfigFilePath();
    if (config_file_path != fs::path()) {
      std::ifstream conf_win(config_file_path.c_str());
      store(parse_config_file(conf_win, config_file_options), variables_map);
      notify(variables_map);
    } else {
      LOG(WARNING) << "WARNING: Sigmoid Core configuration file not found";
    }

    /*if (conf_win) {
      LOG(INFO) << "Found C:\\ProgramData\\Sigmoid\\Core\\sigmoid_core.conf";
      store(parse_config_file(conf_win, config_file_options), variables_map);
      notify(variables_map);
    } else {
      LOG(INFO) << "WARNING: Configuration file C:/ProgramData/Sigmoid/Core"
                << "sigmoid_core.conf missing";
    }*/

    if (variables_map.count("help")) {
      LOG(INFO) << options_description;
      return;
    }

    if (variables_map.count("version")) {
      // We should include version.h and use a standard here
      LOG(INFO) << "Sigmoid Core version = "
                << BOOST_PP_STRINGIZE(SIGMOID_CORE_VERSION) << "\n";
      LOG(INFO) << "MaidSafe-Common Version = "
                << BOOST_PP_STRINGIZE(MAIDSAFE_COMMON_VERSION) << "\n";
      LOG(INFO) << "MaidSafe-Encrypt Version = "
                << BOOST_PP_STRINGIZE(MAIDSAFE_ENCRYPT_VERSION) << "\n";
      return;
    }

    chunkstore_path = GetPathFromProgramOption("chunkdir", &variables_map,
                                               true);
    metadata_path = GetPathFromProgramOption("metadatadir", &variables_map,
                                             true);
    mount_path = GetPathFromProgramOption("mountdir", &variables_map, false);

    if (variables_map.count("stop")) {
      LOG(WARNING) << "Trying to stop.\n";
      StopService(ERROR_SERVICE_SPECIFIC_ERROR,
                  SIGMOID_SERVICE_DRIVE_UNMOUNTING);
      return;
    }

    if (chunkstore_path == fs::path() ||
        metadata_path == fs::path() ||
        mount_path == fs::path()) {
      LOG(ERROR) << "Usage " << options_description << "\n";
      StopService(ERROR_SERVICE_SPECIFIC_ERROR, SIGMOID_SERVICE_EMPTY_PATH);
      return;
    }
  }
  catch(const std::exception& e) {
    LOG(ERROR) << "Exception: " << e.what() << "\n";
    StopService(ERROR_SERVICE_SPECIFIC_ERROR, SIGMOID_SERVICE_STD_EXCEPTION);
    return;
  }
  catch(...) {
    LOG(ERROR) << "Exception of unknown type!\n";
    StopService(ERROR_SERVICE_SPECIFIC_ERROR,
                SIGMOID_SERVICE_UNKNOWN_EXCEPTION);
    return;
  }

  LOG(INFO) << "Sigmoid Core service starting.\n";
  g_service_status.dwCurrentState = SERVICE_RUNNING;
  SetServiceStatus(g_service_status_handle, &g_service_status);

  std::shared_ptr<maidsafe::CbfsDriveInUserSpace> drive_in_user_space(
      new maidsafe::CbfsDriveInUserSpace(chunkstore_path));
  std::string registration_key;

  int result = drive_in_user_space->Init(registration_key);
  if (result == 0) {
    LOG(INFO) << "Init result: " << result;
  } else {
    LOG(ERROR) << "Init failed: " << result;
    StopService(ERROR_SERVICE_SPECIFIC_ERROR, SIGMOID_SERVICE_INITIALISATION);
    return;
  }

  if ((result = drive_in_user_space->Mount(mount_path, metadata_path)) == 0) {
    LOG(INFO) << "Mount result: " << result;
  } else {
    LOG(ERROR) << "Mount failed: " << result;
    StopService(ERROR_SERVICE_SPECIFIC_ERROR, SIGMOID_SERVICE_MOUNTING_DRIVE);
    return;
  }

  drive_in_user_space->WaitUntilUnMounted();
  drive_in_user_space->CleanUp();
  LOG(INFO) << "Sigmoid Core unmounted - service stopping.";
  StopService(0, 0);
}

void ControlHandler(DWORD request) {
  switch (request) {
    case SERVICE_CONTROL_STOP:
      LOG(INFO) << "Sigmoid Core SERVICE_CONTROL_STOP received - "
                   "service stopping.";
      g_service_status.dwWin32ExitCode = 0;
      g_service_status.dwCurrentState = SERVICE_STOPPED;
      SetServiceStatus(g_service_status_handle, &g_service_status);
      return;
    case SERVICE_CONTROL_SHUTDOWN:
      LOG(INFO) << "Sigmoid Core SERVICE_CONTROL_SHUTDOWN received - "
                   "service stopping.";
      g_service_status.dwWin32ExitCode = 0;
      g_service_status.dwCurrentState = SERVICE_STOPPED;
      SetServiceStatus(g_service_status_handle, &g_service_status);
      return;
    default:
      break;
  }
  // Report current status
  SetServiceStatus(g_service_status_handle, &g_service_status);
}
