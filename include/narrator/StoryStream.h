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
		// Append a completed chunk of text (one or more lines, '\n' separated).
		// Thread-safe; called from the narrator thread.
		void append(const std::string& text)
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			m_lines.push_back(text);
			m_dirty = true;
		}

		// Start a new active chunk that will receive streamed tokens/pieces.
		void startChunk()
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			m_lines.push_back("");
			m_dirty = true;
		}

		// Append a piece to the currently active chunk in real time as tokens decode.
		void appendToCurrentChunk(const std::string& piece)
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			if (m_lines.empty())
			{
				m_lines.push_back(piece);
			}
			else
			{
				m_lines.back() += piece;
			}
			m_dirty = true;
		}

		// Replace / finalize the active chunk with the final cleaned sentence.
		void updateCurrentChunk(const std::string& text)
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			if (m_lines.empty())
			{
				m_lines.push_back(text);
			}
			else
			{
				m_lines.back() = text;
			}
			m_dirty = true;
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

		// Returns all chunks if new content was appended/streamed since last call.
		bool getChunks(std::vector<std::string>& out)
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			if (!m_dirty)
				return false;
			m_dirty = false;
			out.assign(m_lines.begin(), m_lines.end());
			return true;
		}

		// Drain everything accumulated so far. Thread-safe; called from the
		// main thread every frame.
		std::deque<std::string> drain()
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			std::deque<std::string> out;
			out.swap(m_lines);
			m_dirty = false;
			return out;
		}

		void clear()
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			m_lines.clear();
			m_dirty = true;
			m_status.store(StoryStatus::Idle);
		}

	private:
		std::mutex m_mutex;
		std::deque<std::string> m_lines;
		bool m_dirty = false;
		std::atomic<StoryStatus> m_status{StoryStatus::Idle};
	};
} // namespace wchess
