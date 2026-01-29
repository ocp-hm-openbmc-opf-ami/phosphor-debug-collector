#include "config.h"
#include <filesystem>
#include <chrono>
#include <thread>
#include <iostream>
#include <regex>

#include "core_manager.hpp"


namespace phosphor
{
namespace dump
{
namespace core
{

using namespace std;

#define  DUMPPREFIX "core."
#define  TIMEOUTSECONDS 5
vector<string> files;

void Manager::watchCallback(const UserMap& fileInfo)
{
    for (const auto& i : fileInfo)
    {
        std::filesystem::path file(i.first);
        std::string path = file.parent_path();

        waitForFileCreation(path, DUMPPREFIX, TIMEOUTSECONDS);
    }

    if (!files.empty())
    {
        createHelper(files);
    }
}

void Manager::waitForFileCreation(const std::string& directory, const std::string& prefix, int timeoutSeconds)
{
    namespace fs = std::filesystem;
    auto start = std::chrono::steady_clock::now();

    while (true)
    {
        std::optional<fs::directory_entry> latestEntry = std::nullopt;
        std::chrono::file_clock::time_point latestTime = std::chrono::file_clock::time_point::min();

        try
        {
            for (const auto& entry : fs::directory_iterator(directory))
            {
                try
                {
                    if (entry.is_regular_file())
                    {
                        std::string filename = entry.path().filename().string();
                        if (filename.rfind(prefix, 0) == 0) // starts with prefix
                        {
                            auto ftime = fs::last_write_time(entry);
                            if (!latestEntry || ftime > latestTime)
                            {
                                latestEntry = entry;
                                latestTime = ftime;
                            }
                        }
                    }
                }
                catch (const fs::filesystem_error& e)
                {
                    lg2::error("Error accessing file: {ERROR}", "ERROR", e);
                    continue;
                }
            }
        }
        catch (const fs::filesystem_error& e)
        {
            lg2::error("Error reading directory: {ERROR}", "ERROR", e);
            return;
        }

        if (latestEntry)
        {
            try
            {
                std::string latestFilename = latestEntry->path().filename().string();
                if (std::find(files.begin(), files.end(), latestFilename) == files.end())
                {
                    files.push_back(latestFilename);
                    return;
                }
            }
            catch (const std::exception& e)
            {
                lg2::error("Error processing latest file: {ERROR}", "ERROR", e);
                return;
            }
        }

        std::this_thread::sleep_for(std::chrono::seconds(1));

        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::seconds>(now - start).count() > timeoutSeconds)
        {
            return;
        }
    }
}

void Manager::createHelper(const vector<string>& files)
{
    constexpr auto MAPPER_BUSNAME = "xyz.openbmc_project.ObjectMapper";
    constexpr auto MAPPER_PATH = "/xyz/openbmc_project/object_mapper";
    constexpr auto MAPPER_INTERFACE = "xyz.openbmc_project.ObjectMapper";
    constexpr auto DUMP_CREATE_IFACE = "xyz.openbmc_project.Dump.Create";

    auto b = sdbusplus::bus::new_default();
    auto mapper = b.new_method_call(MAPPER_BUSNAME, MAPPER_PATH,
                                    MAPPER_INTERFACE, "GetObject");
    mapper.append(BMC_DUMP_OBJPATH, vector<string>({DUMP_CREATE_IFACE}));

    map<string, vector<string>> mapperResponse;
    try
    {
        auto mapperResponseMsg = b.call(mapper);
        mapperResponseMsg.read(mapperResponse);
    }
    catch (const sdbusplus::exception_t& e)
    {
        lg2::error("Failed to GetObject on Dump.Create: {ERROR}", "ERROR", e);
        return;
    }
    if (mapperResponse.empty())
    {
        lg2::error("Error reading mapper response");
        return;
    }

    const auto& host = mapperResponse.cbegin()->first;
    auto m = b.new_method_call(host.c_str(), BMC_DUMP_OBJPATH,
                               DUMP_CREATE_IFACE, "CreateDump");
    phosphor::dump::DumpCreateParams params;
    using CreateParameters =
        sdbusplus::common::xyz::openbmc_project::dump::Create::CreateParameters;
    using DumpType =
        sdbusplus::common::xyz::openbmc_project::dump::Create::DumpType;
    using DumpIntr = sdbusplus::common::xyz::openbmc_project::dump::Create;
    params[DumpIntr::convertCreateParametersToString(
        CreateParameters::DumpType)] =
        DumpIntr::convertDumpTypeToString(DumpType::ApplicationCored);
    params[DumpIntr::convertCreateParametersToString(
        CreateParameters::FilePath)] = files.front();
    m.append(params);
    try
    {
        b.call_noreply(m);
    }
    catch (const sdbusplus::exception_t& e)
    {
        lg2::error("Failed to create dump: {ERROR}", "ERROR", e);
    }
}

} // namespace core
} // namespace dump
} // namespace phosphor
