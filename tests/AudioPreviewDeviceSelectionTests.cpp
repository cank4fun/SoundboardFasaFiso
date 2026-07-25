#include "editor/AudioPreviewDeviceSelection.hpp"

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace
{
    int failures = 0;

    void Expect(const bool condition, const char* const message)
    {
        if (!condition)
        {
            std::cerr << "FAILED: " << message << '\n';
            ++failures;
        }
    }
}

int main()
{
    Expect(
        NormalizeAudioPreviewDeviceRequest("  USB Headset  ") ==
            "usb headset",
        "device requests should be trimmed and ASCII-lowercased"
    );
    Expect(
        ClassifyAudioPreviewDeviceRequest("default") ==
            AudioPreviewDeviceRequestKind::Default,
        "default should select the Windows default playback device"
    );
    Expect(
        ClassifyAudioPreviewDeviceRequest("varsayilan") ==
            AudioPreviewDeviceRequestKind::Default,
        "Turkish default aliases should be accepted"
    );
    Expect(
        ClassifyAudioPreviewDeviceRequest("kapali") ==
            AudioPreviewDeviceRequestKind::Disabled,
        "disabled monitor aliases should be rejected for preview"
    );
    Expect(
        ClassifyAudioPreviewDeviceRequest("USB Headset") ==
            AudioPreviewDeviceRequestKind::Named,
        "ordinary device names should remain named requests"
    );

    const std::vector<std::string> devices{
        "Speakers (Realtek Audio)",
        "Headphones (USB Headset)",
        "CABLE Input (VB-Audio Virtual Cable)"
    };

    const AudioPreviewDeviceMatchResult exact =
        FindAudioPreviewDeviceMatch("headphones (usb headset)", devices);
    Expect(
        exact.status == AudioPreviewDeviceMatchStatus::Found &&
            exact.index == 1U,
        "exact matching should be case-insensitive"
    );

    const AudioPreviewDeviceMatchResult partial =
        FindAudioPreviewDeviceMatch("vb-audio", devices);
    Expect(
        partial.status == AudioPreviewDeviceMatchStatus::Found &&
            partial.index == 2U,
        "a unique partial match should be accepted"
    );

    const std::vector<std::string> ambiguousDevices{
        "Speakers (USB Audio)",
        "Headphones (USB Audio)"
    };
    const AudioPreviewDeviceMatchResult ambiguous =
        FindAudioPreviewDeviceMatch("usb audio", ambiguousDevices);
    Expect(
        ambiguous.status == AudioPreviewDeviceMatchStatus::Ambiguous &&
            !ambiguous.index.has_value(),
        "ambiguous partial matches should be rejected"
    );

    const AudioPreviewDeviceMatchResult missing =
        FindAudioPreviewDeviceMatch("not present", devices);
    Expect(
        missing.status == AudioPreviewDeviceMatchStatus::NotFound &&
            !missing.index.has_value(),
        "missing devices should be reported"
    );

    if (failures != 0)
    {
        std::cerr << failures << " assertion(s) failed.\n";
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
