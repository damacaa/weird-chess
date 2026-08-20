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

#if defined(__linux__) || defined(__APPLE__)
#include <sys/resource.h>
#endif

#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>

namespace wchess
{
	enum class NarratorJobType
	{
		Move,
		Intro
	};

	struct NarratorJob
	{
		NarratorJobType type = NarratorJobType::Move;
		MoveAnnotation annotation;
	};

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
				m_queue.push_back({NarratorJobType::Move, annotation});
			}
			m_cv.notify_one();
		}

		// Enqueue intro generation. Non-blocking.
		void pushIntro()
		{
			{
				std::lock_guard<std::mutex> lock(m_mutex);
				if (!m_running)
					return;
				m_queue.push_back({NarratorJobType::Intro, {}});
			}
			m_cv.notify_one();
		}

		// Signals the worker to finish pending work and join. Idempotent.
		void stop()
		{
			if (m_narrator)
				m_narrator->cancel();
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

		void reset()
		{
			if (m_narrator)
				m_narrator->reset();
			{
				std::lock_guard<std::mutex> lock(m_mutex);
				m_queue.clear();
			}
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

		bool busy() const
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			return !m_queue.empty();
		}

	private:
		void run()
		{
#if defined(__linux__) || defined(__APPLE__)
			// Lower priority of background inference worker to guarantee the main game
			// loop and OpenGL rendering maintain smooth 60 FPS without core starvation.
			setpriority(PRIO_PROCESS, 0, 10);
#endif
			while (true)
			{
				NarratorJob job;
				{
					std::unique_lock<std::mutex> lock(m_mutex);
					m_cv.wait(lock, [this] { return !m_running || !m_queue.empty(); });
					if (!m_running && m_queue.empty())
						break;
					job = m_queue.front();
					m_queue.pop_front();
				}
				if (job.type == NarratorJobType::Intro)
				{
					m_narrator->narrateIntro(*m_stream);
				}
				else
				{
					m_narrator->narrate(job.annotation, *m_stream);
				}
			}
		}

		std::shared_ptr<INarrator> m_narrator;
		std::shared_ptr<StoryStream> m_stream;

		std::thread m_thread;
		mutable std::mutex m_mutex;
		std::condition_variable m_cv;
		std::deque<NarratorJob> m_queue;
		bool m_running = false;
	};
} // namespace wchess
