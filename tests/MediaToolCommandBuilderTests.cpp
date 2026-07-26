#include "platform/MediaToolCommandBuilder.hpp"

#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace
{
    bool Expect(const bool condition, const char* message)
    {
        if (!condition)
        {
            std::cerr << message << '\n';
        }

        return condition;
    }

    bool ExpectArguments(
        const MediaToolCommandBuildResult& result,
        const MediaToolKind expectedTool,
        const std::vector<std::wstring>& expectedArguments,
        const char* message
    )
    {
        if (!Expect(result.Succeeded(), message))
        {
            std::cerr << result.errorMessage << '\n';
            return false;
        }

        return Expect(
            result.command->tool == expectedTool &&
                result.command->arguments == expectedArguments,
            message
        );
    }
}

int main()
{
    const std::filesystem::path inputPath =
        LR"(C:\Media Folder\input file.webm)";

    const std::filesystem::path outputPath =
        LR"(C:\Media Folder\output file.wav)";

    const std::filesystem::path denoPath =
        LR"(C:\Program Files\SoundBoardFasaFiso\tools\deno.exe)";

    const std::wstring url =
        L"https://example.com/watch?v=value&list=ignored";

    const MediaToolCommandBuildResult probe =
        MediaToolCommandBuilder::ProbeAudio(inputPath);

    if (!ExpectArguments(
            probe,
            MediaToolKind::Ffprobe,
            {
                L"-hide_banner",
                L"-v",
                L"error",
                L"-select_streams",
                L"a:0",
                L"-show_entries",
                L"stream=codec_name,sample_rate,channels,channel_layout:"
                    L"format=duration",
                L"-of",
                L"json",
                L"-i",
                inputPath.wstring()
            },
            "The ffprobe arguments were incorrect."
        ))
    {
        return 1;
    }

    const MediaToolCommandBuildResult conversion =
        MediaToolCommandBuilder::ConvertToPcm16Wav(
            inputPath,
            outputPath,
            48000U,
            2U
        );

    if (!ExpectArguments(
            conversion,
            MediaToolKind::Ffmpeg,
            {
                L"-hide_banner",
                L"-loglevel",
                L"error",
                L"-nostdin",
                L"-y",
                L"-i",
                inputPath.wstring(),
                L"-map",
                L"0:a:0",
                L"-vn",
                L"-sn",
                L"-dn",
                L"-map_metadata",
                L"-1",
                L"-acodec",
                L"pcm_s16le",
                L"-ar",
                L"48000",
                L"-ac",
                L"2",
                L"-f",
                L"wav",
                outputPath.wstring()
            },
            "The FFmpeg conversion arguments were incorrect."
        ))
    {
        return 1;
    }

    const MediaToolCommandBuildResult metadata =
        MediaToolCommandBuilder::FetchMetadata(url, denoPath);

    if (!ExpectArguments(
            metadata,
            MediaToolKind::YtDlp,
            {
                L"--no-playlist",
                L"--skip-download",
                L"--dump-single-json",
                L"--no-warnings",
                L"--no-progress",
                L"--js-runtimes",
                L"deno:" + denoPath.wstring(),
                L"--",
                url
            },
            "The yt-dlp metadata arguments were incorrect."
        ))
    {
        return 1;
    }

    const std::filesystem::path outputTemplate =
        LR"(C:\Temporary Import\source.%(ext)s)";

    const MediaToolCommandBuildResult download =
        MediaToolCommandBuilder::DownloadBestAudio(
            url,
            outputTemplate,
            denoPath
        );

    if (!ExpectArguments(
            download,
            MediaToolKind::YtDlp,
            {
                L"--no-playlist",
                L"--no-progress",
                L"--newline",
                L"--no-part",
                L"--format",
                L"bestaudio/best",
                L"--output",
                outputTemplate.wstring(),
                L"--print",
                L"after_move:filepath",
                L"--js-runtimes",
                L"deno:" + denoPath.wstring(),
                L"--",
                url
            },
            "The yt-dlp download arguments were incorrect."
        ))
    {
        return 1;
    }

    if (!Expect(
            !MediaToolCommandBuilder::ProbeAudio({}).Succeeded(),
            "An empty ffprobe input path was accepted."
        ) ||
        !Expect(
            !MediaToolCommandBuilder::ConvertToPcm16Wav(
                inputPath,
                inputPath,
                48000U,
                2U
            ).Succeeded(),
            "Identical FFmpeg input and output paths were accepted."
        ) ||
        !Expect(
            !MediaToolCommandBuilder::ConvertToPcm16Wav(
                inputPath,
                outputPath,
                7999U,
                2U
            ).Succeeded(),
            "An invalid sample rate was accepted."
        ) ||
        !Expect(
            !MediaToolCommandBuilder::ConvertToPcm16Wav(
                inputPath,
                outputPath,
                48000U,
                3U
            ).Succeeded(),
            "An invalid channel count was accepted."
        ) ||
        !Expect(
            !MediaToolCommandBuilder::FetchMetadata(
                L"file:///C:/secret.txt",
                denoPath
            ).Succeeded(),
            "A non-HTTP URL was accepted."
        ) ||
        !Expect(
            !MediaToolCommandBuilder::FetchMetadata(
                L"https://example.com/\nargument",
                denoPath
            ).Succeeded(),
            "A URL containing a control character was accepted."
        ) ||
        !Expect(
            !MediaToolCommandBuilder::DownloadBestAudio(
                url,
                LR"(C:\Temporary Import\source.webm)",
                denoPath
            ).Succeeded(),
            "An output template without %(ext)s was accepted."
        ) ||
        !Expect(
            !MediaToolCommandBuilder::DownloadBestAudio(
                url,
                outputTemplate,
                {}
            ).Succeeded(),
            "An empty Deno path was accepted."
        ))
    {
        return 1;
    }

    std::cout << "MediaToolCommandBuilder tests passed.\n";
    return 0;
}