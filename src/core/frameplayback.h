#pragma once

#include <cstddef>
#include <utility>
#include <vector>

namespace aqtruncat {

// Position inside a runner's playback sequence. The sequence lists frame
// indices in display order (usually 0..n-1; a runner may repeat frames, e.g.
// 0 1 2 3 4 3 2 1). Knows nothing about time, FPS or CPU load.
class FramePlayback
{
public:
    FramePlayback() = default;
    explicit FramePlayback(std::vector<int> sequence) : m_sequence(std::move(sequence)) {}

    // Replaces the sequence and restarts from its first entry: positions of
    // a previous runner have no meaning for a new one.
    void setSequence(std::vector<int> sequence)
    {
        m_sequence = std::move(sequence);
        m_position = 0;
    }

    bool isEmpty() const { return m_sequence.empty(); }
    std::size_t length() const { return m_sequence.size(); }
    std::size_t position() const { return m_position; }

    // Frame index to show now, or -1 without a sequence.
    int currentFrame() const { return m_sequence.empty() ? -1 : m_sequence[m_position]; }

    // Moves to the next entry, wrapping around, and returns its frame index.
    int advance()
    {
        if (m_sequence.empty())
            return -1;
        m_position = (m_position + 1) % m_sequence.size();
        return m_sequence[m_position];
    }

private:
    std::vector<int> m_sequence;
    std::size_t m_position = 0;
};

} // namespace aqtruncat
