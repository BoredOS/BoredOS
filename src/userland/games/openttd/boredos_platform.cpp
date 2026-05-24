#include "stdafx.h"

#include <optional>
#include <cstdint>
#include <string>
#include <string_view>
#include <type_traits>

extern "C" {
#include "libc/stdio.h"
#include "libc/syscall.h"
}

#include "crashlog.h"
#include "fios.h"
#include "library_loader.h"
#include "survey.h"

namespace {

constexpr uint32_t kBoredOSAppNoteType = 0x41505031U;
constexpr uint32_t kBoredOSAppMetadataMagic = 0x414d4431U;
constexpr uint16_t kBoredOSAppMetadataVersion = 1U;
constexpr size_t kBoredOSAppMetadataMaxAppName = 64;
constexpr size_t kBoredOSAppMetadataMaxDescription = 192;
constexpr size_t kBoredOSAppMetadataMaxImages = 4;
constexpr size_t kBoredOSAppMetadataMaxImagePath = 160;

struct __attribute__((packed)) BoredOSAppMetadata {
    uint32_t magic;
    uint16_t version;
    uint16_t image_count;
    uint16_t reserved;
    char app_name[kBoredOSAppMetadataMaxAppName];
    char description[kBoredOSAppMetadataMaxDescription];
    char images[kBoredOSAppMetadataMaxImages][kBoredOSAppMetadataMaxImagePath];
};

struct __attribute__((packed, aligned(4))) BoredOSAppNote {
    uint32_t namesz;
    uint32_t descsz;
    uint32_t type;
    char name[8];
    BoredOSAppMetadata metadata;
};

__attribute__((used, section(".note.boredos.app"), aligned(4)))
static const BoredOSAppNote g_boredos_app_note = {
    .namesz = 7,
    .descsz = sizeof(BoredOSAppMetadata),
    .type = kBoredOSAppNoteType,
    .name = "BOREDOS",
    .metadata = {
        .magic = kBoredOSAppMetadataMagic,
        .version = kBoredOSAppMetadataVersion,
        .image_count = 1,
        .reserved = 0,
        .app_name = "OpenTTD",
        .description = "Native upstream OpenTTD transport simulation for BoredOS.",
        .images = {
            "/Library/images/icons/colloid/applications-games.png",
            "",
            "",
            "",
        },
    },
};

}

void ShowInfoI(std::string_view str)
{
    if (!str.empty()) fwrite(str.data(), 1, str.size(), stderr);
    fputc('\n', stderr);
}

void ShowOSErrorBox(std::string_view str, bool)
{
    ShowInfoI(str);
}

std::optional<std::string> GetClipboardContents()
{
    return std::nullopt;
}

void OSOpenBrowser(const std::string &)
{
}

bool FiosIsRoot(const std::string &path)
{
    return path.empty() || path == "/";
}

void FiosGetDrives(FileList &)
{
}

std::optional<uint64_t> FiosGetDiskFreeSpace(const std::string &)
{
    return std::nullopt;
}

void SurveyOS(nlohmann::json &json)
{
    json["name"] = "BoredOS";
}

void CrashLog::InitialiseCrashLog()
{
}

extern "C" uint32_t openttd_boredos_ticks_ms(void)
{
    uint64_t ticks = sys_system(SYSTEM_CMD_GET_TICKS, 0, 0, 0, 0);
    return (uint32_t)((ticks * 50ull) / 3ull);
}

void *LibraryLoader::OpenLibrary(const std::string &)
{
    this->error = "Dynamic loading is not supported on BoredOS.";
    return nullptr;
}

void LibraryLoader::CloseLibrary()
{
}

void *LibraryLoader::GetSymbol(const std::string &)
{
    this->error = "Dynamic loading is not supported on BoredOS.";
    return nullptr;
}
