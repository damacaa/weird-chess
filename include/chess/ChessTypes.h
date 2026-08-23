#pragma once

// Engine-agnostic chess types. Nothing in this file (or the rest of the game)
// knows about chess.hpp or Stockfish: only the adapters under chess/ map
// between these types and the underlying implementations. This is what makes
// the chess engine swappable (see chess/adapters.md).

#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace wchess
{
	// Win probability and game impact categories (Chess.com style win chance bar).
	enum class ImpactLevel : uint8_t
	{
		Minor,
		Major,
		Critical
	};

	inline const char* impactLevelName(ImpactLevel level)
	{
		switch (level)
		{
			case ImpactLevel::Critical:
				return "CRITICAL";
			case ImpactLevel::Major:
				return "MAJOR";
			default:
				return "MINOR";
		}
	}

	// Calculates win probability (0.0 to 1.0) from centipawns using standard
	// Lichess sigmoid model (calibrated for modern chess engines).
	inline float winProbability(int centipawns)
	{
		// P(win) = 1.0 / (1.0 + exp(-0.00368208 * cp))
		return 1.0f / (1.0f + std::exp(-0.00368208f * static_cast<float>(centipawns)));
	}

	enum class Color : uint8_t
	{
		White,
		Black
	};

	enum class PieceType : uint8_t
	{
		Pawn,
		Knight,
		Bishop,
		Rook,
		Queen,
		King
	};

	struct Square
	{
		// file 0-7 (a-h), rank 0-7 (1-8). (0,0) == a1.
		int file = 0;
		int rank = 0;

		constexpr bool operator==(const Square& other) const
		{
			return file == other.file && rank == other.rank;
		}
		constexpr bool operator!=(const Square& other) const
		{
			return !(*this == other);
		}

		constexpr bool valid() const
		{
			return file >= 0 && file < 8 && rank >= 0 && rank < 8;
		}

		// 0-63, rank-major (a1=0 ... h8=63), matches chess.hpp Square::index().
		constexpr int index() const
		{
			return rank * 8 + file;
		}

		static constexpr Square fromIndex(int index)
		{
			return Square{index & 7, index >> 3};
		}

		std::string algebraic() const
		{
			std::string s;
			s += static_cast<char>('a' + file);
			s += static_cast<char>('1' + rank);
			return s;
		}
	};

	struct Move
	{
		Square from;
		Square to;
		PieceType promotion = PieceType::Queen; // only meaningful for promotions

		bool isCapture = false;
		bool isCastling = false;
		bool isEnPassant = false;
		bool isPromotion = false;

		constexpr bool operator==(const Move& other) const
		{
			return from == other.from && to == other.to && (isPromotion ? promotion == other.promotion : true);
		}
	};

	struct Eval
	{
		bool valid = false;
		int centipawns = 0; // positive = good for the side to move in the evaluated position
		int mateIn = 0;		// >0 = side to move mates in N; <0 = side to move gets mated
		int depth = 0;
		std::string pv; // principal variation as uci move string ("e2e4 e7e5 g1f3")
	};

	enum class GameState : uint8_t
	{
		Ongoing,
		Checkmate,
		Stalemate,
		InsufficientMaterial,
		FiftyMoveRule,
		ThreefoldRepetition
	};

	// The tactic patterns detected around a single move. "Player" here means
	// the side that just moved.
	struct TacticInfo
	{
		bool check = false;
		bool doubleCheck = false;
		bool discoveredCheck = false;
		bool checkmate = false;
		bool stalemate = false;
		bool draw = false;

		bool capture = false;
		bool enPassant = false;
		bool castling = false;
		bool promotion = false;

		// Move is a fork: the moved piece attacks at least two enemy pieces.
		bool fork = false;
		int forkTargets = 0;

		// Move pins an enemy piece against their king.
		bool pin = false;

		// Move skewers the enemy king (king in front, valuable piece behind).
		bool skewer = false;

		// The move uncovers an attack by another friendly piece.
		bool discoveredAttack = false;

		// The moved piece now stands attacked by a cheaper/equal enemy piece
		// and is undefended (a "hanging" piece).
		bool hangsPiece = false;

		// The enemy king sits on its back rank and the mate was on the back
		// rank (or a back-rank check).
		bool backRank = false;
	};

	// How good a move was, chess.com-style.
	enum class MoveQuality : uint8_t
	{
		Brilliant,
		Best,
		Great,
		Excellent,
		Good,
		Book,
		Inaccuracy,
		Mistake,
		Blunder,
		Miss,
		Forced
	};

	// The full annotation record for one move. This is what gets shown on the
	// right panel (stage 1) and what will be fed to the LLM narrator
	// (stage 2, see narrator/llamacpp-integration.md).
	struct MoveAnnotation
	{
		Move move;
		Color mover = Color::White;
		int fullMoveNumber = 1; // 1-relative, white moves are odd

		MoveQuality quality = MoveQuality::Good;
		TacticInfo tactics;

		// Evaluation story (centipawns, from the mover's perspective).
		int evalBeforeCp = 0;
		int evalAfterCp = 0;
		int bestMoveCp = 0; // eval of the best move in the before position
		int deltaCp = 0;	// evalAfter - evalBefore (mover's perspective)
		int lossCp = 0;		// how much worse than the best move this was

		// Win probability (0.0 to 1.0, mover's perspective) and impact tier
		float winChanceBefore = 0.5f;
		float winChanceAfter = 0.5f;
		float winChanceDelta = 0.0f; // signed change from mover's perspective
		ImpactLevel impact = ImpactLevel::Minor;

		bool wasBestMove = false;
		bool onlyLegalMove = false;
		bool forced = false;
		bool gameEnded = false;
		GameState gameState = GameState::Ongoing;

		PieceType pieceMoved = PieceType::Pawn;
		bool hasCapture = false;
		PieceType pieceCaptured = PieceType::Pawn;

		bool isTrade = false;
		bool isRecapture = false;
		bool isQueenTrade = false;

		std::string san;		  // simple algebraic notation, e.g. "Nf7x", "e4", "Rd8 mate"
		std::string title;		  // e.g. "BLUNDER", "FORK", "CHECKMATE"
		std::string summary;	  // one human sentence (chess.com style explanation)
		std::string specialEvent; // Special tactical highlight (forks, pins, blunders, etc.)
		std::string tradeEvent;	  // Trade / exchange evaluation (trading blows,
								  // favorable/unfavorable)
		std::string gameStatus;	  // Position evaluation and momentum shifts (tables
								  // turned, tight spot)
		std::string engineLine;	  // best line from the engine, as text
	};

	inline const char* pieceTypeName(PieceType t)
	{
		switch (t)
		{
			case PieceType::Pawn:
				return "Pawn";
			case PieceType::Knight:
				return "Knight";
			case PieceType::Bishop:
				return "Bishop";
			case PieceType::Rook:
				return "Rook";
			case PieceType::Queen:
				return "Queen";
			case PieceType::King:
				return "King";
			default:
				return "Piece";
		}
	}

	inline const char* qualityName(MoveQuality q)
	{
		switch (q)
		{
			case MoveQuality::Brilliant:
				return "BRILLIANT";
			case MoveQuality::Best:
				return "BEST";
			case MoveQuality::Great:
				return "GREAT";
			case MoveQuality::Excellent:
				return "EXCELLENT";
			case MoveQuality::Good:
				return "GOOD";
			case MoveQuality::Book:
				return "BOOK";
			case MoveQuality::Inaccuracy:
				return "INACCURACY";
			case MoveQuality::Mistake:
				return "MISTAKE";
			case MoveQuality::Blunder:
				return "BLUNDER";
			case MoveQuality::Miss:
				return "MISS";
			case MoveQuality::Forced:
				return "FORCED";
		}
		return "?";
	}

	// Material value in centipawns, used by the fallback AI and tactics.
	inline int pieceValue(PieceType t)
	{
		switch (t)
		{
			case PieceType::Pawn:
				return 100;
			case PieceType::Knight:
			case PieceType::Bishop:
				return 300;
			case PieceType::Rook:
				return 500;
			case PieceType::Queen:
				return 900;
			case PieceType::King:
				return 20000;
		}
		return 0;
	}

	inline const char* pieceChar(PieceType t)
	{
		switch (t)
		{
			case PieceType::Pawn:
				return "";
			case PieceType::Knight:
				return "N";
			case PieceType::Bishop:
				return "B";
			case PieceType::Rook:
				return "R";
			case PieceType::Queen:
				return "Q";
			case PieceType::King:
				return "K";
		}
		return "?";
	}
} // namespace wchess
