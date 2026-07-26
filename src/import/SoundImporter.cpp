#include "import/SoundImporter.hpp"

#include "sound/SoundFileFormat.hpp"

#include <algorithm>
#include <chrono>
#include <cwctype>
#include <set>
#include <string>
#include <system_error>
#include <utility>

namespace
{
    constexpr std::size_t MaximumImportItems = 4096;

    std::wstring CanonicalKey(const std::filesystem::path& path)
    {
        std::wstring key = path.lexically_normal().wstring();
        std::transform(
            key.begin(),
            key.end(),
            key.begin(),
            [](const wchar_t character)
            {
                return static_cast<wchar_t>(std::towlower(character));
            }
        );
        return key;
    }

    bool RelativePathStaysInside(
        const std::filesystem::path& relative
    )
    {
        if (relative.empty() || relative.has_root_path())
        {
            return false;
        }

        for (const auto& component : relative)
        {
            if (component == L"..")
            {
                return false;
            }
        }

        return true;
    }

    std::filesystem::path UniqueDestination(
        const std::filesystem::path& directory,
        const std::filesystem::path& source,
        std::error_code& error
    )
    {
        std::filesystem::path destination =
            directory / source.filename();

        error.clear();
        if (!std::filesystem::exists(destination, error) && !error)
        {
            return destination;
        }

        const std::filesystem::path stem = source.stem();
        const std::filesystem::path extension = source.extension();

        for (unsigned int suffix = 2; suffix < 10000; ++suffix)
        {
            destination = directory /
                (stem.wstring() + L"_" + std::to_wstring(suffix) +
                    extension.wstring());

            error.clear();
            if (!std::filesystem::exists(destination, error) && !error)
            {
                return destination;
            }
        }

        error = std::make_error_code(
            std::errc::file_exists
        );
        return {};
    }

    bool CopyFileAtomically(
        const std::filesystem::path& source,
        const std::filesystem::path& destination,
        std::error_code& error
    )
    {
        const auto nonce = std::chrono::steady_clock::now()
            .time_since_epoch()
            .count();
        std::filesystem::path temporary = destination;
        temporary += L".importing-" + std::to_wstring(nonce) + L".tmp";

        error.clear();
        std::filesystem::copy_file(
            source,
            temporary,
            std::filesystem::copy_options::none,
            error
        );

        if (error)
        {
            std::error_code cleanupError;
            std::filesystem::remove(temporary, cleanupError);
            return false;
        }

        error.clear();
        std::filesystem::rename(temporary, destination, error);

        if (error)
        {
            std::error_code cleanupError;
            std::filesystem::remove(temporary, cleanupError);
            return false;
        }

        return true;
    }
}

SoundImporter::SoundImporter(std::filesystem::path soundsFolder)
    : soundsFolder_(std::move(soundsFolder))
{
}

SoundImportSummary SoundImporter::Import(
    const std::vector<std::filesystem::path>& inputs
) const
{
    SoundImportSummary summary;

    if (soundsFolder_.empty())
    {
        summary.failedPaths = inputs;
        return summary;
    }

    std::error_code error;
    std::filesystem::create_directories(soundsFolder_, error);

    if (error)
    {
        summary.failedPaths = inputs;
        return summary;
    }

    const std::filesystem::path importedFolder =
        soundsFolder_ / L"Imported";
    error.clear();
    std::filesystem::create_directories(importedFolder, error);

    if (error)
    {
        summary.failedPaths = inputs;
        return summary;
    }

    error.clear();
    const std::filesystem::path soundsRoot =
        std::filesystem::weakly_canonical(soundsFolder_, error);

    if (error)
    {
        summary.failedPaths = inputs;
        return summary;
    }

    std::vector<std::filesystem::path> candidates;
    std::set<std::wstring> candidateKeys;
    std::size_t scannedItemCount = 0;

    const auto addCandidate = [&](const std::filesystem::path& candidate)
    {
        if (scannedItemCount >= MaximumImportItems)
        {
            summary.itemLimitReached = true;
            return;
        }

        ++scannedItemCount;

        std::error_code candidateError;
        const std::filesystem::path canonical =
            std::filesystem::weakly_canonical(candidate, candidateError);

        if (candidateError ||
            !std::filesystem::is_regular_file(canonical, candidateError) ||
            candidateError)
        {
            summary.failedPaths.push_back(candidate);
            return;
        }

        if (!SoundFileFormat::IsSupported(canonical))
        {
            ++summary.unsupportedCount;
            return;
        }

        if (candidateKeys.insert(CanonicalKey(canonical)).second)
        {
            candidates.push_back(canonical);
        }
    };

    for (const auto& input : inputs)
    {
        if (summary.itemLimitReached)
        {
            break;
        }

        std::error_code inputError;
        const std::filesystem::path canonical =
            std::filesystem::weakly_canonical(input, inputError);

        if (inputError)
        {
            summary.failedPaths.push_back(input);
            continue;
        }

        inputError.clear();
        if (std::filesystem::is_regular_file(canonical, inputError) &&
            !inputError)
        {
            addCandidate(canonical);
            continue;
        }

        inputError.clear();
        if (!std::filesystem::is_directory(canonical, inputError) ||
            inputError)
        {
            summary.failedPaths.push_back(input);
            continue;
        }

        std::filesystem::recursive_directory_iterator iterator{
            canonical,
            std::filesystem::directory_options::skip_permission_denied,
            inputError
        };
        const std::filesystem::recursive_directory_iterator end;

        while (!inputError && iterator != end)
        {
            if (summary.itemLimitReached)
            {
                break;
            }

            std::error_code entryError;
            if (iterator->is_regular_file(entryError) && !entryError)
            {
                addCandidate(iterator->path());
            }

            iterator.increment(inputError);
        }

        if (inputError)
        {
            summary.failedPaths.push_back(input);
        }
    }

    std::sort(
        candidates.begin(),
        candidates.end(),
        [](const std::filesystem::path& left,
           const std::filesystem::path& right)
        {
            return CanonicalKey(left) < CanonicalKey(right);
        }
    );

    for (const auto& source : candidates)
    {
        error.clear();
        const std::filesystem::path relative =
            std::filesystem::relative(source, soundsRoot, error);

        if (!error && RelativePathStaysInside(relative))
        {
            summary.importedRelativePaths.push_back(
                relative.lexically_normal()
            );
            ++summary.existingCount;
            continue;
        }

        error.clear();
        const std::filesystem::path destination =
            UniqueDestination(importedFolder, source, error);

        if (error || destination.empty())
        {
            summary.failedPaths.push_back(source);
            continue;
        }

        if (!CopyFileAtomically(source, destination, error))
        {
            summary.failedPaths.push_back(source);
            continue;
        }

        summary.importedRelativePaths.push_back(
            std::filesystem::path{L"Imported"} /
                destination.filename()
        );
        ++summary.copiedCount;
    }

    return summary;
}
