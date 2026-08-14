#pragma once

// The worker thread that runs narration. The main thread enqueues move
// annotations; the worker pops one at a time and calls the INarrator, which
// writes into a StoryStream the main thread drains each frame.
//
// This is where the "text generation must happen on a different thread"
// requirement is enforced: game systems never call the narrator directly.

#include "chess/ChessTypes.h"
#include "narrator/INarrator.h"
#include "narrator/StoryStream.h"

#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>

namespace wchess
{
	class NarratorThread
	{
	public:
		explicit NarratorThread(std::shared_ptr<INarrator> narrator)
			: m_narrator(std::move(narrator))
			, m_stream(std::make_shared<StoryStream>())
		{
		}

		~NarratorThread()
		{
			stop();
		}

		// Spawns the worker. Safe to call once.
		void start()
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			if (m_running || !m_narrator)
				return;
			m_running = true;
			m_thread = std::thread(&NarratorThread::run, this);
		}

		// Enqueue one annotation for narration. Non-blocking.
		void push(const MoveAnnotation& annotation)
		{
			{
				std::lock_guard<std::mutex> lock(m_mutex);
				if (!m_running)
					return;
				m_queue.push_back(annotation);
			}
			m_cv.notify_one();
		}

		// Signals the worker to finish pending work and join. Idempotent.
		void stop()
		{
			{
				std::lock_guard<std::mutex> lock(m_mutex);
				if (!m_running)
					return;
				m_running = false;
			}
			m_cv.notify_one();
			if (m_thread.joinable())
				m_thread.join();
		}

		// Shared stream the main thread drains. Never null.
		std::shared_ptr<StoryStream> stream() const
		{
			return m_stream;
		}

		std::string narratorName() const
		{
			return m_narrator ? m_narrator->name() : "none";
		}

	private:
		void run()
		{
			while (true)
			{
				MoveAnnotation annotation;
				{
					std::unique_lock<std::mutex> lock(m_mutex);
					m_cv.wait(lock, [this] { return !m_running || !m_queue.empty(); });
					if (!m_running && m_queue.empty())
						break;
					annotation = m_queue.front();
					m_queue.pop_front();
				}
				m_narrator->narrate(annotation, *m_stream);
			}
		}

		std::shared_ptr<INarrator> m_narrator;
		std::shared_ptr<StoryStream> m_stream;

		std::thread m_thread;
		std::mutex m_mutex;
		std::condition_variable m_cv;
		std::deque<MoveAnnotation> m_queue;
		bool m_running = false;
	};
} // namespace wchess
