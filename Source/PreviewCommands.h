#pragma once

#include "AudioPreview.h"

namespace wavecrate
{
inline bool canStopPreview(const AudioPreview::Snapshot& snapshot)
{
    return snapshot.playRequested
        || snapshot.positionSeconds > 0.0
        || snapshot.state == AudioPreview::State::loading
        || snapshot.state == AudioPreview::State::playing;
}

template <typename Transport>
void togglePreview(Transport& transport)
{
    const auto snapshot = transport.getSnapshot();
    if (snapshot.playRequested || snapshot.state == AudioPreview::State::playing)
    {
        transport.stop();
    }
    else
    {
        transport.play();
    }
}
}
