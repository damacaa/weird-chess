#pragma once

// MinimaxAI: An in-process, high-performance C++20 chess engine implementing IChessAI.
//
// Features:
// - Negamax formulation with Alpha-Beta pruning
// - Quiescence search on captures to eliminate the horizon effect
// - PeSTO piece-square tables (tapered midgame / endgame positional evaluation)
// - MVV-LVA (Most Valuable Victim - Least Valuable Attacker) move ordering
// - Iterative deepening with time management
// - 100% MIT-licensed, header-only, zero dependencies beyond chess.hpp
// - Works natively in WebAssembly (Emscripten / itch.io) and native platforms

#include "chess.hpp"
#include "chess/ChessLibBoard.h"
#include "chess/ChessTypes.h"
#include "chess/IChessAI.h"
#include "config.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <random>
#include <string>
#include <vector>

namespace wchess
{
	namespace detail
	{
		// PeSTO piece-square tables (Tord Romstad / Ronald de Man / Tomasz Michniewski).
		// Values from White's perspective (Square 0 = A1, Square 63 = H8).

		// Base piece values (MG = Midgame, EG = Endgame) in centipawns
		inline constexpr int MG_VAL[6] = {82, 337, 365, 477, 1025, 0};
		inline constexpr int EG_VAL[6] = {94, 281, 297, 512, 936, 0};
		inline constexpr int GAME_PHASE_WEIGHT[6] = {0, 1, 1, 2, 4, 0};

		// Pawns
		inline constexpr int MG_PAWN_TABLE[64] = {
			0,	0,	 0,	  0,   0,	0,	0,	 0,	  98,  134, 61,	 95,  68, 126, 34, -11, -6, 7,	 26,  31, 65, 56,
			25, -20, -14, 13,  6,	21, 23,	 12,  17,  -23, -27, -2,  -5, 12,  17, 6,	10, -25, -26, -4, -4, -10,
			3,	3,	 33,  -12, -35, -1, -20, -23, -15, 24,	38,	 -22, 0,  0,   0,  0,	0,	0,	 0,	  0,
		};
		inline constexpr int EG_PAWN_TABLE[64] = {
			0,	0,	0,	0,	0,	0, 0,  0,  178, 173, 158, 134, 147, 132, 165, 187, 94, 100, 85, 67, 56, 53,
			82, 84, 32, 24, 13, 5, -2, 4,  17,	17,	 13,  9,   -3,	-7,	 -7,  -8,  3,  -1,	4,	7,	-6, 1,
			0,	-5, -1, -8, 13, 8, 8,  10, 13,	0,	 2,	  -7,  0,	0,	 0,	  0,   0,  0,	0,	0,
		};

		// Knights
		inline constexpr int MG_KNIGHT_TABLE[64] = {
			-167, -89, -34, -49, 61, -97, -15, -107, -73,  -41, 72,	 36,  23,  62,	7,	 -17,
			-47,  60,  37,	65,	 84, 129, 73,  44,	 -9,   17,	19,	 53,  37,  69,	18,	 22,
			-13,  4,   16,	13,	 28, 19,  21,  -8,	 -23,  -9,	12,	 10,  19,  17,	25,	 -16,
			-29,  -53, -12, -3,	 -1, 18,  -14, -19,	 -105, -21, -58, -33, -17, -28, -19, -23,
		};
		inline constexpr int EG_KNIGHT_TABLE[64] = {
			-58, -38, -13, -28, -31, -27, -63, -99, -25, -8,  -25, -2,	-9,	 -25, -24, -52, -24, -20, 10,  9,	-1, -9,
			-19, -41, -17, 3,	22,	 22,  22,  11,	8,	 -18, -18, -6,	16,	 25,  16,  17,	4,	 -18, -23, -3,	-1, 15,
			10,	 -3,  -20, -22, -42, -20, -10, -5,	-2,	 -20, -23, -44, -29, -51, -23, -15, -22, -18, -50, -64,
		};

		// Bishops
		inline constexpr int MG_BISHOP_TABLE[64] = {
			-29, 4,	 -82, -37, -25, -42, 7,	 -8, -26, 16, -18, -13, 30,	 59, 18,  -47, -16, 37,	 43,  40,  35, 50,
			37,	 -2, -4,  5,   19,	50,	 37, 37, 7,	  -2, -6,  13,	13,	 26, 34,  12,  10,	4,	 0,	  15,  15, 15,
			14,	 27, 18,  10,  4,	15,	 16, 0,	 7,	  21, 33,  1,	-33, -3, -14, -21, -13, -12, -39, -21,
		};
		inline constexpr int EG_BISHOP_TABLE[64] = {
			-14, -21, -11, -8,	-7,	 -9,  -17, -24, -8, -4, 7,	 -12, -3,  -13, -4,	 -14, 2,  -8,  0,	-1,	 -2, 6,
			0,	 4,	  -3,  9,	12,	 9,	  14,  10,	3,	2,	-6,	 3,	  13,  19,	7,	 10,  -3, -9,  -12, -3,	 8,	 10,
			13,	 3,	  -7,  -15, -14, -18, -7,  -1,	4,	-9, -15, -27, -23, -9,	-23, -5,  -9, -16, -5,	-17,
		};

		// Rooks
		inline constexpr int MG_ROOK_TABLE[64] = {
			32, 42, 32,	 51,  63,  9,	31,	 43, 27, 32,  58,  62,	80,	 67,  26, 44, -5, 19,  26,	36,	 17,  45,
			61, 16, -24, -11, 7,   26,	24,	 35, -8, -20, -36, -26, -12, -1,  9,  -7, 6,  -23, -45, -25, -16, -17,
			3,	0,	-5,	 -33, -44, -16, -20, -9, -1, 11,  -6,  -71, -19, -13, 1,  17, 16, 7,   -37, -26,
		};
		inline constexpr int EG_ROOK_TABLE[64] = {
			13, 10,	 18, 15,  12, 12, 8, 5, 11, 13, 13,	 11, -3, 3, 8,	3,	7,	7,	 7,	 5,	  4,  -3,
			-5, -3,	 4,	 3,	  13, 1,  2, 1, -1, 2,	3,	 5,	 8,	 4, -5, -6, -8, -11, -4, 0,	  -5, -1,
			-7, -12, -8, -16, -6, -6, 0, 2, -9, -9, -11, -3, -9, 2, 3,	-1, -5, -13, 4,	 -20,
		};

		// Queens
		inline constexpr int MG_QUEEN_TABLE[64] = {
			-28, 0,	 29,  12,  59,	44,	 43, 45, -24, -39, -5, 1,	-16, 57,  28, 54, -13, -17, 7,	 8,	  29,  56,
			47,	 57, -27, -27, -16, -16, -1, 17, -2,  1,   -9, -26, -9,	 -10, -2, -4, 3,   -3,	-14, 2,	  -11, -2,
			-5,	 2,	 14,  5,   -35, -8,	 11, 2,	 8,	  15,  -3, 1,	-1,	 -18, -9, 10, -15, -25, -31, -50,
		};
		inline constexpr int EG_QUEEN_TABLE[64] = {
			-9, 22, 22, 27, 27,	 19,  10,  20,	-17, 20,  32,  41,	58,	 25,  30,  0,	-20, 6,	  9,   49,	47, 35,
			19, 9,	3,	22, 24,	 45,  57,  40,	57,	 36,  -18, 28,	19,	 47,  31,  34,	39,	 18,  -16, -27, 15, 6,
			9,	17, 10, 5,	-22, -23, -30, -16, -16, -23, -36, -32, -33, -28, -22, -43, -5,	 -32, -20, -41,
		};

		// Kings
		inline constexpr int MG_KING_TABLE[64] = {
			-65, 23,  16,  -15, -56, -34, 2,   13,	29,	 -1,  -20, -7, -8,	-4,	 -38, -29, -9,	24,	 2,	  -16, -20, 6,
			22,	 -22, -17, -20, -12, -27, -30, -25, -14, -36, -49, -1, -27, -39, -46, -44, -33, -51, -14, -14, -22, -46,
			-44, -30, -15, -27, 1,	 7,	  -8,  -64, -43, -16, 9,   8,  -15, 36,	 12,  -54, 8,	-28, 24,  14,
		};
		inline constexpr int EG_KING_TABLE[64] = {
			-74, -35, -18, -18, -11, 15,  4,  -17, -12, 17, 14,	 17,  17,  38,	23,	 11,  10,  17,	23,	 15,  20, 45,
			44,	 13,  -8,  22,	24,	 27,  26, 33,  26,	3,	-18, -4,  21,  24,	27,	 23,  9,   -11, -19, -3,  11, 21,
			23,	 16,  7,   -9,	-27, -11, 4,  13,  14,	4,	-5,	 -17, -53, -34, -21, -11, -28, -14, -24, -43,
		};

		inline const int* mgTable(chess::PieceType pt)
		{
			switch (pt.internal())
			{
				case chess::PieceType::PAWN:
					return MG_PAWN_TABLE;
				case chess::PieceType::KNIGHT:
					return MG_KNIGHT_TABLE;
				case chess::PieceType::BISHOP:
					return MG_BISHOP_TABLE;
				case chess::PieceType::ROOK:
					return MG_ROOK_TABLE;
				case chess::PieceType::QUEEN:
					return MG_QUEEN_TABLE;
				default:
					return MG_KING_TABLE;
			}
		}

		inline const int* egTable(chess::PieceType pt)
		{
			switch (pt.internal())
			{
				case chess::PieceType::PAWN:
					return EG_PAWN_TABLE;
				case chess::PieceType::KNIGHT:
					return EG_KNIGHT_TABLE;
				case chess::PieceType::BISHOP:
					return EG_BISHOP_TABLE;
				case chess::PieceType::ROOK:
					return EG_ROOK_TABLE;
				case chess::PieceType::QUEEN:
					return EG_QUEEN_TABLE;
				default:
					return EG_KING_TABLE;
			}
		}
	} // namespace detail

	class MinimaxAI : public IChessAI
	{
	public:
		MinimaxAI() = default;

		bool isAvailable() const override
		{
			return true;
		}

		std::string name() const override
		{
			return "MinimaxAI (in-process)";
		}

		void setStrength(int skill, int elo) override
		{
			// Map elo/skill to max search depth and casual blunder chance.
			// Elo 1320 (casual/beginner): depth 3, 30% blunder chance
			// Elo 1700 (club player): depth 4-5, 10% blunder chance
			// Elo 2500+ (strong master): depth 6, 0% blunder chance
			m_skill = std::clamp(skill, 0, 20);
			m_elo = std::clamp(elo, 1320, 3190);

			if (m_elo < 1450)
			{
				m_searchDepth = 3;
				m_blunderChance = 0.25f;
			}
			else if (m_elo < 1850)
			{
				m_searchDepth = 4;
				m_blunderChance = 0.08f;
			}
			else if (m_elo < 2300)
			{
				m_searchDepth = 5;
				m_blunderChance = 0.0f;
			}
			else
			{
				m_searchDepth = 6;
				m_blunderChance = 0.0f;
			}
		}

		void setPosition(const std::string& fen) override
		{
			m_fen = fen;
			m_board.setFen(fen);
		}

		Move bestMove(const std::vector<Move>& legalMoves) override
		{
			if (legalMoves.empty())
				return Move{};

			// Casual blunder probability for easier difficulty levels
			std::uniform_real_distribution<float> roll(0.0f, 1.0f);
			if (legalMoves.size() > 1 && m_blunderChance > 0.0f && roll(m_rng) < m_blunderChance)
			{
				std::uniform_int_distribution<size_t> dist(0, legalMoves.size() - 1);
				return legalMoves[dist(m_rng)];
			}

			chess::Board board = m_board.raw();
			chess::Move bestInternal = chess::Move::NO_MOVE;

			// Iterative deepening search (bounded by time limit)
			int maxDepth = std::max(1, m_searchDepth);
			auto startTime = std::chrono::steady_clock::now();
			int maxTimeMs = 40;

			for (int depth = 1; depth <= maxDepth; ++depth)
			{
				chess::Move currentBest = chess::Move::NO_MOVE;
				negamax(board, depth, -INFINITY_SCORE, INFINITY_SCORE, true, &currentBest);

				if (currentBest != chess::Move::NO_MOVE)
					bestInternal = currentBest;

				auto elapsed =
					std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - startTime)
						.count();
				if (elapsed >= maxTimeMs && depth >= 2)
					break;
			}

			if (bestInternal != chess::Move::NO_MOVE)
			{
				Move converted = ChessLibBoard::toExternal(bestInternal);
				for (const auto& legal : legalMoves)
				{
					if (legal.from == converted.from && legal.to == converted.to)
					{
						if (!legal.isPromotion || legal.promotion == converted.promotion)
							return legal;
					}
				}
			}

			return legalMoves[0];
		}

		Eval evaluate(const std::string& fen, int movetimeMs) override
		{
			Eval eval;
			chess::Board board(fen);
			eval.valid = true;

			// Check game over conditions
			auto [reason, result] = board.isGameOver();
			if (result != chess::GameResult::NONE)
			{
				if (reason == chess::GameResultReason::CHECKMATE)
				{
					eval.mateIn = -1;
					eval.centipawns = -MATE_SCORE;
				}
				else
				{
					eval.centipawns = 0;
				}
				eval.depth = 1;
				return eval;
			}

			int maxEvalTime = std::min(movetimeMs > 0 ? movetimeMs : 25, 25);
			auto startTime = std::chrono::steady_clock::now();

			chess::Move bestMove = chess::Move::NO_MOVE;
			int score = 0;
			int depthReached = 1;

			// Fast iterative deepening evaluation up to depth 4
			for (int depth = 1; depth <= 4; ++depth)
			{
				chess::Move currentMove = chess::Move::NO_MOVE;
				score = negamax(board, depth, -INFINITY_SCORE, INFINITY_SCORE, true, &currentMove);
				depthReached = depth;
				if (currentMove != chess::Move::NO_MOVE)
					bestMove = currentMove;

				auto elapsed =
					std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - startTime)
						.count();
				if (elapsed >= maxEvalTime && depth >= 2)
					break;
			}

			eval.centipawns = score;
			eval.depth = depthReached;

			if (bestMove != chess::Move::NO_MOVE)
			{
				Move ext = ChessLibBoard::toExternal(bestMove);
				eval.pv = ChessLibBoard::toUci(ext);
			}

			return eval;
		}

		void shutdown() override {}

	private:
		static constexpr int MATE_SCORE = 30000;
		static constexpr int INFINITY_SCORE = 50000;

		// Positional static evaluation using PeSTO tapered piece-square tables
		static int evaluateStatic(const chess::Board& board)
		{
			int mgWhite = 0, egWhite = 0;
			int mgBlack = 0, egBlack = 0;
			int gamePhase = 0;

			auto scorePiece = [&](chess::PieceType pt, chess::Color c)
			{
				chess::Bitboard bb = board.pieces(pt, c);
				const int* mgTbl = detail::mgTable(pt);
				const int* egTbl = detail::egTable(pt);
				int ptIdx = static_cast<int>(pt.internal());

				while (bb)
				{
					int sq = bb.pop();
					gamePhase += detail::GAME_PHASE_WEIGHT[ptIdx];

					if (c == chess::Color::WHITE)
					{
						mgWhite += detail::MG_VAL[ptIdx] + mgTbl[sq];
						egWhite += detail::EG_VAL[ptIdx] + egTbl[sq];
					}
					else
					{
						// Flip square for black (rank 7 <-> rank 0)
						int flippedSq = sq ^ 56;
						mgBlack += detail::MG_VAL[ptIdx] + mgTbl[flippedSq];
						egBlack += detail::EG_VAL[ptIdx] + egTbl[flippedSq];
					}
				}
			};

			scorePiece(chess::PieceType::PAWN, chess::Color::WHITE);
			scorePiece(chess::PieceType::KNIGHT, chess::Color::WHITE);
			scorePiece(chess::PieceType::BISHOP, chess::Color::WHITE);
			scorePiece(chess::PieceType::ROOK, chess::Color::WHITE);
			scorePiece(chess::PieceType::QUEEN, chess::Color::WHITE);
			scorePiece(chess::PieceType::KING, chess::Color::WHITE);

			scorePiece(chess::PieceType::PAWN, chess::Color::BLACK);
			scorePiece(chess::PieceType::KNIGHT, chess::Color::BLACK);
			scorePiece(chess::PieceType::BISHOP, chess::Color::BLACK);
			scorePiece(chess::PieceType::ROOK, chess::Color::BLACK);
			scorePiece(chess::PieceType::QUEEN, chess::Color::BLACK);
			scorePiece(chess::PieceType::KING, chess::Color::BLACK);

			int mgScore = mgWhite - mgBlack;
			int egScore = egWhite - egBlack;

			// Tapered evaluation between midgame and endgame
			int mgPhase = std::min(24, gamePhase);
			int egPhase = 24 - mgPhase;
			int eval = (mgScore * mgPhase + egScore * egPhase) / 24;

			// Return from the perspective of the side to move
			return board.sideToMove() == chess::Color::WHITE ? eval : -eval;
		}

		// Move scoring for Alpha-Beta ordering (MVV-LVA)
		static int scoreMove(const chess::Board& board, const chess::Move& move, const chess::Move& pvMove)
		{
			if (move == pvMove)
				return 100000;

			if (board.isCapture(move))
			{
				chess::Piece victim = board.at(move.to());
				chess::Piece attacker = board.at(move.from());
				int victimVal = (victim != chess::Piece::NONE)
									? detail::MG_VAL[static_cast<int>(victim.type().internal())]
									: detail::MG_VAL[0]; // en passant
				int attackerVal = (attacker != chess::Piece::NONE)
									  ? detail::MG_VAL[static_cast<int>(attacker.type().internal())]
									  : detail::MG_VAL[0];
				return 10000 + (victimVal * 10 - attackerVal);
			}

			if (move.typeOf() == chess::Move::PROMOTION)
				return 9000;

			return 0;
		}

		// Quiescence search: searches captures at leaf nodes to avoid horizon blunders
		static int quiescence(chess::Board& board, int alpha, int beta, int maxQDepth = 4)
		{
			int standPat = evaluateStatic(board);
			if (standPat >= beta)
				return beta;
			if (alpha < standPat)
				alpha = standPat;

			if (maxQDepth <= 0)
				return standPat;

			chess::Movelist captures;
			chess::movegen::legalmoves<chess::movegen::MoveGenType::CAPTURE>(captures, board);

			// Sort captures by MVV-LVA
			std::vector<std::pair<int, chess::Move>> scored;
			scored.reserve(captures.size());
			for (const auto& move : captures)
			{
				scored.push_back({scoreMove(board, move, chess::Move::NO_MOVE), move});
			}
			std::sort(scored.begin(), scored.end(), [](const auto& a, const auto& b) { return a.first > b.first; });

			for (const auto& [_, move] : scored)
			{
				board.makeMove(move);
				int score = -quiescence(board, -beta, -alpha, maxQDepth - 1);
				board.unmakeMove(move);

				if (score >= beta)
					return beta;
				if (score > alpha)
					alpha = score;
			}
			return alpha;
		}

		// Negamax search with Alpha-Beta pruning
		int negamax(chess::Board& board, int depth, int alpha, int beta, bool isRoot = false,
					chess::Move* bestMoveOut = nullptr)
		{
			if (depth <= 0)
				return quiescence(board, alpha, beta);

			auto [reason, result] = board.isGameOver();
			if (result != chess::GameResult::NONE)
			{
				if (reason == chess::GameResultReason::CHECKMATE)
					return -MATE_SCORE + (10 - depth); // Prefer faster checkmates
				return 0;							   // Draw / stalemate
			}

			chess::Movelist moves;
			chess::movegen::legalmoves(moves, board);
			if (moves.empty())
			{
				if (board.inCheck())
					return -MATE_SCORE + (10 - depth);
				return 0;
			}

			// Order moves for optimal alpha-beta pruning
			std::vector<std::pair<int, chess::Move>> scored;
			scored.reserve(moves.size());
			chess::Move pv =
				(bestMoveOut && *bestMoveOut != chess::Move::NO_MOVE) ? *bestMoveOut : chess::Move::NO_MOVE;
			for (const auto& move : moves)
			{
				scored.push_back({scoreMove(board, move, pv), move});
			}
			std::sort(scored.begin(), scored.end(), [](const auto& a, const auto& b) { return a.first > b.first; });

			int bestScore = -INFINITY_SCORE;
			chess::Move localBest = chess::Move::NO_MOVE;

			for (const auto& [_, move] : scored)
			{
				board.makeMove(move);
				int score = -negamax(board, depth - 1, -beta, -alpha);
				board.unmakeMove(move);

				if (score > bestScore)
				{
					bestScore = score;
					localBest = move;
				}

				if (score > alpha)
				{
					alpha = score;
					if (isRoot && bestMoveOut)
						*bestMoveOut = move;
				}

				if (alpha >= beta)
					break; // Beta cutoff
			}

			if (isRoot && bestMoveOut && *bestMoveOut == chess::Move::NO_MOVE)
				*bestMoveOut = localBest;

			return bestScore;
		}

		ChessLibBoard m_board;
		std::string m_fen{chess::constants::STARTPOS};
		int m_skill{ChessConfig::DEFAULT_SKILL};
		int m_elo{ChessConfig::DEFAULT_ELO};
		int m_searchDepth{4};
		float m_blunderChance{0.0f};
		std::mt19937 m_rng{std::random_device{}()};
	};
} // namespace wchess
