#pragma once

// Fallback AI: no external binary required. Picks a random legal move and
// evaluates positions with a material count. Used when the Stockfish binary
// is not available, so the game always runs.

#include "chess/ChessLibBoard.h"
#include "chess/IChessAI.h"

#include <random>

namespace wchess
{
	class NullAI : public IChessAI
	{
	public:
		NullAI() = default;

		bool isAvailable() const override
		{
			return true;
		}

		std::string name() const override
		{
			return "nullai (random)";
		}

		void setStrength(int, int) override
		{
		}

		void setPosition(const std::string& fen) override
		{
			m_board.setFen(fen);
		}

		Move bestMove(const std::vector<Move>& legalMoves) override
		{
			if (legalMoves.empty())
				return Move{};
			std::uniform_int_distribution<size_t> dist(0, legalMoves.size() - 1);
			return legalMoves[dist(m_rng)];
		}

		Eval evaluate(const std::string& fen, int) override
		{
			Eval eval;
			m_board.setFen(fen);

			// Simple material count from the side-to-move's perspective.
			int score = 0;
			const chess::Board& raw = m_board.raw();

			auto materialFor = [&](chess::PieceType pt, chess::Color c, int sign) {
				chess::Bitboard b = raw.pieces(pt, c);
				while (b)
				{
					(void)b.pop();
					score += sign * pieceValue(toWchess(pt));
				}
			};

			materialFor(chess::PieceType::PAWN, chess::Color::WHITE, 1);
			materialFor(chess::PieceType::KNIGHT, chess::Color::WHITE, 1);
			materialFor(chess::PieceType::BISHOP, chess::Color::WHITE, 1);
			materialFor(chess::PieceType::ROOK, chess::Color::WHITE, 1);
			materialFor(chess::PieceType::QUEEN, chess::Color::WHITE, 1);
			materialFor(chess::PieceType::PAWN, chess::Color::BLACK, -1);
			materialFor(chess::PieceType::KNIGHT, chess::Color::BLACK, -1);
			materialFor(chess::PieceType::BISHOP, chess::Color::BLACK, -1);
			materialFor(chess::PieceType::ROOK, chess::Color::BLACK, -1);
			materialFor(chess::PieceType::QUEEN, chess::Color::BLACK, -1);

			eval.centipawns = m_board.sideToMove() == Color::White ? score : -score;
			eval.valid = true;
			eval.depth = 0;
			return eval;
		}

		void shutdown() override
		{
		}

	private:
		static PieceType toWchess(chess::PieceType pt)
		{
			switch (pt.internal())
			{
				case chess::PieceType::KNIGHT:
					return PieceType::Knight;
				case chess::PieceType::BISHOP:
					return PieceType::Bishop;
				case chess::PieceType::ROOK:
					return PieceType::Rook;
				case chess::PieceType::QUEEN:
					return PieceType::Queen;
				case chess::PieceType::KING:
					return PieceType::King;
				default:
					return PieceType::Pawn;
			}
		}

		ChessLibBoard m_board;
		std::mt19937 m_rng{std::random_device{}()};
	};
} // namespace wchess
