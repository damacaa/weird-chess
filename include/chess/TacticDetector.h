#pragma once

// Detects the tactical patterns of a single move (forks, pins, skewers,
// discovered attacks, hanging pieces, checks, mates...) using the bitboard
// API of the vendored chess.hpp library. Pure functions: no game state lives
// here. The results feed the MoveClassifier and the AnnotationWriter.

#include "chess/ChessLibBoard.h"
#include "chess/ChessTypes.h"

namespace wchess
{
	namespace TacticDetector
	{
		namespace detail
		{
			// Attacks of the piece type `type` sitting on `sq` with the given
			// occupancy.
			inline chess::Bitboard attacksOf(const chess::Board& board, chess::PieceType type, chess::Square sq,
											 chess::Bitboard occ)
			{
				using namespace chess;
				switch (type.internal())
				{
					case chess::PieceType::PAWN:
						return attacks::pawn(board.sideToMove(), sq); // side to move == owner here
					case chess::PieceType::KNIGHT:
						return attacks::knight(sq);
					case chess::PieceType::BISHOP:
						return attacks::bishop(sq, occ);
					case chess::PieceType::ROOK:
						return attacks::rook(sq, occ);
					case chess::PieceType::QUEEN:
						return attacks::queen(sq, occ);
					case chess::PieceType::KING:
						return attacks::king(sq);
					default:
						return Bitboard(0);
				}
			}
		} // namespace detail

		// Analyzes `move` played in position `before`. The board is copied and
		// the move applied to a scratch board; `before` is left untouched.
		inline TacticInfo analyze(const ChessLibBoard& before, const Move& move)
		{
			TacticInfo info;

			info.capture = move.isCapture;
			info.enPassant = move.isEnPassant;
			info.castling = move.isCastling;
			info.promotion = move.isPromotion;

			ChessLibBoard after = before;
			if (!after.makeMove(move))
				return info;

			const chess::Board& raw = after.raw();
			using namespace chess;

			// The mover is the side to move in the pre-move position.
			const Color moverC = before.sideToMove();
			const chess::Color moverCh = moverC == Color::White ? chess::Color::WHITE : chess::Color::BLACK;
			const chess::Color enemyCh = moverCh == chess::Color::WHITE ? chess::Color::BLACK : chess::Color::WHITE;

			// --- check / mate / draw ---
			info.check = after.inCheck();
			if (info.check)
			{
				CheckType ct = before.raw().givesCheck(ChessLibBoard::toInternal(move));
				info.discoveredCheck = ct == CheckType::DISCOVERY_CHECK;
				info.doubleCheck = info.discoveredCheck && info.check;
			}
			if (after.isGameOver())
			{
				switch (after.gameState())
				{
					case GameState::Checkmate:
						info.checkmate = true;
						break;
					case GameState::Stalemate:
						info.stalemate = true;
						break;
					default:
						info.draw = true;
						break;
				}
			}

			// --- back rank ---
			Square enemyKing = after.kingSquare(moverC == Color::White ? Color::Black : Color::White);
			if (info.check && (enemyKing.rank == 0 || enemyKing.rank == 7))
				info.backRank = true;

			// --- fork ---
			if (!move.isPromotion && !move.isCastling)
			{
				auto piece = before.pieceAt(move.from);
				if (piece && piece->second != PieceType::Pawn && piece->second != PieceType::King)
				{
					chess::Square sq(chess::File(move.to.file), chess::Rank(move.to.rank));
					chess::PieceType pt;
					switch (piece->second)
					{
						case PieceType::Knight:
							pt = chess::PieceType::KNIGHT;
							break;
						case PieceType::Bishop:
							pt = chess::PieceType::BISHOP;
							break;
						case PieceType::Rook:
							pt = chess::PieceType::ROOK;
							break;
						default:
							pt = chess::PieceType::QUEEN;
							break;
					}
					chess::Bitboard attacked = detail::attacksOf(raw, pt, sq, raw.occ()) & raw.us(enemyCh);
					int targets = 0;
					while (attacked)
					{
						chess::Square s(attacked.pop());
						Piece p = raw.at(s);
						// Count only real targets: non-pawns (or the king).
						if (p.type() == chess::PieceType::KING || p.type() == chess::PieceType::KNIGHT ||
							p.type() == chess::PieceType::BISHOP || p.type() == chess::PieceType::ROOK || p.type() == chess::PieceType::QUEEN)
						{
							++targets;
						}
					}
					info.forkTargets = targets;
					info.fork = targets >= 2;
				}
			}

			// --- pin / skewer: the moved piece sits on a ray from the enemy king ---
			{
				auto piece = before.pieceAt(move.from);
				if (piece && !move.isCastling)
				{
					bool isSliderDiag = piece->second == PieceType::Bishop || piece->second == PieceType::Queen;
					bool isSliderOrtho = piece->second == PieceType::Rook || piece->second == PieceType::Queen;
					if (isSliderDiag || isSliderOrtho)
					{
						chess::Square ksq(chess::File(enemyKing.file), chess::Rank(enemyKing.rank));
						chess::Square targetSq(chess::File(move.to.file), chess::Rank(move.to.rank));
						const int dirs[8][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}, {1, 1}, {1, -1}, {-1, 1}, {-1, -1}};
						for (auto& d : dirs)
						{
							bool diag = d[0] != 0 && d[1] != 0;
							bool ortho = d[0] == 0 || d[1] == 0;
							if (diag ? !isSliderDiag : !isSliderOrtho)
								continue;

							// First and second occupied squares along the ray (any color).
							auto piecesOnRay = [&](int df, int dr) {
								std::vector<chess::Square> out;
								int f = static_cast<int>(ksq.file()) + df;
								int r = static_cast<int>(ksq.rank()) + dr;
								while (f >= 0 && f < 8 && r >= 0 && r < 8)
								{
									chess::Square s = chess::Square(chess::File(f), chess::Rank(r));
									if (raw.at(s) != chess::Piece::NONE)
										out.push_back(s);
									f += df;
									r += dr;
								}
								return out;
							};

							auto forward = piecesOnRay(d[0], d[1]);
							auto backward = piecesOnRay(-d[0], -d[1]);

							if (!forward.empty() && forward[0] == targetSq)
							{
								// The mover is the closest piece to the king: if something
								// valuable stands behind the king -> skewer.
								if (!backward.empty())
									info.skewer = true;
							}
							else if (forward.size() >= 2 && forward[1] == targetSq)
							{
								// Exactly one piece (the king's own) between king and mover: pin.
								info.pin = true;
							}
						}
					}
				}
			}

			// --- hangs piece: mover now attacked by the enemy and undefended ---
			{
				chess::Square to(chess::File(move.to.file), chess::Rank(move.to.rank));
				info.hangsPiece = raw.isAttacked(to, enemyCh) && !raw.isAttacked(to, moverCh);
			}

			// --- discovered attack: a friendly slider attacks something new ---
			{
				chess::Bitboard friendlySliders =
					raw.pieces(chess::PieceType::BISHOP, chess::PieceType::ROOK, chess::PieceType::QUEEN) & raw.us(moverCh);
				chess::Bitboard enemyPieces = raw.us(enemyCh);
				chess::Bitboard occNoMover = raw.occ();
				occNoMover.clear(chess::Square(chess::File(move.from.file), chess::Rank(move.from.rank)).index());

				chess::Bitboard attackersNoMover = 0;
				chess::Bitboard attackersWithMover = 0;
				while (friendlySliders)
				{
					chess::Square sq(friendlySliders.pop());
					chess::Piece p = raw.at(sq);
					chess::Bitboard withOcc = detail::attacksOf(raw, p.type(), sq, raw.occ());
					chess::Bitboard noOcc = detail::attacksOf(raw, p.type(), sq, occNoMover);
					attackersNoMover |= noOcc & enemyPieces;
					attackersWithMover |= withOcc & enemyPieces;
				}
				if ((attackersNoMover & ~attackersWithMover))
					info.discoveredAttack = true;
			}

			return info;
		}
	} // namespace TacticDetector
} // namespace wchess
