#pragma once

// Background annotation worker. Move classification needs several engine
// evaluations (~350ms each); running them synchronously on the main thread
// freezes the frame and delays the piece animation. This worker runs the
// annotation pipeline on its own thread: the move is applied and animated
// instantly, the annotation arrives a few hundred ms later and the AI reply
// waits for it (see systems/aiSystem.h).
//
// Threading contract:
//   - submit()   : main thread, non-blocking, at most one job in flight
//   - run()      : worker thread; the ONLY caller of IChessAI::evaluate
//                  while a job is in flight (the main thread must not talk
//                  to the engine concurrently - see aiSystem/uiSystem)
//   - poll()     : main thread, returns the finished annotation once

#include "chess/AnnotationWriter.h"
#include "chess/ChessLibBoard.h"
#include "chess/ChessTypes.h"
#include "chess/IChessAI.h"
#include "chess/MoveClassifier.h"
#include "chess/TacticDetector.h"
#include "config.h"

#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace wchess
{
	// Everything the worker needs to build one annotation. Captured on the
	// main thread at move time; the worker never touches the live board.
	struct AnnotationJob
	{
		std::string beforeFen;
		std::string afterFen;
		Move move;
		Color mover = Color::White;

		Move prevMove;
		bool hasPrevMove = false;

		bool prevEvalValid = false;
		int prevEval = 0; // eval-after of the previous annotation (mover's perspective)

		int legalMoveCount = 0; // legal moves in the BEFORE position
		int fullMoveNumber = 1;

		bool isGameOver = false;
		GameState gameState = GameState::Ongoing;
	};

	namespace AnnotationPipeline
	{
		// Pure annotation builder: only uses the job + the AI for evaluation.
		// Safe to call from any thread (creates its own scratch boards).
		inline MoveAnnotation build(const AnnotationJob& job, IChessAI& ai)
		{
			MoveAnnotation ann;
			ann.move = job.move;
			ann.mover = job.mover;
			ann.fullMoveNumber = job.fullMoveNumber;

			// --- tactics (pure bitboard analysis, no engine needed) ---
			ChessLibBoard before;
			before.setFen(job.beforeFen);
			ann.tactics = TacticDetector::analyze(before, job.move);

			// --- evaluations (from the mover's perspective) ---
			// eval-before: reuse the previous move's eval-after (negated).
			if (job.prevEvalValid)
			{
				ann.evalBeforeCp = -job.prevEval;
			}
			else
			{
				Eval beforeEval = ai.evaluate(job.beforeFen, ChessConfig::EVAL_MOVETIME_MS);
				ann.evalBeforeCp = beforeEval.valid ? beforeEval.centipawns : 0;
			}

			// eval-after: evaluate() reports from the side-to-move's
			// perspective; after the move that is the opponent, so negate.
			Eval after = ai.evaluate(job.afterFen, ChessConfig::EVAL_MOVETIME_MS);
			ann.evalAfterCp = after.valid ? -after.centipawns : ann.evalBeforeCp;

			// best-move eval of the before position (side to move == mover).
			Eval best = ai.evaluate(job.beforeFen, ChessConfig::EVAL_MOVETIME_MS);
			ann.bestMoveCp = best.valid ? best.centipawns : ann.evalBeforeCp;
			if (best.valid && !best.pv.empty())
				ann.engineLine = best.pv;

			ann.deltaCp = ann.evalAfterCp - ann.evalBeforeCp;
			ann.lossCp = ann.bestMoveCp - ann.evalAfterCp;

			// Win probability and impact calculation (Chess.com style win chance bar)
			ann.winChanceBefore = winProbability(ann.evalBeforeCp);
			ann.winChanceAfter = winProbability(ann.evalAfterCp);
			ann.winChanceDelta = ann.winChanceAfter - ann.winChanceBefore;

			float winShiftAbs = std::abs(ann.winChanceDelta);
			if (winShiftAbs >= 0.35f || ann.tactics.checkmate)
				ann.impact = ImpactLevel::Critical;
			else if (winShiftAbs >= 0.15f)
				ann.impact = ImpactLevel::Major;
			else
				ann.impact = ImpactLevel::Minor;

			ann.wasBestMove = ann.lossCp <= static_cast<int>(ChessConfig::BEST_LOSS_PAWNS * 100.0f);
			ann.onlyLegalMove = job.legalMoveCount <= 1;
			ann.forced = ann.onlyLegalMove;

			// --- classify ---
			ClassificationInput input;
			input.evalBeforeCp = ann.evalBeforeCp;
			input.evalAfterCp = ann.evalAfterCp;
			input.bestMoveCp = ann.bestMoveCp;
			input.tactics = ann.tactics;
			input.move = job.move;
			input.legalMoveCount = job.legalMoveCount;
			ann.quality = classify(input);

			// --- decorate with semantic text ---
			auto movingPiece = before.pieceAt(job.move.from);
			const char* letter = "";
			if (movingPiece)
			{
				ann.pieceMoved = movingPiece->second;
				PieceType t = movingPiece->second;
				letter = t == PieceType::Knight	  ? "N"
						 : t == PieceType::Bishop ? "B"
						 : t == PieceType::Rook	  ? "R"
						 : t == PieceType::Queen  ? "Q"
						 : t == PieceType::King	  ? "K"
												  : "";
			}

			if (job.move.isEnPassant)
			{
				ann.hasCapture = true;
				ann.pieceCaptured = PieceType::Pawn;
			}
			else
			{
				auto capturedPiece = before.pieceAt(job.move.to);
				if (capturedPiece)
				{
					ann.hasCapture = true;
					ann.pieceCaptured = capturedPiece->second;
				}
			}

			// --- Trade / Exchange Detection ---
			bool isRecapture = job.hasPrevMove && job.prevMove.isCapture && job.move.isCapture &&
							   (job.move.to == job.prevMove.to || job.move.to == job.prevMove.from);

			bool isEqualMaterialCapture =
				ann.hasCapture && (pieceValue(ann.pieceMoved) == pieceValue(ann.pieceCaptured) ||
								   (ann.pieceMoved == PieceType::Bishop && ann.pieceCaptured == PieceType::Knight) ||
								   (ann.pieceMoved == PieceType::Knight && ann.pieceCaptured == PieceType::Bishop));

			ann.isTrade = ann.hasCapture && (isRecapture || isEqualMaterialCapture);
			ann.isRecapture = isRecapture;
			ann.isQueenTrade =
				ann.hasCapture && ann.pieceMoved == PieceType::Queen && ann.pieceCaptured == PieceType::Queen;

			AnnotationWriter::decorate(ann, letter);

			// --- game end ---
			if (job.isGameOver)
			{
				ann.gameEnded = true;
				ann.gameState = job.gameState;
			}
			else
			{
				ChessLibBoard afterBoard;
				afterBoard.setFen(job.afterFen);
				if (afterBoard.isGameOver())
				{
					ann.gameEnded = true;
					ann.gameState = afterBoard.gameState();
				}
			}

			return ann;
		}
	} // namespace AnnotationPipeline

	class AsyncAnnotator
	{
	public:
		explicit AsyncAnnotator(std::shared_ptr<IChessAI> ai)
			: m_ai(std::move(ai))
		{
		}

		~AsyncAnnotator()
		{
			stop();
		}

		AsyncAnnotator(const AsyncAnnotator&) = delete;
		AsyncAnnotator& operator=(const AsyncAnnotator&) = delete;

		void start()
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			if (m_running)
				return;
			m_running = true;
			m_thread = std::thread(&AsyncAnnotator::run, this);
		}

		// Queues a job. Returns false if a job is already in flight or the
		// worker is stopped.
		bool submit(AnnotationJob job)
		{
			{
				std::lock_guard<std::mutex> lock(m_mutex);
				if (!m_running || m_hasJob)
					return false;
				m_job = std::move(job);
				m_hasJob = true;
				m_hasResult = false;
			}
			m_cv.notify_one();
			return true;
		}

		// True while the worker is still processing a job.
		bool busy() const
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			return m_hasJob;
		}

		// Returns true once when a finished annotation is available.
		// Call from the main thread; safe to call every frame.
		bool poll(MoveAnnotation& out)
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			if (!m_hasResult)
				return false;
			out = m_result;
			m_hasResult = false;
			return true;
		}

		void stop()
		{
			{
				std::lock_guard<std::mutex> lock(m_mutex);
				if (!m_running)
					return;
				m_running = false;
				m_hasJob = false;
			}
			m_cv.notify_one();
			if (m_thread.joinable())
				m_thread.join();
		}

	private:
		void run()
		{
			while (true)
			{
				AnnotationJob job;
				{
					std::unique_lock<std::mutex> lock(m_mutex);
					m_cv.wait(lock, [this] { return !m_running || m_hasJob; });
					if (!m_running && !m_hasJob)
						break;
					job = m_job;
				}

				MoveAnnotation result = m_ai ? AnnotationPipeline::build(job, *m_ai) : MoveAnnotation{};

				{
					std::lock_guard<std::mutex> lock(m_mutex);
					m_hasJob = false;
					if (m_running)
					{
						m_result = std::move(result);
						m_hasResult = true;
					}
				}
			}
		}

		std::shared_ptr<IChessAI> m_ai;
		std::thread m_thread;
		mutable std::mutex m_mutex;
		std::condition_variable m_cv;
		bool m_running = false;

		bool m_hasJob = false;
		AnnotationJob m_job;

		bool m_hasResult = false;
		MoveAnnotation m_result;
	};
} // namespace wchess
