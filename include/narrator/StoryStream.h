#pragma once

// The stream of story text produced by a narrator. Written from the narrator
// worker thread, drained by the main thread's narrativeRenderSystem. The
// status drives the UI (e.g. "the story ended abruptly" on a blunder/mate).

#include <atomic>
#include <deque>
#include <mutex>
#include <string>

namespace wchess
{
	enum class StoryStatus : uint8_t
	{
		Idle,			// nothing generated yet
		Generating,		// narrator is mid-stream
		EndedAbruptly,	// story stopped suddenly (blunder / checkmate)
		EndedNaturally	// story finished on its own
	};

	class StoryStream
	{
	public:
		// Append a chunk of text (one or more lines, '\n' separated).
		// Thread-safe; called from the narrator thread.
		void append(const std::string& text)
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			m_lines.push_back(text);
		}

		// Set the stream status. Thread-safe.
		void setStatus(StoryStatus status)
		{
			m_status.store(status);
		}

		StoryStatus status() const
		{
			return m_status.load();
		}

		// Drain everything accumulated so far. Thread-safe; called from the
		// main thread every frame.
		std::deque<std::string> drain()
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			std::deque<std::string> out;
			out.swap(m_lines);
			return out;
		}

		void clear()
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			m_lines.clear();
			m_status.store(StoryStatus::Idle);
		}

	private:
		std::mutex m_mutex;
		std::deque<std::string> m_lines;
		std::atomic<StoryStatus> m_status{StoryStatus::Idle};
	};
} // namespace wchess
